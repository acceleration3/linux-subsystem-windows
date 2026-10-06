#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QWidget>
#include <QString>

#include <optional>

class QComboBox;
class QPlainTextEdit;
class QLineEdit;
class QLabel;

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

// Manager-side terminal for an interactive ``cmd.exe`` running in the
// VM. It fires ``console_open`` at the agent, which spawns ``cmd.exe``
// with redirected stdin and stdout and reports its output back as
// ``console_output`` events; each chunk is appended to the read-only
// view, and lines typed below go out as ``console_input``.
//
// A page in the manager's stack rather than a window of its own. The
// session is opened the first time the page is shown and closed when
// the page is destroyed or explicitly released, so a manager left on
// the Overview page is not holding a cmd.exe open inside the guest.
// Ctrl+D on an empty line still ends the session.
//
// The agent is a session-0 service, so "the guest" is not one place: the
// picker in the input band chooses the Windows session cmd.exe runs in —
// a user's, as that user and elevated, or session 0 as SYSTEM. The list
// is the hub's; it is only polled while this page is on screen.
class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    // ``status`` says whether the agent answers — a session does not
    // survive it going away, and one is opened when it comes back — and
    // which Windows sessions there are to open one in.
    ConsolePanel(link::AgentRpcClient& rpc, services::StatusHub& status,
                 QWidget* parent = nullptr);
    ~ConsolePanel() override;

    // Ends the guest-side session if one is open. Called when the page
    // is left for long enough that holding a shell is rude, and by the
    // destructor.
    void release_session();

    // Empties what is on screen. The guest-side session keeps running:
    // this is the transcript, not the shell.
    void clear_transcript();

    // The page's one-line detail: where the open shell runs and as whom,
    // or why there is none. ``summary_changed`` when that changes.
    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();
    // The agent would not open a shell, and why; ``opened`` when one is.
    void open_failed(const QString& reason);
    void opened();

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
    void on_input_submitted();
    void on_output_event(const QString& session_id, const QByteArray& bytes);
    void on_closed_event(const QString& session_id, int exit_code);

private:
    void open_session();
    // The hub's session list changed: re-fill the picker, keeping the
    // choice when it still exists.
    void rebuild_picker();
    // The user picked a Windows session: the shell moves there.
    void on_picker(int index);
    // The agent started or stopped answering.
    void on_agent(bool online);
    void append_output(const QString& text);
    static QString sanitize(const QByteArray& raw);

    link::AgentRpcClient& rpc_;
    services::StatusHub& hub_;
    bool agent_online_ = false;
    QString session_id_;
    bool opening_ = false;
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* picker_ = nullptr;
    // The Windows session asked for; unset means the agent's default
    // (the console user's). ``shell_session_`` is where the open shell
    // actually runs, as the agent reported it.
    std::optional<int> wanted_session_;
    std::optional<int> shell_session_;
    QString shell_user_;
    bool shell_elevated_ = false;

    // The console_output / console_closed handlers capture ``this``;
    // the destructor drops these first, which waits out a handler the
    // reader thread is already running.
    link::EventSubscription output_sub_;
    link::EventSubscription closed_sub_;
};

}  // namespace lsw::host::manager
