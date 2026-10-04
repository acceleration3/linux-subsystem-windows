#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QWidget>
#include <QString>

class QPlainTextEdit;
class QLineEdit;
class QLabel;

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
class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    explicit ConsolePanel(link::AgentRpcClient& rpc, QWidget* parent = nullptr);
    ~ConsolePanel() override;

    // Ends the guest-side session if one is open. Called when the page
    // is left for long enough that holding a shell is rude, and by the
    // destructor.
    void release_session();

    // Empties what is on screen. The guest-side session keeps running:
    // this is the transcript, not the shell.
    void clear_transcript();

protected:
    void showEvent(QShowEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
    void on_input_submitted();
    void on_output_event(const QString& session_id, const QByteArray& bytes);
    void on_closed_event(const QString& session_id, int exit_code);

private:
    void open_session();
    void append_output(const QString& text);
    static QString sanitize(const QByteArray& raw);

    link::AgentRpcClient& rpc_;
    QString session_id_;
    bool opening_ = false;
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* status_ = nullptr;

    // The console_output / console_closed handlers capture ``this``;
    // the destructor drops these first, which waits out a handler the
    // reader thread is already running.
    link::EventSubscription output_sub_;
    link::EventSubscription closed_sub_;
};

}  // namespace lsw::host::manager
