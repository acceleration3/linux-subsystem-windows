#include "lsw/host/manager/console_window.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QByteArray>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QStringList>
#include <QTextCursor>
#include <QtConcurrent>
#include <QVBoxLayout>

#include <nlohmann/json.hpp>

#include "lsw/linkproto/commands.hpp"

namespace lsw::host::manager {

namespace {

constexpr int kMaxBufferLines = 5000;

}  // namespace

ConsoleWindow::ConsoleWindow(link::AgentRpcClient& rpc, QWidget* parent)
    : QDialog(parent), rpc_(rpc) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Command Line"));
    resize(900, 540);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(4);

    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    output_->setMaximumBlockCount(kMaxBufferLines);
    {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        output_->setFont(mono);
    }
    output_->setStyleSheet(
        "QPlainTextEdit { background-color: #111; color: #ddd; "
        "border: 1px solid #444; }");
    root->addWidget(output_, 1);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    auto* prompt = new QLabel(QStringLiteral(">"));
    prompt->setStyleSheet("color: #4af; font-weight: bold;");
    {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        prompt->setFont(mono);
    }
    row->addWidget(prompt);
    input_ = new QLineEdit;
    {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        input_->setFont(mono);
    }
    input_->setStyleSheet(
        "QLineEdit { background-color: #111; color: #ddd; "
        "border: 1px solid #444; padding: 2px 4px; }");
    input_->installEventFilter(this);  // catch Ctrl+C / Ctrl+D
    row->addWidget(input_, 1);
    root->addLayout(row);

    status_ = new QLabel(tr("Spawning cmd.exe..."));
    status_->setStyleSheet("color: gray;");
    root->addWidget(status_);

    connect(input_, &QLineEdit::returnPressed, this, &ConsoleWindow::on_input_submitted);

    // Register event handlers for console output/close. The agent
    // fires the same topic for every session, so we filter by
    // session_id in the slot. The lambdas capture ``this``, so the
    // dtor must off_event them before the dialog is torn down — see
    // WA_DeleteOnClose. A QPointer guard inside each lambda is a belt
    // for the brace in case an event arrives between off_event and
    // the actual deletion on the reader thread.
    const QPointer<ConsoleWindow> self{this};
    output_token_ = rpc_.on_event(QString::fromLatin1(linkproto::topic::kConsoleOutput),
                                    [self](const nlohmann::json& f) {
        if (!self) return;
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        const auto b64 = QByteArray::fromStdString(f.value("data_b64", std::string{}));
        const auto bytes = QByteArray::fromBase64(b64);
        QMetaObject::invokeMethod(
            self.data(), "on_output_event", Qt::QueuedConnection,
            Q_ARG(QString, sid), Q_ARG(QByteArray, bytes));
    });
    closed_token_ = rpc_.on_event(QString::fromLatin1(linkproto::topic::kConsoleClosed),
                                    [self](const nlohmann::json& f) {
        if (!self) return;
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        const auto exit_code = f.value("exit_code", 0);
        QMetaObject::invokeMethod(
            self.data(), "on_closed_event", Qt::QueuedConnection,
            Q_ARG(QString, sid), Q_ARG(int, exit_code));
    });

    open_session();
}

ConsoleWindow::~ConsoleWindow() {
    rpc_.off_event(output_token_);
    rpc_.off_event(closed_token_);
}

