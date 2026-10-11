#include "lsw/host/services/guest_shells.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/services/status_hub.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QMetaObject>

#include <nlohmann/json.hpp>

namespace lsw::host::services {

namespace {

// Characters of transcript kept per shell. Past it the oldest lines go.
constexpr qsizetype kMaxTranscript = 1 << 20;

// cmd.exe writes CR-LF in the OEM code page. A view wants plain lines.
QString decode(const QByteArray& raw) {
    QString s = QString::fromLocal8Bit(raw);
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.remove(QChar('\r'));
    return s;
}

}  // namespace

GuestShells::GuestShells(link::AgentRpcClient& rpc, StatusHub& status, QObject* parent)
    : QObject(parent), rpc_(rpc), hub_(status), agent_online_(status.vm().agent_online()) {
    connect(&hub_, &StatusHub::changed, this, [this] {
        const bool online = hub_.vm().agent_online();
        if (online != agent_online_) {
            agent_online_ = online;
            on_agent(online);
        }
    });

    // The agent fires the same topics for every console it holds, the
    // MCP server's included; the slots keep to the ones opened here.
    output_sub_ = rpc_.subscribe(linkproto::topic::kConsoleOutput, [this](const nlohmann::json& f) {
        const auto id = QString::fromStdString(f.value("session_id", std::string{}));
        const auto bytes =
            QByteArray::fromBase64(QByteArray::fromStdString(f.value("data_b64", std::string{})));
        QMetaObject::invokeMethod(this, [this, id, bytes] { on_output(id, bytes); },
                                  Qt::QueuedConnection);
    });
    closed_sub_ = rpc_.subscribe(linkproto::topic::kConsoleClosed, [this](const nlohmann::json& f) {
        const auto id = QString::fromStdString(f.value("session_id", std::string{}));
        const int exit_code = f.value("exit_code", 0);
        QMetaObject::invokeMethod(this, [this, id, exit_code] { on_closed(id, exit_code); },
                                  Qt::QueuedConnection);
    });
}

GuestShells::~GuestShells() {
    output_sub_.reset();
    closed_sub_.reset();
    for (const Shell& shell : std::as_const(shells_)) {
        if (shell.state == State::Open) {
            rpc_.request_detached(linkproto::cmd::kConsoleClose,
                                  {{"session_id", shell.console_id.toStdString()}});
        }
    }
}

void GuestShells::open(int session) {
    if (!agent_online_) {
        return;
    }
    Shell& shell = shells_[session];
    if (shell.state == State::Open || shell.state == State::Opening) {
        return;
    }
    shell.state = State::Opening;
    shell.console_id.clear();
    shell.error.clear();
    shell.attempt = next_attempt_++;
    Q_EMIT changed(session);

    const std::uint64_t attempt = shell.attempt;
    rpc_.request_async(linkproto::cmd::kConsoleOpen, {{"session", session}}, this,
                       [this, session, attempt](const link::AgentReply& reply) {
        const auto id = reply.ok
                            ? QString::fromStdString(reply.body.value("session_id", std::string{}))
                            : QString{};
        const auto it = shells_.find(session);
        if (it == shells_.end() || it->attempt != attempt || it->state != State::Opening) {
            // Given up on while the reply was on its way: the agent went
            // away, or this was destroyed. The shell it opened is not wanted.
            if (!id.isEmpty()) {
                rpc_.request_detached(linkproto::cmd::kConsoleClose,
                                      {{"session_id", id.toStdString()}});
            }
            return;
        }
        Shell& opened = *it;
        if (id.isEmpty()) {
            opened.state = State::Failed;
            opened.error = reply.ok ? QStringLiteral("The agent gave no opened.")
                                   : QString::fromStdString(reply.error);
            opened.queued.clear();
            LSW_WARN("console: no opened in Windows session {}: {}", session,
                     opened.error.toStdString());
            Q_EMIT changed(session);
            return;
        }
        opened.state = State::Open;
        opened.console_id = id;
        opened.user = QString::fromStdString(reply.body.value("user", std::string{}));
        opened.elevated = reply.body.value("elevated", false);
        for (const QString& line : std::as_const(opened.queued)) {
            write(opened, line);
        }
        opened.queued.clear();
        Q_EMIT changed(session);
    });
}

void GuestShells::send(int session, const QString& line) {
    // Echoed into the transcript: cmd does not echo what arrives on a
    // redirected stdin, and the command belongs above its output.
    append(session, QStringLiteral("> ") + line + QChar('\n'));
    Shell& shell = shells_[session];
    if (shell.state == State::Open) {
        write(shell, line);
        return;
    }
    shell.queued.append(line);
    open(session);
}

void GuestShells::write(const Shell& shell, const QString& line) {
    rpc_.request_detached(linkproto::cmd::kConsoleInput,
                          {{"session_id", shell.console_id.toStdString()},
                           {"data", (line + QStringLiteral("\r\n")).toStdString()}});
}

void GuestShells::clear(int session) {
    const auto it = shells_.find(session);
    if (it == shells_.end()) {
        return;
    }
    it->transcript.clear();
    Q_EMIT changed(session);
}

const GuestShells::Shell* GuestShells::find(int session) const {
    const auto it = shells_.constFind(session);
    return it == shells_.constEnd() ? nullptr : &*it;
}

void GuestShells::on_agent(bool online) {
    if (online) {
        return;
    }
    // A shell is a cmd.exe the agent holds; with the agent gone, so is
    // the shell, whatever the transcript still shows.
    for (auto it = shells_.begin(); it != shells_.end(); ++it) {
        if (it->state != State::Open && it->state != State::Opening) {
            continue;
        }
        const bool was_open = it->state == State::Open;
        it->state = State::Lost;
        it->console_id.clear();
        it->queued.clear();
        if (was_open) {
            append(it.key(), QStringLiteral("\n[the agent stopped answering; this cmd.exe is gone]\n"));
        }
        Q_EMIT changed(it.key());
    }
}

std::optional<int> GuestShells::session_of(const QString& console_id) const {
    for (auto it = shells_.constBegin(); it != shells_.constEnd(); ++it) {
        if (it->state == State::Open && it->console_id == console_id) {
            return it.key();
        }
    }
    return std::nullopt;
}

void GuestShells::on_output(const QString& console_id, const QByteArray& bytes) {
    if (const auto session = session_of(console_id)) {
        append(*session, decode(bytes));
    }
}

void GuestShells::on_closed(const QString& console_id, int exit_code) {
    const auto session = session_of(console_id);
    if (!session) {
        return;
    }
    Shell& shell = shells_[*session];
    shell.state = State::Exited;
    shell.exit_code = exit_code;
    shell.console_id.clear();
    append(*session, QStringLiteral("\n[cmd.exe exited, code %1]\n").arg(exit_code));
    Q_EMIT changed(*session);
}

void GuestShells::append(int session, const QString& text) {
    QString& transcript = shells_[session].transcript;
    transcript += text;
    if (transcript.size() > kMaxTranscript) {
        const qsizetype cut = transcript.indexOf(QChar('\n'), transcript.size() - kMaxTranscript);
        transcript.remove(0, cut < 0 ? transcript.size() - kMaxTranscript : cut + 1);
    }
    Q_EMIT output(session, text);
}

}  // namespace lsw::host::services
