#include "lsw/manager/mcp/mcp_server.hpp"

#include "lsw/log.hpp"
#include "lsw/manager/apps/log_store.hpp"
#include "lsw/manager/vm/agent_rpc_client.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMetaObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QtConcurrent>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace lsw::manager::mcp {

namespace {

// Build the standard MCP tool-call ``result`` body — single text
// content block. ``is_error`` flags the call as failed so the LLM
// surfaces it visibly.
nlohmann::json text_result(const std::string& text, bool is_error = false) {
    return {
        {"content", nlohmann::json::array({{{"type", "text"}, {"text", text}}})},
        {"isError", is_error},
    };
}

nlohmann::json json_result(const nlohmann::json& payload) {
    return text_result(payload.dump(2));
}

nlohmann::json error_result(const std::string& msg) {
    return text_result(msg, true);
}

// Tool input schemas — JSON Schema fragments. Kept inline because
// they're each short and live right next to the tool that uses them.
nlohmann::json schema_no_args() {
    return {{"type", "object"}, {"properties", nlohmann::json::object()}};
}

nlohmann::json schema_with(const nlohmann::json& props,
                            const std::vector<std::string>& required = {}) {
    nlohmann::json s = {{"type", "object"}, {"properties", props}};
    if (!required.empty()) {
        nlohmann::json req = nlohmann::json::array();
        for (const auto& r : required) req.push_back(r);
        s["required"] = std::move(req);
    }
    return s;
}

// MCP error codes (JSON-RPC 2.0 + MCP additions).
constexpr int kErrParseError      = -32700;
constexpr int kErrInvalidRequest  = -32600;
constexpr int kErrMethodNotFound  = -32601;
constexpr int kErrInvalidParams   = -32602;
constexpr int kErrInternalError   = -32603;

nlohmann::json jsonrpc_error(const nlohmann::json& id, int code, const std::string& msg) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id.is_null() ? nlohmann::json() : id},
        {"error", {{"code", code}, {"message", msg}}},
    };
}

nlohmann::json jsonrpc_result(const nlohmann::json& id, nlohmann::json result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", std::move(result)},
    };
}

}  // namespace

McpServer::McpServer(vm::AgentRpcClient& rpc, apps::LogStore& log_store, QObject* parent)
    : QObject(parent), rpc_(rpc), log_store_(log_store), server_(new QTcpServer(this)) {
    connect(server_, &QTcpServer::newConnection, this, &McpServer::on_new_connection);

    // Subscribe to ``console_output`` events so ``console_read`` has
    // accumulated bytes to return when the LLM polls. Same on the
    // ``console_closed`` topic — record the exit code as a synthetic
    // tail line so the LLM sees the session ended.
    // McpServer lives for the App's lifetime; the handlers are fine
    // to outlive any individual call. Tokens kept so the destructor
    // unregisters cleanly during shutdown teardown.
    output_token_ = rpc_.on_event(QStringLiteral("console_output"),
                                    [this](const nlohmann::json& f) {
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        const auto b64 = QByteArray::fromStdString(f.value("data_b64", std::string{}));
        if (sid.isEmpty()) return;
        QByteArray bytes = QByteArray::fromBase64(b64);
        std::lock_guard<std::mutex> lock(sessions_mu_);
        auto& buf = sessions_[sid];
        buf.append(bytes);
        if (buf.size() > static_cast<qsizetype>(kSessionBufferCap)) {
            buf.remove(0, buf.size() - static_cast<qsizetype>(kSessionBufferCap));
        }
    });
    closed_token_ = rpc_.on_event(QStringLiteral("console_closed"),
                                    [this](const nlohmann::json& f) {
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        const auto code = f.value("exit_code", 0);
        if (sid.isEmpty()) return;
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[sid].append(
            QStringLiteral("\n[session exited, code=%1]\n").arg(code).toUtf8());
    });
}

McpServer::~McpServer() {
    rpc_.off_event(output_token_);
    rpc_.off_event(closed_token_);
}

bool McpServer::listen(std::uint16_t port) {
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        LSW_ERROR("mcp: listen on localhost:{} failed: {}", port,
                  server_->errorString().toStdString());
        return false;
    }
    LSW_INFO("mcp: listening on http://127.0.0.1:{}", server_->serverPort());
    return true;
}