void ConsoleWindow::open_session() {
    // Async — don't block the GUI on the RPC. ``call`` blocks until
    // the reply arrives, so wrap in QtConcurrent and marshal the
    // session_id back via QueuedConnection.
    const QPointer<ConsoleWindow> self{this};
    (void)QtConcurrent::run([self]() {
        if (!self) return;
        const nlohmann::json req = {{"cmd", linkproto::cmd::kConsoleOpen}};
        const auto reply = self->rpc_.call(req);
        QString sid;
        if (!reply.isEmpty()) {
            try {
                const auto j = nlohmann::json::parse(reply.constData(),
                                                      reply.constData() + reply.size());
                if (j.value("ok", false)) {
                    sid = QString::fromStdString(j.value("session_id", std::string{}));
                }
            } catch (const nlohmann::json::exception& e) {
                // sid stays empty, which the caller reports as a console
                // that would not open. Say which reply did it.
                LSW_WARN("console: open reply did not parse: {}", e.what());
            }
        }
        QMetaObject::invokeMethod(
            self.data(), [self, sid]() {
                if (!self) return;
                if (sid.isEmpty()) {
                    self->status_->setText(self->tr("Failed to open console — agent unreachable?"));
                    self->input_->setEnabled(false);
                    return;
                }
                self->session_id_ = sid;
                self->status_->setText(self->tr("Session %1 — type commands and press Enter").arg(sid));
                self->input_->setFocus();
            },
            Qt::QueuedConnection);
    });
}

QString ConsoleWindow::sanitize(const QByteArray& raw) {
    // cmd.exe spits out CR-LF; QPlainTextEdit handles both, but
    // strip stray \r so the cursor doesn't bounce.
    QString s = QString::fromLocal8Bit(raw);
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.remove(QChar('\r'));
    return s;
}

void ConsoleWindow::append_output(const QString& text) {
    auto* sb = output_->verticalScrollBar();
    const bool was_at_bottom = sb->value() == sb->maximum();
    auto cursor = output_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (was_at_bottom) {
        output_->moveCursor(QTextCursor::End);
    }
}

void ConsoleWindow::on_output_event(const QString& session_id, const QByteArray& bytes) {
    if (session_id != session_id_) return;
    append_output(sanitize(bytes));
}

void ConsoleWindow::on_closed_event(const QString& session_id, int exit_code) {
    if (session_id != session_id_) return;
    status_->setText(tr("Session %1 exited (code=%2)").arg(session_id).arg(exit_code));
    input_->setEnabled(false);
    session_id_.clear();
}

void ConsoleWindow::on_input_submitted() {
    if (session_id_.isEmpty()) return;
    const QString line = input_->text();
    input_->clear();
    // Echo what the user typed into the output pane so they can see
    // their command above cmd's response (cmd doesn't echo via
    // redirected stdin).
    append_output(QStringLiteral("> ") + line + QChar('\n'));
    const QString to_send = line + QStringLiteral("\r\n");
    const QString sid = session_id_;
    // Capture the rpc by pointer (it outlives the dialog — owned by
    // App) instead of ``this`` so the worker is safe even if the
    // dialog closes while the call is in flight.
    auto* rpc = &rpc_;
    (void)QtConcurrent::run([rpc, sid, to_send]() {
        nlohmann::json req = {
            {"cmd", linkproto::cmd::kConsoleInput},
            {"params", {{"session_id", sid.toStdString()},
                         {"data", to_send.toStdString()}}},
        };
        (void)rpc->call(req);
    });
}

bool ConsoleWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched != input_ || event->type() != QEvent::KeyPress) {
        return QDialog::eventFilter(watched, event);
    }
    auto* ke = static_cast<QKeyEvent*>(event);
    // Ctrl+L clears the output buffer — convenient and Bash-ish.
    if (ke->modifiers().testFlag(Qt::ControlModifier) && ke->key() == Qt::Key_L) {
        output_->clear();
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void ConsoleWindow::closeEvent(QCloseEvent* e) {
    if (!session_id_.isEmpty()) {
        const QString sid = session_id_;
        session_id_.clear();
        // ``this`` is about to be deleted (WA_DeleteOnClose); the
        // worker only needs the rpc reference, which the App owns.
        auto* rpc = &rpc_;
        (void)QtConcurrent::run([rpc, sid]() {
            nlohmann::json req = {
                {"cmd", linkproto::cmd::kConsoleClose},
                {"params", {{"session_id", sid.toStdString()}}},
            };
            (void)rpc->call(req);
        });
    }
    QDialog::closeEvent(e);
}

}  // namespace lsw::host::manager
