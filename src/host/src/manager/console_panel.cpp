#include "lsw/host/manager/console_panel.hpp"

#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"

#include <QByteArray>
#include <QCloseEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QStringList>
#include <QTextCursor>
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
    output_->setFont(ui::Theme::mono_font(12));
    output_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMono));
    root->addWidget(output_, 1);

    // The input lives in a band under the transcript, on the surface
    // colour with a rule over it, with a Send button for the case where
    // Enter is not the habit.
    auto* row = ui::DialogChrome::foot_band(root, QMargins(14, 10, 14, 10), 8);
    input_ = new QLineEdit;
    input_->setPlaceholderText(tr("Type a command and press Enter"));
    input_->setFont(ui::Theme::mono_font(12));
    input_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMono));
    input_->installEventFilter(this);  // catch Ctrl+C / Ctrl+D
    row->addWidget(input_, 1);
    auto* send = ui::Widgets::button(QStringLiteral("Send"), ui::Theme::Role::kPrimary);
    connect(send, &QPushButton::clicked, this, &ConsolePanel::on_input_submitted);
    row->addWidget(send);

    status_ = ui::Widgets::meta(tr("Spawning cmd.exe..."));
    root->addWidget(status_);

    connect(input_, &QLineEdit::returnPressed, this, &ConsolePanel::on_input_submitted);

    // The agent fires the same topic for every session, so the slots
    // filter by session_id.
    output_sub_ = rpc_.subscribe(linkproto::topic::kConsoleOutput, [this](const nlohmann::json& f) {
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        const auto b64 = QByteArray::fromStdString(f.value("data_b64", std::string{}));
        const auto bytes = QByteArray::fromBase64(b64);
        QMetaObject::invokeMethod(this, "on_output_event", Qt::QueuedConnection,
                                  Q_ARG(QString, sid), Q_ARG(QByteArray, bytes));
    });
    closed_sub_ = rpc_.subscribe(linkproto::topic::kConsoleClosed, [this](const nlohmann::json& f) {
        const auto sid = QString::fromStdString(f.value("session_id", std::string{}));
        const auto exit_code = f.value("exit_code", 0);
        QMetaObject::invokeMethod(this, "on_closed_event", Qt::QueuedConnection,
                                  Q_ARG(QString, sid), Q_ARG(int, exit_code));
    });
}

ConsolePanel::~ConsolePanel() {
    output_sub_.reset();
    closed_sub_.reset();
    release_session();
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
    rpc_.request_async(linkproto::cmd::kConsoleOpen, nlohmann::json::object(), this,
                       [this](const link::AgentReply& reply) {
        const auto sid = reply.ok
                             ? QString::fromStdString(reply.body.value("session_id", std::string{}))
                             : QString{};
        if (sid.isEmpty()) {
            LSW_WARN("console: open failed: {}", reply.ok ? "no session_id" : reply.error);
            status_->setText(tr("Could not open a console. The agent is not answering."));
            input_->setEnabled(false);
            return;
        }
        session_id_ = sid;
        status_->setText(tr("Session %1. Type a command and press Enter.").arg(sid));
        input_->setFocus();
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
    rpc_.request_detached(linkproto::cmd::kConsoleInput,
                          {{"session_id", session_id_.toStdString()},
                           {"data", to_send.toStdString()}});
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
    rpc_.request_detached(linkproto::cmd::kConsoleClose, {{"session_id", sid.toStdString()}});
}

}  // namespace lsw::host::manager