std::uint16_t McpServer::port() const noexcept {
    return server_->isListening() ? static_cast<std::uint16_t>(server_->serverPort()) : 0;
}

void McpServer::on_new_connection() {
    while (auto* sock = server_->nextPendingConnection()) {
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] { handle_socket(sock); });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void McpServer::handle_socket(QTcpSocket* sock) {
    // Minimal HTTP/1.1 request parser. We accept POST / with a
    // Content-Length body containing JSON-RPC; everything else gets
    // a 405 / 404. Keep-alive isn't supported (Connection: close on
    // every reply) so we don't need to handle multiple requests per
    // connection.
    const auto all = sock->peek(64 * 1024);
    const qsizetype header_end = all.indexOf("\r\n\r\n");
    if (header_end < 0) {
        return;  // headers not complete yet; another readyRead will fire
    }
    const QByteArray header_block = all.left(header_end);
    const qsizetype body_start = header_end + 4;

    // Parse the request line + Content-Length.
    const QList<QByteArray> lines = header_block.split('\n');
    if (lines.isEmpty()) {
        send_http_response(sock, 400, R"({"error":"bad request"})");
        return;
    }
    const auto request_line = QString::fromUtf8(lines.first()).trimmed();
    int content_length = 0;
    for (const auto& l : lines) {
        const auto line = QString::fromUtf8(l).trimmed();
        if (line.startsWith(QStringLiteral("Content-Length:"), Qt::CaseInsensitive)) {
            content_length = line.mid(QStringLiteral("Content-Length:").size())
                                  .trimmed()
                                  .toInt();
        }
    }

    // Wait for the full body before consuming the bytes.
    if (sock->bytesAvailable() < body_start + content_length) {
        return;
    }
    // Drop headers + body off the read buffer.
    sock->read(body_start);
    const QByteArray body = sock->read(content_length);

    if (!request_line.startsWith(QStringLiteral("POST"))) {
        send_http_response(sock, 405, R"({"error":"method not allowed; use POST"})");
        return;
    }

    nlohmann::json req;
    try {
        req = nlohmann::json::parse(body.constData(), body.constData() + body.size());
    } catch (const nlohmann::json::exception& e) {
        const auto err = jsonrpc_error(nullptr, kErrParseError, e.what()).dump();
        send_http_response(sock, 400, err);
        return;
    }

    // Tool calls into the agent can block (rpc_.call is synchronous).
    // Dispatch on a worker so the GUI thread isn't stuck waiting on
    // a slow VM. The response is written back via QueuedConnection.
    const QPointer<QTcpSocket> sock_ptr{sock};
    const QPointer<McpServer> self{this};
    (void)QtConcurrent::run([self, sock_ptr, req]() {
        if (!self) return;
        nlohmann::json reply = self->dispatch_jsonrpc(req);
        const std::string body = reply.dump();
        QMetaObject::invokeMethod(
            self.data(),
            [sock_ptr, body]() {
                if (sock_ptr) {
                    send_http_response(sock_ptr.data(), 200, body);
                }
            },
            Qt::QueuedConnection);
    });
}

nlohmann::json McpServer::dispatch_jsonrpc(const nlohmann::json& req) {
    if (!req.is_object() || req.value("jsonrpc", std::string{}) != "2.0") {
        return jsonrpc_error(req.value("id", nlohmann::json()), kErrInvalidRequest,
                              "not a JSON-RPC 2.0 frame");
    }
    const auto id = req.value("id", nlohmann::json());
    const auto method = req.value("method", std::string{});
    const auto params = req.contains("params") ? req["params"] : nlohmann::json::object();

    try {
        if (method == "initialize")      return jsonrpc_result(id, on_initialize(params));
        if (method == "tools/list")      return jsonrpc_result(id, on_tools_list());
        if (method == "tools/call")      return jsonrpc_result(id, on_tools_call(params));
        // Common harmless probes — answer empty so the client moves on.
        if (method == "notifications/initialized") return jsonrpc_result(id, {});
        if (method == "resources/list")  return jsonrpc_result(id,
                                              {{"resources", nlohmann::json::array()}});
        if (method == "prompts/list")    return jsonrpc_result(id,
                                              {{"prompts", nlohmann::json::array()}});
        if (method == "ping")            return jsonrpc_result(id, {});
        return jsonrpc_error(id, kErrMethodNotFound, "unknown method: " + method);
    } catch (const std::exception& e) {
        return jsonrpc_error(id, kErrInternalError, e.what());
    }
}

