#include "lsw/host/manager/console_panel.hpp"

#include "lsw/host/ui/theme.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QByteArray>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
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

ConsolePanel::ConsolePanel(link::AgentRpcClient& rpc, QWidget* parent)
    : QWidget(parent), rpc_(rpc) {
    setWindowTitle(tr("Command Line"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    output_->setMaximumBlockCount(kMaxBufferLines);
    {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        output_->setFont(mono);
    }
    output_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMono));
    root->addWidget(output_, 1);

    // The input lives in a band under the transcript, on the surface
    // colour with a rule over it, with a Send button for the case where
    // Enter is not the habit.
    auto* band = new QFrame;
    band->setObjectName(QString::fromLatin1(ui::Theme::Role::kDialogFoot));
    auto* row = new QHBoxLayout(band);
    row->setContentsMargins(14, 10, 14, 10);
    row->setSpacing(8);
    input_ = new QLineEdit;
    input_->setPlaceholderText(tr("Type a command and press Enter"));
    {
        QFont mono(QStringLiteral("monospace"));
        mono.setStyleHint(QFont::Monospace);
        input_->setFont(mono);
    }
    input_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMono));
    input_->installEventFilter(this);  // catch Ctrl+C / Ctrl+D
    row->addWidget(input_, 1);
    auto* send = new QPushButton(QStringLiteral("Send"));
    send->setObjectName(QString::fromLatin1(ui::Theme::Role::kPrimary));
    connect(send, &QPushButton::clicked, this, &ConsolePanel::on_input_submitted);
    row->addWidget(send);
    root->addWidget(band);

    status_ = new QLabel(tr("Spawning cmd.exe..."));
    status_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMeta));
    root->addWidget(status_);

    connect(input_, &QLineEdit::returnPressed, this, &ConsolePanel::on_input_submitted);

    // Register event handlers for console output/close. The agent
    // fires the same topic for every session, so we filter by
    // session_id in the slot. The lambdas capture ``this``, so the
    // dtor must off_event them before the dialog is torn down — see
    // WA_DeleteOnClose. A QPointer guard inside each lambda is a belt
    // for the brace in case an event arrives between off_event and
    // the actual deletion on the reader thread.
    const QPointer<ConsolePanel> self{this};
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
}

ConsolePanel::~ConsolePanel() {
    release_session();
    rpc_.off_event(output_token_);
    rpc_.off_event(closed_token_);
}

void ConsolePanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    // Lazily, and once. A manager sitting on the Overview page has no
    // business holding a cmd.exe open inside the guest, and opening the
    // session in the constructor did exactly that.
    if (session_id_.isEmpty() && !opening_) {
        opening_ = true;
        open_session();
    }
}

void ConsolePanel::open_session() {
    // Async — don't block the GUI on the RPC. ``call`` blocks until
    // the reply arrives, so wrap in QtConcurrent and marshal the
    // session_id back via QueuedConnection.
    const QPointer<ConsolePanel> self{this};
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
                    self->status_->setText(self->tr("Could not open a console. The agent is not answering."));
                    self->input_->setEnabled(false);
                    return;
                }
                self->session_id_ = sid;
                self->status_->setText(self->tr("Session %1. Type a command and press Enter.").arg(sid));
                self->input_->setFocus();
            },
            Qt::QueuedConnection);
    });
}

QString ConsolePanel::sanitize(const QByteArray& raw) {
    // cmd.exe spits out CR-LF; QPlainTextEdit handles both, but
    // strip stray \r so the cursor doesn't bounce.
    QString s = QString::fromLocal8Bit(raw);
    s.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    s.remove(QChar('\r'));
    return s;
}

void ConsolePanel::append_output(const QString& text) {
    auto* sb = output_->verticalScrollBar();
    const bool was_at_bottom = sb->value() == sb->maximum();
    auto cursor = output_->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    if (was_at_bottom) {
        output_->moveCursor(QTextCursor::End);
    }
}

void ConsolePanel::on_output_event(const QString& session_id, const QByteArray& bytes) {
    if (session_id != session_id_) return;
    append_output(sanitize(bytes));
}

void ConsolePanel::on_closed_event(const QString& session_id, int exit_code) {
    if (session_id != session_id_) return;
    status_->setText(tr("Session %1 exited (code=%2)").arg(session_id).arg(exit_code));
    input_->setEnabled(false);
    session_id_.clear();
}

void ConsolePanel::on_input_submitted() {
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

bool ConsolePanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched != input_ || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }
    auto* ke = static_cast<QKeyEvent*>(event);
    // Ctrl+L clears the output buffer — convenient and Bash-ish.
    if (ke->modifiers().testFlag(Qt::ControlModifier) && ke->key() == Qt::Key_L) {
        output_->clear();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ConsolePanel::clear_transcript() {
    output_->clear();
}

void ConsolePanel::release_session() {
    if (session_id_.isEmpty()) {
        opening_ = false;
        return;
    }
    const QString sid = session_id_;
    session_id_.clear();
    opening_ = false;
    // The worker only needs the rpc reference, which the App owns and
    // outlives this page.
    auto* rpc = &rpc_;
    (void)QtConcurrent::run([rpc, sid]() {
        nlohmann::json req = {
            {"cmd", linkproto::cmd::kConsoleClose},
            {"params", {{"session_id", sid.toStdString()}}},
        };
        (void)rpc->call(req);
    });
}

}  // namespace lsw::host::manager
