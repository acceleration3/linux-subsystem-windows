#include "lsw/host/manager/console_panel.hpp"

#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QSignalBlocker>
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

ConsolePanel::ConsolePanel(link::AgentRpcClient& rpc, services::StatusHub& status,
                           QWidget* parent)
    : QWidget(parent), rpc_(rpc), hub_(status) {
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
    picker_ = new QComboBox;
    picker_->setToolTip(tr("The Windows session cmd.exe runs in"));
    connect(picker_, &QComboBox::activated, this, &ConsolePanel::on_picker);
    row->addWidget(picker_);
    input_ = new QLineEdit;
    input_->setPlaceholderText(tr("Type a command and press Enter"));
    input_->setFont(ui::Theme::mono_font(12));
    input_->setObjectName(QString::fromLatin1(ui::Theme::Role::kMono));
    input_->installEventFilter(this);  // catch Ctrl+C / Ctrl+D
    row->addWidget(input_, 1);
    auto* send = ui::Widgets::button(QStringLiteral("Send"), ui::Theme::Role::kPrimary);
    connect(send, &QPushButton::clicked, this, &ConsolePanel::on_input_submitted);
    row->addWidget(send);

    status_ = ui::Widgets::meta(QString{});
    root->addWidget(status_);
    agent_online_ = hub_.vm().agent_online();
    on_agent(agent_online_);
    rebuild_picker();
    connect(&hub_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Sessions) {
            rebuild_picker();
        }
        const bool online = hub_.vm().agent_online();
        if (online != agent_online_) {
            agent_online_ = online;
            on_agent(online);
        }
    });

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

void ConsolePanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    hub_.set_detail(services::StatusHub::Area::Sessions, false);
}

void ConsolePanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    hub_.set_detail(services::StatusHub::Area::Sessions, true);
    // Lazily, and once. A manager sitting on the Overview page has no
    // business holding a cmd.exe open inside the guest, and opening the
    // session in the constructor did exactly that.
    if (session_id_.isEmpty() && !opening_ && agent_online_) {
        open_session();
    }
}

void ConsolePanel::on_agent(bool online) {
    if (online) {
        if (session_id_.isEmpty() && !opening_ && isVisible()) {
            open_session();
        } else if (session_id_.isEmpty() && !opening_) {
            status_->setText(tr("Windows is answering. The console opens when this page is shown."));
        }
        return;
    }
    // A session is a cmd.exe the agent holds; with the agent gone, so
    // is the shell, whatever the transcript still shows.
    if (!session_id_.isEmpty()) {
        append_output(QStringLiteral("\n[the agent stopped answering; session %1 is gone]\n")
                          .arg(session_id_));
    }
    session_id_.clear();
    shell_session_.reset();
    opening_ = false;
    input_->setEnabled(false);
    status_->setText(tr("Windows is not answering. The console opens when it does."));
    Q_EMIT summary_changed();
}

void ConsolePanel::open_session() {
    opening_ = true;
    status_->setText(tr("Opening cmd.exe in the guest"));
    nlohmann::json params = nlohmann::json::object();
    if (wanted_session_) {
        params["session"] = *wanted_session_;
    }
    rpc_.request_async(linkproto::cmd::kConsoleOpen, std::move(params), this,
                       [this](const link::AgentReply& reply) {
        const auto sid = reply.ok
                             ? QString::fromStdString(reply.body.value("session_id", std::string{}))
                             : QString{};
        if (sid.isEmpty()) {
            LSW_WARN("console: open failed: {}", reply.ok ? "no session_id" : reply.error);
            opening_ = false;
            status_->setText(tr("Could not open a console: %1")
                                 .arg(reply.ok ? tr("the agent gave no session")
                                               : QString::fromStdString(reply.error)));
            input_->setEnabled(false);
            return;
        }
        if (!opening_) {
            // Released, or the agent went away, while the reply was on its
            // way: the shell it opened is not wanted.
            rpc_.request_detached(linkproto::cmd::kConsoleClose,
                                  {{"session_id", sid.toStdString()}});
            return;
        }
        opening_ = false;
        session_id_ = sid;
        shell_session_ = reply.body.value("session", 0);
        shell_user_ = QString::fromStdString(reply.body.value("user", std::string{}));
        shell_elevated_ = reply.body.value("elevated", false);
        status_->setText(tr("Console %1 in Windows session %2 as %3%4. Type a command and press "
                            "Enter.")
                             .arg(sid)
                             .arg(*shell_session_)
                             .arg(shell_user_.isEmpty() ? tr("an unnamed user") : shell_user_,
                                  shell_elevated_ ? tr(", elevated") : QString{}));
        input_->setEnabled(true);
        input_->setFocus();
        rebuild_picker();
        Q_EMIT summary_changed();
    });
}