nlohmann::json McpServer::on_initialize(const nlohmann::json& /*params*/) {
    return {
        {"protocolVersion", "2024-11-05"},
        {"serverInfo", {{"name", "lsw-manager"}, {"version", "0.0.1"}}},
        {"capabilities", {{"tools", nlohmann::json::object()}}},
    };
}

nlohmann::json McpServer::on_tools_list() {
    nlohmann::json tools = nlohmann::json::array();
    auto add = [&](const char* name, const char* desc, nlohmann::json schema) {
        tools.push_back({
            {"name", name},
            {"description", desc},
            {"inputSchema", std::move(schema)},
        });
    };

    add("system_info",
        "Returns the guest VM's OS version, hostname, IPv4, total/available RAM. "
        "No arguments. Use this first to confirm the agent is reachable.",
        schema_no_args());

    add("list_apps",
        "Enumerate apps installed in the VM (Add/Remove Programs view). "
        "Returns a JSON array of {id, name, exe_path}. Slow first call (a few "
        "hundred ms for icon extraction).",
        schema_no_args());

    add("list_windows",
        "Enumerate top-level visible windows in the VM. Returns "
        "{hwnd, pid, title, exe_path, client_w, client_h} per window.",
        schema_no_args());

    add("launch",
        "Spawn an app in the user session. ``app_id`` is an absolute path to "
        "an .exe or .lnk (use ``list_apps`` to find one). Returns the spawned "
        "PID — feed that to ``list_windows`` filtering to find the new window.",
        schema_with({{"app_id", {{"type", "string"}}}}, {"app_id"}));

    add("console_open",
        "Spawn a fresh cmd.exe in the VM and return its ``session_id``. Use "
        "``console_input`` to send commands, ``console_read`` to poll output, "
        "``console_close`` to terminate.",
        schema_no_args());

    add("console_input",
        "Send a line to cmd.exe stdin. ``data`` is the raw text; if it doesn't "
        "end with ``\\r\\n`` you'll need to send the line break separately. "
        "Typical pattern: send ``cd C:\\Users\\\\r\\n``, then ``console_read``.",
        schema_with({
            {"session_id", {{"type", "string"}}},
            {"data", {{"type", "string"}}},
        }, {"session_id", "data"}));

    add("console_read",
        "Return + clear the accumulated stdout/stderr bytes for ``session_id``. "
        "Empty string if nothing has been received yet; call again after a "
        "moment if you just sent input.",
        schema_with({{"session_id", {{"type", "string"}}}}, {"session_id"}));

    add("console_close",
        "Terminate the cmd.exe session.",
        schema_with({{"session_id", {{"type", "string"}}}}, {"session_id"}));

    add("recent_logs",
        "Return the last N log records the agent has pushed to the manager. "
        "Useful when debugging — these are the same lines as in ``Open Log``. "
        "Args: ``count`` (default 100).",
        schema_with({{"count", {{"type", "integer"}}}}));

    add("rpc",
        "Escape hatch: forward an arbitrary RPC frame to the agent. ``cmd`` is "
        "the verb (e.g. ``close_window``), ``params`` is the args object. "
        "Returns the raw reply body. Use this for verbs not exposed as a "
        "dedicated tool.",
        schema_with({
            {"cmd", {{"type", "string"}}},
            {"params", {{"type", "object"}}},
        }, {"cmd"}));

    add("redeploy_agent",
        "Hot-swap the running guest agent with the locally-built "
        "``lsw-guest-agent.exe`` (found via ``$LSW_AGENT_EXE`` or the "
        "build-tree default). The manager spins up a one-shot HTTP server "
        "the guest reaches at ``10.0.2.2:<port>``, sends a swap script "
        "(curl download → taskkill → move → relaunch), then polls "
        "``system_info`` until the ``agent_version`` field changes to "
        "confirm the new binary is running. Blocks for up to ~30 s.",
        schema_no_args());

    return {{"tools", std::move(tools)}};
}

