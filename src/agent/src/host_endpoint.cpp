#include "lsw/agent/host_endpoint.hpp"

#include "lsw/agent/app_catalog.hpp"
#include "lsw/agent/capture_setup.hpp"
#include "lsw/agent/console.hpp"
#include "lsw/agent/gdi_util.hpp"
#include "lsw/agent/input_inject.hpp"
#include "lsw/agent/installer.hpp"
#include "lsw/agent/log.hpp"
#include "lsw/agent/wgc_capture.hpp"
#include "lsw/agent/process_launcher.hpp"
#include "lsw/agent/system_info.hpp"
#include "lsw/agent/window_capture.hpp"

// clang-format off
// Header order matters: <winsock2.h> before <windows.h>.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on

#include <chrono>
#include <cstdint>
#include <cstring>
#include <nlohmann/json.hpp>
#include <unordered_set>
#include <vector>

// =============================================================================
// Wire protocol — type-tagged, length-prefixed frames.
//
//   [type:u8][len:u32 big-endian][payload:len bytes]
//
// Three frame types share the same single TCP connection:
//
//   type=1 CONTROL    payload = JSON object (UTF-8, no trailing newline)
//                     Carries the request / reply / event message:
//                       Request  {"id": N, "cmd": "...", "params": {...}}
//                       Reply    {"id": N, "result": {...}}
//                                {"id": N, "error":  "..."}
//                       Event    {"event": "<topic>", ...payload}
//                     (Events: ``log``, ``setup_progress``, ``setup_done``.)
//
//   type=2 FRAME      payload = [hwnd:u64-BE][png bytes:rest]
//                     Window-mirror frame. ``png bytes`` is the raw PNG
//                     output from PrintWindow — no base64, no JSON
//                     wrapper. Pushed by the agent on each capture tick
//                     for every HWND the manager has subscribed to via
//                     the ``subscribe_window`` RPC.
//
//   type=3 FRAME_GONE payload = [hwnd:u64-BE]
//                     Window vanished mid-stream. The manager drops
//                     the mirror.
//
// Mixing binary frames into the same connection as JSON RPCs would be
// fragile with newline-delimited framing (PNG bytes contain newlines).
// The length-prefixed envelope means the receiver always knows exactly
// how many bytes are coming next — no escaping, no scanning.
// =============================================================================

namespace lsw::agent {

namespace {

// TCP port the agent listens on. The host (Linux side) reaches this
// via QEMU user-mode networking's ``hostfwd`` rule. (A real AF_VSOCK
// transport was attempted and reverted — host kernels in this class
// have no working vhost_vsock, so TCP-over-hostfwd is the reliable
// channel.)
constexpr unsigned short kAgentTcpPort = 5102;

// Capture poll tick. Caps latency on agent-initiated pushes (log,
// installer, frames) and acts as the frame rate cap for the window
// mirror (~30 Hz).
constexpr int kTickMs = 33;

constexpr std::uint8_t kFrameTypeControl = 1;
constexpr std::uint8_t kFrameTypeFrame = 2;
constexpr std::uint8_t kFrameTypeFrameGone = 3;

constexpr size_t kFrameHeaderSize = 1 + 4;  // type + length
constexpr size_t kMaxLogPayload = 200;

[[nodiscard]] std::string truncate_for_log(const std::string& s) {
    if (s.size() <= kMaxLogPayload) {
        return s;
    }
    return s.substr(0, kMaxLogPayload) + "...(+"
           + std::to_string(s.size() - kMaxLogPayload) + " more bytes)";
}

[[nodiscard]] std::string peer_address(SOCKET s) {
    sockaddr_in peer{};
    int len = sizeof(peer);
    if (::getpeername(s, reinterpret_cast<sockaddr*>(&peer), &len) != 0) {
        return "?";
    }
    char ip[64] = {};
    ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    return std::string{ip} + ":" + std::to_string(ntohs(peer.sin_port));
}

[[nodiscard]] bool is_silent_cmd(const std::string& cmd) {
    // inject_input fires at mouse-move frequency; logging a REQ/REP
    // per event would churn the bounded log ring into uselessness
    // during any interaction. The injector itself logs only failures.
    // list_windows now runs at the watcher's ~50 Hz poll; inject_input
    // at mouse-move rate. Logging REQ/REP for either floods the ring.
    return cmd == "inject_input" || cmd == "list_windows";
}

// Big-endian integer encode/decode helpers — wire format is BE
// because that's still the network-byte-order convention and it
// makes Wireshark dumps readable.
void write_u32_be(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<std::uint8_t>(v & 0xFFu);
}
std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)
         | static_cast<std::uint32_t>(p[3]);
}
void write_u64_be(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (56 - 8 * i)) & 0xFFu);
    }
}
// read_u64_be is defined symmetrically with write_u64_be but
// currently unused on the agent side (the agent only WRITES hwnds in
// frame envelopes; the manager READS them). Kept here so the helpers
// stay paired — silence the unused-function warning with [[maybe_unused]].
[[maybe_unused]] std::uint64_t read_u64_be(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return v;
}

