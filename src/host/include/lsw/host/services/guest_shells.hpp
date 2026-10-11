#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace lsw::host::services {

class StatusHub;

// The manager's guest shells: at most one ``cmd.exe`` per Windows
// session, a user's as that user or session 0 as SYSTEM, each with the
// transcript of everything it printed and everything typed into it.
//
// The shells belong to the manager, not to a page. The Console page and
// its pop-outs are views onto them: they pick a Windows session, show
// its transcript and send lines to it, and leaving a page does not end
// the shell behind it, so a ``cd`` holds until the shell exits or the
// agent goes away. A shell is opened the first time a view asks for it,
// and again after it has gone.
class GuestShells : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,    // nothing typed and nothing opened yet
        Opening,
        Open,
        Exited,  // the cmd.exe ended; ``exit_code`` says how
        Failed,  // the agent would not start one; ``error`` says why
        Lost,    // the agent stopped answering, and its shells with it
    };

    struct Shell {
        State state = State::Idle;
        QString console_id;  // the agent's id while Open
        QString user;
        bool elevated = false;
        int exit_code = 0;
        QString error;
        QString transcript;
        // Lines sent while it opens, written once it is open.
        QStringList queued;
        // Which open request is current, so a reply to an abandoned one
        // is told apart.
        std::uint64_t attempt = 0;
    };

    GuestShells(link::AgentRpcClient& rpc, StatusHub& status, QObject* parent = nullptr);
    // Ends every shell.
    ~GuestShells() override;

    // Start the shell for Windows session ``session`` (0 is SYSTEM)
    // unless it is open or opening. Nothing happens while the agent is
    // not answering.
    void open(int session);

    // Type ``line`` into that session's shell, opening one first if it
    // has none.
    void send(int session, const QString& line);

    // Empty the transcript. The shell keeps running.
    void clear(int session);

    // Null for a session no view has asked about.
    [[nodiscard]] const Shell* find(int session) const;

Q_SIGNALS:
    // ``text`` was appended to the session's transcript.
    void output(int session, const QString& text);
    // The session's shell changed state, or its transcript was cleared.
    void changed(int session);

private:
    void on_agent(bool online);
    void on_output(const QString& console_id, const QByteArray& bytes);
    void on_closed(const QString& console_id, int exit_code);
    void append(int session, const QString& text);
    void write(const Shell& shell, const QString& line);
    [[nodiscard]] std::optional<int> session_of(const QString& console_id) const;

    link::AgentRpcClient& rpc_;
    StatusHub& hub_;
    bool agent_online_ = false;
    std::uint64_t next_attempt_ = 1;
    QHash<int, Shell> shells_;

    // The handlers capture ``this``; the destructor drops these first,
    // which waits out a handler the reader thread is already running.
    link::EventSubscription output_sub_;
    link::EventSubscription closed_sub_;
};

}  // namespace lsw::host::services