nlohmann::json McpServer::on_tools_call(const nlohmann::json& params) {
    const auto name = params.value("name", std::string{});
    const auto args =
        (params.contains("arguments") && params["arguments"].is_object())
            ? params["arguments"]
            : nlohmann::json::object();

    if (!rpc_.has_target() && name != "recent_logs") {
        return error_result("No VM is currently targeted — cannot reach the agent.");
    }

    if (name == "system_info")    return tool_system_info(args);
    if (name == "list_apps")      return tool_list_apps(args);
    if (name == "list_windows")   return tool_list_windows(args);
    if (name == "launch")         return tool_launch(args);
    if (name == "console_open")   return tool_console_open(args);
    if (name == "console_input")  return tool_console_input(args);
    if (name == "console_read")   return tool_console_read(args);
    if (name == "console_close")  return tool_console_close(args);
    if (name == "recent_logs")    return tool_recent_logs(args);
    if (name == "rpc")            return tool_rpc(args);
    if (name == "redeploy_agent") return tool_redeploy_agent(args);
    return error_result("unknown tool: " + name);
}

// ---- Tool implementations -------------------------------------------------

namespace {

// Convenience: call the agent with the given verb and return either
// the parsed reply or an error result. Handles the "empty bytes
// means transport failure" and "{error:...} body means agent error"
// cases uniformly.
struct AgentCall {
    bool ok;
    nlohmann::json body;
    std::string error;
};

// Locate the host-side cross-built ``lsw-guest-agent.exe``. ``LSW_AGENT_EXE``
// wins; otherwise we fall back to the build-tree default
// (``build-agent/src/agent/lsw-guest-agent.exe`` relative to the
// manager binary's grandparent). Same lookup logic as InstallPage uses
// when bundling the payload ISO.
[[nodiscard]] QString locate_agent_exe() {
    const QString from_env = qEnvironmentVariable("LSW_AGENT_EXE");
    if (!from_env.isEmpty() && QFile::exists(from_env)) {
        return from_env;
    }
    const QString candidate = QDir::cleanPath(
        QCoreApplication::applicationDirPath()
        + "/../../../build-agent/src/agent/lsw-guest-agent.exe");
    if (QFile::exists(candidate)) {
        return candidate;
    }
    return {};
}

AgentCall call_agent(vm::AgentRpcClient& rpc, const std::string& cmd,
                      const nlohmann::json& params = nlohmann::json::object()) {
    nlohmann::json req = {{"cmd", cmd}};
    if (!params.empty()) req["params"] = params;
    const auto reply = rpc.call(req);
    if (reply.isEmpty()) {
        return {false, {}, "agent unreachable (no reply)"};
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                              reply.constData() + reply.size());
        if (j.is_object() && j.contains("error")) {
            return {false, {}, j["error"].get<std::string>()};
        }
        return {true, j, {}};
    } catch (const std::exception& e) {
        return {false, {}, std::string{"parse failed: "} + e.what()};
    }
}

}  // namespace

nlohmann::json McpServer::tool_system_info(const nlohmann::json&) {
    const auto r = call_agent(rpc_, "system_info");
    return r.ok ? json_result(r.body) : error_result(r.error);
}

nlohmann::json McpServer::tool_list_apps(const nlohmann::json&) {
    const auto r = call_agent(rpc_, "list_apps");
    if (!r.ok) return error_result(r.error);
    // Strip icon_png_base64 — pointless noise for an LLM.
    nlohmann::json out = r.body;
    if (out.contains("apps") && out["apps"].is_array()) {
        for (auto& a : out["apps"]) {
            if (a.is_object()) a.erase("icon_png_base64");
        }
    }
    return json_result(out);
}

nlohmann::json McpServer::tool_list_windows(const nlohmann::json&) {
    const auto r = call_agent(rpc_, "list_windows");
    return r.ok ? json_result(r.body) : error_result(r.error);
}

nlohmann::json McpServer::tool_launch(const nlohmann::json& args) {
    const auto app_id = args.value("app_id", std::string{});
    if (app_id.empty()) return error_result("launch: missing app_id");
    const auto r = call_agent(rpc_, "launch", {{"app_id", app_id}});
    return r.ok ? json_result(r.body) : error_result(r.error);
}