[[nodiscard]] std::string summarize_reply(const std::string& cmd,
                                            const nlohmann::json& result) {
    const auto dump = result.dump();
    if (cmd == "list_apps" && result.contains("apps") && result["apps"].is_array()) {
        return "list_apps: " + std::to_string(result["apps"].size())
               + " apps, " + std::to_string(dump.size()) + "B json";
    }
    if (cmd == "list_windows" && result.contains("windows") && result["windows"].is_array()) {
        return "list_windows: " + std::to_string(result["windows"].size()) + " windows";
    }
    if (cmd == "get_icon") {
        const auto b64 = result.value("icon_png_base64", std::string{});
        return "get_icon: png_b64_len=" + std::to_string(b64.size())
               + (b64.empty() ? " (FAILED)" : "");
    }
    if (cmd == "launch") {
        const auto ok = result.value("ok", false);
        const auto pid = result.value("pid", std::uint64_t{0});
        const auto err = result.value("error", std::string{});
        return "launch: ok=" + std::string{ok ? "true" : "false"}
               + " pid=" + std::to_string(pid)
               + (err.empty() ? "" : " err=" + err);
    }
    return cmd + ": " + std::to_string(dump.size()) + "B json";
}

// Per-cmd dispatcher. Throws on application-level errors (caught at
// the connection level → converted into a JSON ``error`` reply).
nlohmann::json dispatch(const std::string& cmd, const nlohmann::json& params,
                         std::unordered_set<std::uint64_t>& subscribed_windows) {
    if (cmd == "system_info") return nlohmann::json::parse(SystemInfo::collect_json());
    if (cmd == "list_apps")    return nlohmann::json::parse(AppCatalog::list_json());
    if (cmd == "launch") {
        const auto app_id = params.value("app_id", std::string{});
        unsigned long pid = 0;
        std::string err;
        const bool ok = ProcessLauncher::launch(app_id, &pid, &err);
        nlohmann::json r = {{"ok", ok}, {"pid", static_cast<std::uint64_t>(pid)}};
        if (!ok && !err.empty()) r["error"] = err;
        return r;
    }
    if (cmd == "list_windows") {
        const auto wins = WindowCapture::enumerate();
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& w : wins) {
            arr.push_back({
                {"hwnd", static_cast<std::uint64_t>(w.hwnd)},
                {"owner", static_cast<std::uint64_t>(w.owner)},
                {"pid", static_cast<std::uint64_t>(w.pid)},
                {"title", w.title},
                {"exe_path", w.exe_path},
                {"client_w", w.client_w},
                {"client_h", w.client_h},
                {"screen_x", w.screen_x},
                {"screen_y", w.screen_y},
            });
        }
        return {{"windows", std::move(arr)}};
    }
    if (cmd == "close_window") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        return {{"ok", hwnd != 0
                         && WindowCapture::close_window(static_cast<std::uintptr_t>(hwnd))}};
    }
    if (cmd == "focus_window") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        return {{"ok", hwnd != 0
                         && WindowCapture::focus_window(static_cast<std::uintptr_t>(hwnd))}};
    }
    if (cmd == "unfocus_window") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        return {{"ok", hwnd != 0
                         && WindowCapture::unfocus_window(static_cast<std::uintptr_t>(hwnd))}};
    }
    if (cmd == "inject_input") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) throw std::invalid_argument("inject_input: missing hwnd");
        const auto type = params.value("type", std::string{});
        InputEvent ev;
        if (type == "move")          ev.kind = InputEvent::Kind::MouseMove;
        else if (type == "down")     ev.kind = InputEvent::Kind::MouseDown;
        else if (type == "up")       ev.kind = InputEvent::Kind::MouseUp;
        else if (type == "dblclick") ev.kind = InputEvent::Kind::MouseDblClk;
        else if (type == "wheel")    ev.kind = InputEvent::Kind::Wheel;
        else if (type == "key_down") ev.kind = InputEvent::Kind::KeyDown;
        else if (type == "key_up")   ev.kind = InputEvent::Kind::KeyUp;
        else if (type == "char")     ev.kind = InputEvent::Kind::Char;
        else throw std::invalid_argument("inject_input: bad type '" + type + "'");
        ev.x = params.value("x", 0);
        ev.y = params.value("y", 0);
        ev.button = params.value("button", 0);
        ev.wheel = params.value("wheel", 0);
        ev.hwheel = params.value("hwheel", false);
        ev.vk = params.value("vk", 0u);
        ev.scan = params.value("scan", 0u);
        ev.codepoint = params.value("cp", 0u);
        ev.mods = params.value("mods", 0u);
        return {{"ok", InputInjector::inject(static_cast<std::uintptr_t>(hwnd), ev)}};
    }
    if (cmd == "resize_window") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        const auto w = params.value("width", 0);
        const auto h = params.value("height", 0);
        return {{"ok", hwnd != 0
                         && WindowCapture::resize_client(
                                static_cast<std::uintptr_t>(hwnd), w, h)}};
    }
    if (cmd == "nchittest") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) throw std::invalid_argument("nchittest: missing hwnd");
        const int x = params.value("x", 0);
        const int y = params.value("y", 0);
        const auto r = WindowCapture::hit_test_nc(
            static_cast<std::uintptr_t>(hwnd), x, y);
        return {{"ht", r.ht},
                {"efb_l", r.efb_l}, {"efb_t", r.efb_t},
                {"efb_r", r.efb_r}, {"efb_b", r.efb_b},
                {"win_l", r.win_l}, {"win_t", r.win_t},
                {"win_r", r.win_r}, {"win_b", r.win_b},
                {"sx", r.sx}, {"sy", r.sy},
                {"dpi", r.dpi}};
    }
    if (cmd == "window_props") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) throw std::invalid_argument("window_props: missing hwnd");
        const auto p = WindowCapture::query_props(
            static_cast<std::uintptr_t>(hwnd));
        return {
            {"valid", p.valid},
            {"x", p.x}, {"y", p.y}, {"w", p.w}, {"h", p.h},
            {"resizable", p.resizable},
            {"minimizable", p.minimizable},
            {"maximizable", p.maximizable},
            {"closable", p.closable},
            {"min_w", p.min_w}, {"min_h", p.min_h},
            {"max_w", p.max_w}, {"max_h", p.max_h},
            {"screen_w", p.screen_w}, {"screen_h", p.screen_h},
            {"btn_min",   {p.btn_min_x,   p.btn_min_y,
                            p.btn_min_w,   p.btn_min_h}},
            {"btn_max",   {p.btn_max_x,   p.btn_max_y,
                            p.btn_max_w,   p.btn_max_h}},
            {"btn_close", {p.btn_close_x, p.btn_close_y,
                            p.btn_close_w, p.btn_close_h}},
            {"btn_help",  {p.btn_help_x,  p.btn_help_y,
                            p.btn_help_w,  p.btn_help_h}},
        };
    }
    if (cmd == "capture_debug") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        nlohmann::json r;
        r["wgc_available"] = WgcCapture::available();
        r["pw"] = nlohmann::json::parse(WindowCapture::capture_debug_json(
            static_cast<std::uintptr_t>(hwnd)));
        r["wgc"] = nlohmann::json::parse(
            WgcCapture::debug_json(static_cast<std::uintptr_t>(hwnd)));
        return r;
    }
    if (cmd == "subscribe_window") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        if (hwnd == 0) throw std::invalid_argument("subscribe_window: missing hwnd");
        subscribed_windows.insert(hwnd);
        return {{"ok", true}};
    }
    if (cmd == "unsubscribe_window") {
        const auto hwnd = params.value("hwnd", std::uint64_t{0});
        subscribed_windows.erase(hwnd);
        WgcCapture::release(static_cast<std::uintptr_t>(hwnd));
        return {{"ok", true}};
    }
    if (cmd == "set_capture_method") {
        const auto path = params.value("path", std::string{});
        const auto method = params.value("method", std::string{});
        if (path.empty() || method.empty()) {
            throw std::invalid_argument("set_capture_method: missing path or method");
        }
        const bool ok = (method == "vulkan") ? CaptureSetup::enable_vulkan_for(path)
                                              : CaptureSetup::disable_vulkan_for(path);
        return {{"ok", ok}};
    }
    if (cmd == "get_icon") {
        const auto path = params.value("path", std::string{});
        if (path.empty()) throw std::invalid_argument("get_icon: missing path");
        const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        std::wstring wpath(static_cast<size_t>(wlen > 0 ? wlen - 1 : 0), L'\0');
        if (wlen > 0) {
            ::MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlen);
        }
        return {{"icon_png_base64", gdi::extract_icon_png_base64(wpath)}};
    }
    if (cmd == "download_install") {
        const auto url = params.value("url", std::string{});
        if (url.empty()) throw std::invalid_argument("download_install: missing url");
        return {{"ok", true},
                {"job_id", Installer::instance().start_download_install(
                                params.value("name", std::string{}), url,
                                params.value("args", std::string{}))}};
    }
    if (cmd == "download_extract") {
        const auto url = params.value("url", std::string{});
        if (url.empty()) throw std::invalid_argument("download_extract: missing url");
        return {{"ok", true},
                {"job_id", Installer::instance().start_download_extract(
                                params.value("name", std::string{}), url,
                                params.value("extract_to", std::string{}),
                                params.value("script", std::string{}))}};
    }
    if (cmd == "run_script") {
        const auto script = params.value("script", std::string{});
        if (script.empty()) throw std::invalid_argument("run_script: missing script");
        return {{"ok", true},
                {"job_id", Installer::instance().start_run_script(
                                params.value("name", std::string{}), script)}};
    }
    if (cmd == "console_open") {
        const auto sid = Console::instance().open();
        if (sid.empty()) {
            throw std::runtime_error("console_open: failed to spawn cmd.exe");
        }
        return {{"ok", true}, {"session_id", sid}};
    }
    if (cmd == "console_input") {
        const auto sid = params.value("session_id", std::string{});
        const auto data = params.value("data", std::string{});
        if (sid.empty()) throw std::invalid_argument("console_input: missing session_id");
        const bool ok = Console::instance().write(sid, data);
        return {{"ok", ok}};
    }
    if (cmd == "console_close") {
        const auto sid = params.value("session_id", std::string{});
        if (!sid.empty()) {
            Console::instance().close(sid);
        }
        return {{"ok", true}};
    }
    throw std::invalid_argument("unknown command: " + cmd);
}

}  // namespace