QString ConsolePanel::summary() const {
    if (session_id_.isEmpty()) {
        if (opening_) {
            return QStringLiteral("cmd.exe · opening");
        }
        return agent_online_ ? QStringLiteral("cmd.exe · no shell open")
                             : QStringLiteral("cmd.exe · agent not answering");
    }
    return QStringLiteral("cmd.exe · Windows session %1 · %2%3")
        .arg(shell_session_.value_or(0))
        .arg(shell_user_.isEmpty() ? QStringLiteral("unnamed user") : shell_user_,
             shell_elevated_ ? QStringLiteral(", elevated") : QString{});
}

void ConsolePanel::rebuild_picker() {
    const QSignalBlocker quiet(picker_);
    picker_->clear();
    const auto& sessions = hub_.sessions();
    if (!sessions.valid) {
        // Nothing to choose from while the agent is away; a shell opened
        // then goes wherever the agent puts it.
        picker_->addItem(tr("Default session"));
        picker_->setEnabled(false);
        return;
    }
    for (const auto& session : sessions.sessions) {
        QString label = tr("Session %1 · %2").arg(session.id).arg(
            session.user.isEmpty() ? tr("no user") : session.user);
        if (session.state != QLatin1String("active")) {
            label += QStringLiteral(" (%1)").arg(session.state);
        }
        picker_->addItem(label, session.id);
    }
    picker_->addItem(tr("Session 0 · SYSTEM"), 0);
    // What is selected is the shell that is open, else what was asked
    // for, else the agent's default.
    const std::optional<int> selected =
        shell_session_ ? shell_session_ : (wanted_session_ ? wanted_session_ : sessions.default_id);
    const int index = selected ? picker_->findData(*selected) : -1;
    picker_->setCurrentIndex(index >= 0 ? index : 0);
    picker_->setEnabled(true);
}

void ConsolePanel::on_picker(int index) {
    const QVariant item = picker_->itemData(index);
    if (!item.isValid()) {
        return;
    }
    const int chosen = item.toInt();
    if (shell_session_ == chosen || (!shell_session_ && wanted_session_ == chosen)) {
        return;
    }
    wanted_session_ = chosen;
    if (!session_id_.isEmpty()) {
        append_output(QStringLiteral("\n[moving to Windows session %1]\n").arg(chosen));
        release_session();
    }
    if (agent_online_ && isVisible() && !opening_) {
        open_session();
    }
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
    status_->setText(tr("Session %1 exited (code=%2). Leave the page and come back for a new "
                        "one.")
                         .arg(session_id)
                         .arg(exit_code));
    input_->setEnabled(false);
    session_id_.clear();
    shell_session_.reset();
    Q_EMIT summary_changed();
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
    shell_session_.reset();
    opening_ = false;
    input_->setEnabled(false);
    status_->setText(tr("Session %1 closed. A new one opens when this page is shown.").arg(sid));
    rpc_.request_detached(linkproto::cmd::kConsoleClose, {{"session_id", sid.toStdString()}});
    Q_EMIT summary_changed();
}

}  // namespace lsw::host::manager