nlohmann::json McpServer::tool_console_open(const nlohmann::json&) {
    const auto r = call_agent(rpc_, "console_open");
    if (!r.ok) return error_result(r.error);
    const auto sid = r.body.value("session_id", std::string{});
    if (sid.empty()) return error_result("agent returned no session_id");
    return text_result("session_id=" + sid);
}

nlohmann::json McpServer::tool_console_input(const nlohmann::json& args) {
    const auto sid = args.value("session_id", std::string{});
    const auto data = args.value("data", std::string{});
    if (sid.empty()) return error_result("console_input: missing session_id");
    const auto r = call_agent(rpc_, "console_input",
                                {{"session_id", sid}, {"data", data}});
    return r.ok ? text_result("ok") : error_result(r.error);
}

nlohmann::json McpServer::tool_console_read(const nlohmann::json& args) {
    const auto sid = QString::fromStdString(args.value("session_id", std::string{}));
    if (sid.isEmpty()) return error_result("console_read: missing session_id");
    QByteArray data;
    {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        const auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            data = it.value();
            it.value().clear();
        }
    }
    return text_result(QString::fromUtf8(data).toStdString());
}

nlohmann::json McpServer::tool_console_close(const nlohmann::json& args) {
    const auto sid = args.value("session_id", std::string{});
    if (sid.empty()) return error_result("console_close: missing session_id");
    const auto r = call_agent(rpc_, "console_close", {{"session_id", sid}});
    {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_.remove(QString::fromStdString(sid));
    }
    return r.ok ? text_result("ok") : error_result(r.error);
}

nlohmann::json McpServer::tool_recent_logs(const nlohmann::json& args) {
    const qsizetype count = args.value("count", 100);
    const auto& records = log_store_.records();
    const qsizetype start = (records.size() > count) ? (records.size() - count) : 0;
    QString out;
    for (qsizetype i = start; i < records.size(); ++i) {
        const auto& r = records[i];
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(r.ts_ms)).toLocalTime();
        out += QStringLiteral("[%1 %2] %3\n")
                   .arg(dt.toString(QStringLiteral("HH:mm:ss.zzz")),
                        r.level.leftJustified(5, QLatin1Char(' ')),
                        r.msg);
    }
    return text_result(out.toStdString());
}

nlohmann::json McpServer::tool_rpc(const nlohmann::json& args) {
    const auto cmd = args.value("cmd", std::string{});
    if (cmd.empty()) return error_result("rpc: missing cmd");
    const auto params = (args.contains("params") && args["params"].is_object())
                            ? args["params"]
                            : nlohmann::json::object();
    const auto r = call_agent(rpc_, cmd, params);
    return r.ok ? json_result(r.body) : error_result(r.error);
}