HostEndpoint::HostEndpoint() {
    WSADATA wsa{};
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
}

HostEndpoint::~HostEndpoint() {
    if (listen_fd_ >= 0) {
        ::closesocket(static_cast<SOCKET>(listen_fd_));
    }
    ::WSACleanup();
}

bool HostEndpoint::open_listen() {
    SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        LSW_AGENT_ERROR(std::string{"host_endpoint: socket() failed wsa_err="}
                        + std::to_string(::WSAGetLastError()));
        return false;
    }
    BOOL reuse = TRUE;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kAgentTcpPort);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        LSW_AGENT_ERROR(std::string{"host_endpoint: bind(:5102) failed wsa_err="}
                        + std::to_string(::WSAGetLastError()));
        ::closesocket(s);
        return false;
    }
    if (::listen(s, 4) != 0) {
        LSW_AGENT_ERROR(std::string{"host_endpoint: listen failed wsa_err="}
                        + std::to_string(::WSAGetLastError()));
        ::closesocket(s);
        return false;
    }
    listen_fd_ = static_cast<intptr_t>(s);
    LSW_AGENT_INFO("host_endpoint: listening on :5102");
    return true;
}

void HostEndpoint::run(std::atomic<bool>& stop_flag) {
    if (!open_listen()) return;
    while (!stop_flag.load()) {
        sockaddr_in peer{};
        int peer_len = sizeof(peer);
        SOCKET c = ::accept(static_cast<SOCKET>(listen_fd_),
                             reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (c == INVALID_SOCKET) {
            if (stop_flag.load()) break;
            LSW_AGENT_WARN(std::string{"host_endpoint: accept failed wsa_err="}
                           + std::to_string(::WSAGetLastError()));
            continue;
        }
        char ip[64] = {};
        ::inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
        const std::string peerdesc = std::string{ip} + ":" + std::to_string(ntohs(peer.sin_port));
        LSW_AGENT_INFO(std::string{"host_endpoint: connection accepted from "} + peerdesc);
        handle_connection(static_cast<intptr_t>(c));
        ::closesocket(c);
        LSW_AGENT_INFO(std::string{"host_endpoint: connection closed (peer "} + peerdesc + ")");
    }
}

void HostEndpoint::handle_connection(intptr_t accepted_fd) {
    auto s = static_cast<SOCKET>(accepted_fd);
    const std::string peer = peer_address(s);
    std::vector<std::uint8_t> rxbuf;
    rxbuf.reserve(64 * 1024);
    std::uint64_t total_recv = 0;
    std::uint64_t total_sent = 0;
    std::uint64_t requests_handled = 0;
    std::uint64_t events_sent = 0;
    std::uint64_t frames_sent = 0;
    std::uint64_t last_log_seq = 0;
    std::unordered_set<std::uint64_t> subscribed_windows;

    // Send a single envelope frame. Header is written into a small
    // stack buffer, then payload follows.
    auto send_envelope = [&](std::uint8_t type, const std::uint8_t* payload,
                              std::size_t payload_len) -> bool {
        if (payload_len > std::numeric_limits<std::uint32_t>::max()) {
            LSW_AGENT_ERROR("host_endpoint: payload exceeds u32 length cap");
            return false;
        }
        std::uint8_t header[kFrameHeaderSize];
        header[0] = type;
        write_u32_be(header + 1, static_cast<std::uint32_t>(payload_len));
        // Two send()s — TCP coalesces, and we avoid copying a big
        // payload just to bolt a 5-byte header in front.
        auto write_all = [&](const std::uint8_t* p, int n) -> bool {
            while (n > 0) {
                const int w = ::send(s, reinterpret_cast<const char*>(p), n, 0);
                if (w < 0) {
                    const int err = ::WSAGetLastError();
                    if (err == 10053 || err == 10054) {
                        LSW_AGENT_DEBUG(std::string{"host_endpoint: send to "} + peer
                                        + " refused (peer closed) wsa_err="
                                        + std::to_string(err));
                    } else {
                        LSW_AGENT_WARN(std::string{"host_endpoint: send to "} + peer
                                       + " failed wsa_err=" + std::to_string(err));
                    }
                    return false;
                }
                p += w;
                n -= w;
                total_sent += static_cast<std::uint64_t>(w);
            }
            return true;
        };
        if (!write_all(header, kFrameHeaderSize)) return false;
        if (payload_len > 0 && !write_all(payload, static_cast<int>(payload_len))) {
            return false;
        }
        return true;
    };

    auto send_control = [&](const nlohmann::json& j) -> bool {
        const std::string body = j.dump();
        return send_envelope(kFrameTypeControl,
                              reinterpret_cast<const std::uint8_t*>(body.data()),
                              body.size());
    };

    // ---- Event drains ------------------------------------------------------

    auto push_pending_logs = [&]() -> bool {
        std::uint64_t next_since = last_log_seq;
        const auto recs = log::collect_since(last_log_seq, next_since);
        for (const auto& r : recs) {
            nlohmann::json msg = {
                {"event", "log"},
                {"seq", r.seq},
                {"ts_ms", r.ts_ms},
                {"level", log::level_name(r.level)},
                {"msg", r.msg},
            };
            if (!send_control(msg)) return false;
            ++events_sent;
        }
        last_log_seq = next_since;
        return true;
    };

    auto push_pending_console = [&]() -> bool {
        const auto notes = Console::instance().collect_pending();
        for (const auto& n : notes) {
            nlohmann::json msg;
            msg["event"] = (n.kind == Console::Kind::Output) ? "console_output"
                                                              : "console_closed";
            msg["session_id"] = n.session_id;
            if (n.kind == Console::Kind::Output) {
                // Base64 the bytes so they survive JSON. Console
                // output is typically a few KB/s — small enough that
                // we don't need to allocate a fresh binary envelope
                // type for it; events stay JSON.
                msg["data_b64"] = gdi::base64_encode(n.data);
            } else {
                msg["exit_code"] = n.exit_code;
            }
            if (!send_control(msg)) return false;
            ++events_sent;
        }
        return true;
    };

    auto push_pending_installer = [&]() -> bool {
        auto notes = Installer::instance().collect_pending();
        for (size_t i = 0; i < notes.size(); ++i) {
            const auto& n = notes[i];
            nlohmann::json msg = {
                {"event", (n.kind == Installer::Kind::Done) ? "setup_done" : "setup_progress"},
                {"job_id", n.job_id},
                {"name", n.name},
                {"phase", n.phase},
                {"bytes", n.bytes},
                {"total_bytes", n.total_bytes},
            };
            if (n.kind == Installer::Kind::Done) {
                msg["ok"] = n.ok;
                msg["exit_code"] = n.exit_code;
            }
            if (!n.message.empty()) msg["message"] = n.message;
            if (!send_control(msg)) {
                // Connection died mid-drain (classically: guest-tools
                // just reset the NetKVM NIC this very channel rides
                // on). Put the undelivered notes — crucially any
                // ``setup_done`` — back so the next connection
                // redelivers them instead of stranding the wizard.
                notes.erase(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(i));
                Installer::instance().requeue_front(std::move(notes));
                return false;
            }
            ++events_sent;
        }
        return true;
    };

    auto push_pending_frames = [&]() -> bool {
        for (auto it = subscribed_windows.begin(); it != subscribed_windows.end(); ) {
            const auto hwnd = *it;
            const auto png = WindowCapture::capture_client_png(
                static_cast<std::uintptr_t>(hwnd));
            if (png.empty()) {
                // Window vanished — emit frame_gone, drop subscription.
                std::uint8_t payload[8];
                write_u64_be(payload, hwnd);
                if (!send_envelope(kFrameTypeFrameGone, payload, sizeof(payload))) {
                    return false;
                }
                LSW_AGENT_INFO(std::string{"host_endpoint: hwnd="}
                               + std::to_string(hwnd) + " reported gone");
                ++events_sent;
                it = subscribed_windows.erase(it);
                continue;
            }
            // Build [hwnd:u64][png bytes] payload in one buffer.
            std::vector<std::uint8_t> payload(8 + png.size());
            write_u64_be(payload.data(), hwnd);
            std::memcpy(payload.data() + 8, png.data(), png.size());
            if (!send_envelope(kFrameTypeFrame, payload.data(), payload.size())) {
                return false;
            }
            ++frames_sent;
            ++it;
        }
        return true;
    };

    // (Cursor reflection is now a per-window/per-point pull: the host
    // issues the ``cursor_at`` RPC on hover. A single global push
    // could only ever be right for one mirror at a time.)

    // ---- Request dispatcher ------------------------------------------------

    auto handle_request = [&](const nlohmann::json& req) -> bool {
        const auto t0 = std::chrono::steady_clock::now();
        const auto id = req.value("id", std::uint64_t{0});
        const auto cmd = req.value("cmd", std::string{});
        const auto params = (req.contains("params") && req["params"].is_object())
                              ? req["params"] : nlohmann::json::object();
        const bool silent = is_silent_cmd(cmd);
        if (!silent) {
            LSW_AGENT_INFO(std::string{"host_endpoint: REQ id="} + std::to_string(id)
                           + " cmd=" + cmd
                           + " line=" + truncate_for_log(req.dump()));
        }
        nlohmann::json reply;
        reply["id"] = id;
        try {
            reply["result"] = dispatch(cmd, params, subscribed_windows);
        } catch (const std::exception& e) {
            reply["error"] = e.what();
            LSW_AGENT_WARN(std::string{"host_endpoint: cmd="} + cmd
                           + " threw: " + e.what());
        }
        if (!silent) {
            const auto dt_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t0).count();
            const std::string summary = reply.contains("error")
                                          ? "error: " + reply["error"].get<std::string>()
                                          : summarize_reply(cmd, reply["result"]);
            LSW_AGENT_INFO(std::string{"host_endpoint: REP id="} + std::to_string(id)
                           + " cmd=" + cmd + " (" + std::to_string(dt_ms) + "ms) "
                           + summary);
        }
        return send_control(reply);
    };

    // ---- Per-frame demux ---------------------------------------------------

    auto handle_inbound = [&](std::uint8_t type, const std::uint8_t* payload,
                                std::size_t len) -> bool {
        if (type != kFrameTypeControl) {
            // Agent doesn't currently expect inbound frame/frame_gone.
            // Drop silently.
            return true;
        }
        nlohmann::json frame;
        try {
            frame = nlohmann::json::parse(payload, payload + len);
        } catch (const nlohmann::json::exception& e) {
            LSW_AGENT_WARN(std::string{"host_endpoint: malformed control JSON ("}
                           + e.what() + ") len=" + std::to_string(len));
            return true;
        }
        if (!frame.is_object()) return true;
        if (frame.contains("event")) {
            // Manager-pushed event — none defined; ignore.
            return true;
        }
        const bool has_id = frame.contains("id");
        const bool has_cmd = frame.contains("cmd") && frame["cmd"].is_string();
        if (has_id && has_cmd) {
            if (!handle_request(frame)) return false;
            ++requests_handled;
        }
        return true;
    };

    // ---- Main loop ---------------------------------------------------------

    while (true) {
        fd_set rd;
        FD_ZERO(&rd);
        FD_SET(s, &rd);
        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = kTickMs * 1000;
        const int sel = ::select(0, &rd, nullptr, nullptr, &tv);
        if (sel == SOCKET_ERROR) {
            LSW_AGENT_WARN(std::string{"host_endpoint: select error from "} + peer
                           + " wsa_err=" + std::to_string(::WSAGetLastError()));
            return;
        }

        if (sel > 0 && FD_ISSET(s, &rd)) {
            std::uint8_t chunk[16 * 1024];
            const int n = ::recv(s, reinterpret_cast<char*>(chunk), sizeof(chunk), 0);
            if (n <= 0) {
                if (n < 0) {
                    LSW_AGENT_WARN(std::string{"host_endpoint: recv error from "} + peer
                                   + " wsa_err=" + std::to_string(::WSAGetLastError())
                                   + " (after " + std::to_string(requests_handled)
                                   + " requests, " + std::to_string(events_sent)
                                   + " events, " + std::to_string(frames_sent)
                                   + " frames, " + std::to_string(total_recv)
                                   + "B in / " + std::to_string(total_sent) + "B out)");
                } else {
                    LSW_AGENT_DEBUG(std::string{"host_endpoint: peer "} + peer
                                    + " EOF (handled " + std::to_string(requests_handled)
                                    + " requests, " + std::to_string(events_sent)
                                    + " events, " + std::to_string(frames_sent)
                                    + " frames, " + std::to_string(total_recv)
                                    + "B in / " + std::to_string(total_sent) + "B out)");
                }
                return;
            }
            total_recv += static_cast<std::uint64_t>(n);
            rxbuf.insert(rxbuf.end(), chunk, chunk + n);

            // Drain as many complete envelopes as we have.
            std::size_t pos = 0;
            for (;;) {
                if (rxbuf.size() - pos < kFrameHeaderSize) break;
                const std::uint8_t type = rxbuf[pos];
                const std::uint32_t len = read_u32_be(rxbuf.data() + pos + 1);
                if (rxbuf.size() - pos < kFrameHeaderSize + len) break;
                if (!handle_inbound(type, rxbuf.data() + pos + kFrameHeaderSize, len)) {
                    return;
                }
                pos += kFrameHeaderSize + len;
            }
            if (pos > 0) {
                rxbuf.erase(rxbuf.begin(), rxbuf.begin() + static_cast<std::ptrdiff_t>(pos));
            }
        }

        if (!push_pending_logs()) return;
        if (!push_pending_installer()) return;
        if (!push_pending_console()) return;
        if (!push_pending_frames()) return;
    }
}

}  // namespace lsw::agent
