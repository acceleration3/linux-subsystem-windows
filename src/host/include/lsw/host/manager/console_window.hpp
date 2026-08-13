#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QLineEdit;
class QLabel;

namespace lsw::host::manager {

// Manager-side terminal for an interactive ``cmd.exe`` running in
// the VM. Opening the dialog fires the ``console_open`` RPC at the
// agent; the agent spawns ``cmd.exe`` with redirected stdin/stdout
// and reports its output as ``console_output`` events. We append
// each chunk to the read-only QPlainTextEdit. Lines typed in the
// input QLineEdit are forwarded via the ``console_input`` RPC.
//
// Closing the dialog (or hitting Ctrl+D on an empty line) fires
// ``console_close``. The dialog is modeless and auto-deletes on
// close; multiple consoles can be open simultaneously (each gets
// its own session_id from the agent).
class ConsoleWindow : public QDialog {
    Q_OBJECT
public:
    explicit ConsoleWindow(link::AgentRpcClient& rpc, QWidget* parent = nullptr);
    ~ConsoleWindow() override;

protected:
    void closeEvent(QCloseEvent* e) override;
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
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* status_ = nullptr;

    // Tokens for the console_output / console_closed event handlers.
    // Must be unregistered in the destructor so the rpc reader thread
    // doesn't invoke a lambda that captured ``this`` after the dialog
    // is gone (WA_DeleteOnClose).
    link::AgentRpcClient::EventToken output_token_ = 0;
    link::AgentRpcClient::EventToken closed_token_ = 0;
};

}  // namespace lsw::host::manager