// Hot-swap the running guest agent with the locally-built
// ``lsw-guest-agent.exe``. The flow:
//   1. Locate the .exe (env LSW_AGENT_EXE, then build-tree default).
//   2. Sample the running agent's ``agent_version`` so we have a
//      baseline to compare against after the swap.
//   3. Spin up a one-shot HTTP server on a free localhost port that
//      serves the .exe bytes. The guest reaches it at ``10.0.2.2:<port>``
//      via QEMU user-mode networking.
//   4. Fire a ``run_script`` RPC at the agent with a batch that:
//      curls the new binary, kills the running agent, moves the file
//      into place, and relaunches via ``start ""``. The script runs
//      in a detached cmd.exe so killing the agent doesn't kill it.
//   5. Poll ``system_info`` for ~30 s; success when ``agent_version``
//      differs from the baseline (means the new binary is what's
//      answering).
//   6. Tear down the HTTP server and report.
nlohmann::json McpServer::tool_redeploy_agent(const nlohmann::json& /*args*/) {
    const QString exe_path = locate_agent_exe();
    if (exe_path.isEmpty()) {
        return error_result("redeploy_agent: cannot find lsw-guest-agent.exe — "
                             "set $LSW_AGENT_EXE or run scripts/build-agent.sh");
    }
    QFile f{exe_path};
    if (!f.open(QIODevice::ReadOnly)) {
        return error_result("redeploy_agent: cannot open "
                             + exe_path.toStdString() + ": "
                             + f.errorString().toStdString());
    }
    const QByteArray exe_bytes = f.readAll();
    f.close();
    if (exe_bytes.isEmpty()) {
        return error_result("redeploy_agent: " + exe_path.toStdString()
                             + " is empty");
    }

    // Baseline ``agent_version``. Empty if the running agent predates
    // this field — any non-empty post-swap value counts as a change.
    const auto pre = call_agent(rpc_, "system_info");
    if (!pre.ok) {
        return error_result("redeploy_agent: agent unreachable before swap: "
                             + pre.error);
    }
    const std::string prev_version =
        pre.body.value("agent_version", std::string{});

    // Bind the deploy listener on 127.0.0.1:0 (kernel picks port).
    const int srv = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (srv < 0) {
        return error_result(std::string{"redeploy_agent: socket: "}
                             + std::strerror(errno));
    }
    int one = 1;
    ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string err = std::strerror(errno);
        ::close(srv);
        return error_result("redeploy_agent: bind: " + err);
    }
    socklen_t alen = sizeof(addr);
    if (::getsockname(srv, reinterpret_cast<sockaddr*>(&addr), &alen) != 0
        || ::listen(srv, 4) != 0) {
        ::close(srv);
        return error_result("redeploy_agent: listen failed");
    }
    const unsigned short port = ntohs(addr.sin_port);

    // Serve loop — accepts repeatedly until ``stop`` flips. The guest
    // may retry the download on a transient failure, so we keep it
    // alive for the duration of the polling window rather than
    // shutting after one shot.
    std::atomic<bool> stop{false};
    std::atomic<int>  served{0};
    std::thread server_thread([srv, &stop, &served, &exe_bytes]() {
        while (!stop.load()) {
            sockaddr_in peer{};
            socklen_t pl = sizeof(peer);
            const int c = ::accept(srv, reinterpret_cast<sockaddr*>(&peer), &pl);
            if (c < 0) {
                if (stop.load()) return;
                continue;
            }
            // Drain request bytes (we don't validate them — the body
            // we're going to send is fixed and the URL we advertise
            // points at exactly one resource). Stop once we've seen
            // the headers-terminating CRLFCRLF or the peer pauses.
            std::string req_buf;
            req_buf.reserve(512);
            char buf[4096];
            for (int i = 0; i < 8; ++i) {
                timeval tv{0, 100 * 1000};  // 100 ms each
                fd_set rd;
                FD_ZERO(&rd);
                FD_SET(c, &rd);
                if (::select(c + 1, &rd, nullptr, nullptr, &tv) <= 0) break;
                const ssize_t n = ::recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req_buf.append(buf, static_cast<size_t>(n));
                if (req_buf.find("\r\n\r\n") != std::string::npos) break;
            }
            const std::string hdr = std::string{"HTTP/1.1 200 OK\r\n"}
                + "Content-Type: application/octet-stream\r\n"
                + "Content-Length: " + std::to_string(exe_bytes.size()) + "\r\n"
                + "Connection: close\r\n\r\n";
            (void)::send(c, hdr.data(), hdr.size(), MSG_NOSIGNAL);
            const char* p = exe_bytes.constData();
            qsizetype left = exe_bytes.size();
            while (left > 0) {
                const ssize_t w = ::send(c, p, static_cast<size_t>(left),
                                          MSG_NOSIGNAL);
                if (w <= 0) break;
                p += w;
                left -= w;
            }
            ::close(c);
            ++served;
        }
    });

    // Swap script. ``start ""`` detaches the new agent so it survives
    // cmd.exe exiting; the agent's run_script_job thread dies with
    // the agent process, but the cmd.exe was launched with
    // bInheritHandles=FALSE so it has no parent-dependent state.
    static constexpr const char* kSwapScript = R"BAT(@echo off
setlocal
set NEW=%TEMP%\lsw-agent-new.exe
set LOG=%TEMP%\lsw-swap.log
> "%LOG%" echo [swap] start %DATE% %TIME%
curl --fail -L -o "%NEW%" http://10.0.2.2:__PORT__/lsw-guest-agent.exe >> "%LOG%" 2>&1
if errorlevel 1 ( echo [swap] download failed >> "%LOG%" & exit /b 1 )
for %%I in ("%NEW%") do echo [swap] downloaded size=%%~zI >> "%LOG%"
timeout /t 1 /nobreak >nul
echo [swap] killing agent... >> "%LOG%"
taskkill /F /IM lsw-agent.exe >> "%LOG%" 2>&1
timeout /t 3 /nobreak >nul
echo [swap] moving binary >> "%LOG%"
move /Y "%NEW%" "C:\ProgramData\LSW\lsw-agent.exe" >> "%LOG%" 2>&1
echo [swap] relaunching >> "%LOG%"
rem No scheduled task anymore — the agent autostarts from HKLM\Run
rem as the interactive user. The swap itself runs IN the agent
rem (i.e. as that same in-session user), so a plain `start ""`
rem relaunches it in the right context. It's detached, so it
rem survives this cmd / the just-killed agent.
start "" "C:\ProgramData\LSW\lsw-agent.exe"
echo [swap] done >> "%LOG%"
exit /b 0
)BAT";
    std::string script = kSwapScript;
    {
        const std::string needle{"__PORT__"};
        const auto pos = script.find(needle);
        if (pos != std::string::npos) {
            script.replace(pos, needle.size(), std::to_string(port));
        }
    }

    // The agent runs as the elevated-admin wizard user; the swap
    // runs in that context (it can taskkill its own process and the
    // relaunch is via the scheduled task, which re-mints the proper
    // elevated user token).
    const auto ack = call_agent(rpc_, "run_script", {
        {"name", "redeploy agent"},
        {"script", script},
    });
    auto cleanup_server = [&]() {
        stop.store(true);
        ::shutdown(srv, SHUT_RDWR);
        ::close(srv);
        if (server_thread.joinable()) server_thread.join();
    };
    if (!ack.ok) {
        cleanup_server();
        return error_result("redeploy_agent: run_script RPC failed: "
                             + ack.error);
    }

    // Poll for the new binary. AgentRpcClient reconnects with a 500 ms
    // backoff after the script kills the old agent, and the new agent
    // typically listens within 1-2 s of relaunch.
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::seconds(45);
    std::string new_version;
    bool ok = false;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const auto chk = call_agent(rpc_, "system_info");
        if (!chk.ok) continue;
        const auto v = chk.body.value("agent_version", std::string{});
        if (!v.empty() && v != prev_version) {
            new_version = v;
            ok = true;
            break;
        }
    }

    cleanup_server();

    if (!ok) {
        return error_result(
            "redeploy_agent: timed out (45 s) waiting for new agent. "
            "Served " + std::to_string(served.load())
            + " download(s) on port " + std::to_string(port)
            + ", prev_version='" + prev_version
            + "'. Check %TEMP%\\lsw-swap.log on the guest for details.");
    }

    return text_result(
        "redeploy_agent: success — agent_version '" + prev_version + "' -> '"
        + new_version + "' (" + std::to_string(served.load())
        + " download(s) served on port " + std::to_string(port) + ")");
}

// ---- HTTP response writer -------------------------------------------------

void McpServer::send_http_response(QTcpSocket* sock, int status,
                                     const std::string& body) {
    if (sock == nullptr) return;
    const char* reason = (status == 200) ? "OK"
                       : (status == 400) ? "Bad Request"
                       : (status == 404) ? "Not Found"
                       : (status == 405) ? "Method Not Allowed"
                       : "Error";
    QByteArray resp;
    resp.append("HTTP/1.1 ");
    resp.append(QByteArray::number(status));
    resp.append(' ');
    resp.append(reason);
    resp.append("\r\n");
    resp.append("Content-Type: application/json\r\n");
    resp.append("Content-Length: ");
    resp.append(QByteArray::number(static_cast<qsizetype>(body.size())));
    resp.append("\r\n");
    resp.append("Connection: close\r\n");
    resp.append("Access-Control-Allow-Origin: *\r\n");
    resp.append("\r\n");
    resp.append(body.data(), static_cast<qsizetype>(body.size()));
    sock->write(resp);
    sock->flush();
    sock->disconnectFromHost();
}

}  // namespace lsw::manager::mcp
