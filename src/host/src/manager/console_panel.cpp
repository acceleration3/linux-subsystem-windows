#include "lsw/host/manager/console_panel.hpp"

#include "lsw/host/ui/dialog_chrome.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include "lsw/host/services/guest_shells.hpp"
#include "lsw/host/services/status_hub.hpp"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QTextCursor>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

constexpr int kMaxBufferLines = 5000;

}  // namespace

ConsolePanel::ConsolePanel(services::GuestShells& shells, services::StatusHub& status,
                           QWidget* parent)
    : QWidget(parent), shells_(shells), hub_(status) {
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
    input_->installEventFilter(this);  // catch Ctrl+L
    row->addWidget(input_, 1);
    auto* send = ui::Widgets::button(QStringLiteral("Send"), ui::Theme::Role::kPrimary);
    connect(send, &QPushButton::clicked, this, &ConsolePanel::on_input_submitted);
    row->addWidget(send);

    status_ = ui::Widgets::meta(QString{});
    root->addWidget(status_);
    agent_online_ = hub_.vm().agent_online();
    rebuild_picker();
    refresh();
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

    connect(&shells_, &services::GuestShells::output, this,
            [this](int session, const QString& text) {
        if (shown_ == session) {
            append_output(text);
        }
    });
    connect(&shells_, &services::GuestShells::changed, this, &ConsolePanel::on_shell_changed);
}

void ConsolePanel::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    hub_.set_detail(services::StatusHub::Area::Sessions, false);
}

void ConsolePanel::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    hub_.set_detail(services::StatusHub::Area::Sessions, true);
    // Lazily: a manager sitting on the Overview page has no business
    // holding a cmd.exe open inside the guest. Once open, it stays.
    if (shown_) {
        shells_.open(*shown_);
    }
}

void ConsolePanel::on_agent(bool online) {
    if (online && shown_ && isVisible()) {
        shells_.open(*shown_);
    }
    refresh();
}

QString ConsolePanel::summary() const {
    if (!agent_online_) {
        return QStringLiteral("cmd.exe · agent not answering");
    }
    const auto* shell = shown_ ? shells_.find(*shown_) : nullptr;
    if (shell == nullptr || shell->state != services::GuestShells::State::Open) {
        return shell != nullptr && shell->state == services::GuestShells::State::Opening
                   ? QStringLiteral("cmd.exe · opening")
                   : QStringLiteral("cmd.exe · no shell open");
    }
    return QStringLiteral("cmd.exe · Windows session %1 · %2%3")
        .arg(*shown_)
        .arg(shell->user.isEmpty() ? QStringLiteral("unnamed user") : shell->user,
             shell->elevated ? QStringLiteral(", elevated") : QString{});
}

void ConsolePanel::refresh() {
    using State = services::GuestShells::State;
    const auto* shell = shown_ ? shells_.find(*shown_) : nullptr;
    QString text;
    bool typing = true;
    if (!agent_online_) {
        text = tr("Windows is not answering. The console opens when it does.");
        typing = false;
    } else if (!shown_) {
        text = tr("Asking Windows which sessions there are.");
        typing = false;
    } else if (shell == nullptr || shell->state == State::Idle) {
        text = tr("The console opens when this page is shown.");
    } else if (shell->state == State::Opening) {
        text = tr("Opening cmd.exe in Windows session %1.").arg(*shown_);
    } else if (shell->state == State::Open) {
        text = tr("cmd.exe in Windows session %1 as %2%3. Type a command and press Enter.")
                   .arg(*shown_)
                   .arg(shell->user.isEmpty() ? tr("an unnamed user") : shell->user,
                        shell->elevated ? tr(", elevated") : QString{});
    } else if (shell->state == State::Exited) {
        text = tr("cmd.exe exited (code %1). Press Enter for a new one.").arg(shell->exit_code);
    } else if (shell->state == State::Failed) {
        text = tr("No console: %1. Press Enter to try again.").arg(shell->error);
    } else {
        text = tr("That cmd.exe went with the agent. Press Enter for a new one.");
    }
    status_->setText(text);
    input_->setEnabled(typing);
    Q_EMIT summary_changed();
}

void ConsolePanel::on_shell_changed(int session) {
    if (shown_ != session) {
        return;
    }
    const auto* shell = shells_.find(session);
    if (shell != nullptr) {
        if (shell->transcript.isEmpty()) {
            output_->clear();  // cleared from this view or another
        }
        if (shell->state == services::GuestShells::State::Open) {
            Q_EMIT opened();
            if (isVisible()) {
                input_->setFocus();
            }
        } else if (shell->state == services::GuestShells::State::Failed) {
            Q_EMIT open_failed(shell->error);
        }
    }
    refresh();
}

void ConsolePanel::rebuild_picker() {
    const QSignalBlocker quiet(picker_);
    picker_->clear();
    const auto& sessions = hub_.sessions();
    if (!sessions.valid) {
        // Nothing to choose from while the agent is away. The shell on
        // screen stays there, transcript and all.
        picker_->addItem(shown_ ? tr("Session %1").arg(*shown_) : tr("Default session"));
        picker_->setEnabled(false);
        refresh();
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
    // What is selected is what was asked for, else the agent's default,
    // else the first there is.
    const std::optional<int> selected = wanted_ ? wanted_ : sessions.default_id;
    const int index = selected ? picker_->findData(*selected) : -1;
    picker_->setCurrentIndex(index >= 0 ? index : 0);
    picker_->setEnabled(true);
    show_shell(picker_->currentData().toInt());
}

void ConsolePanel::on_picker(int index) {
    const QVariant item = picker_->itemData(index);
    if (!item.isValid()) {
        return;
    }
    wanted_ = item.toInt();
    show_shell(*wanted_);
}

void ConsolePanel::show_shell(int session) {
    if (shown_ == session) {
        return;
    }
    shown_ = session;
    const auto* shell = shells_.find(session);
    output_->setPlainText(shell != nullptr ? shell->transcript : QString{});
    output_->moveCursor(QTextCursor::End);
    if (isVisible()) {
        shells_.open(session);
    }
    refresh();
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

void ConsolePanel::on_input_submitted() {
    if (!shown_ || !input_->isEnabled()) {
        return;
    }
    const QString line = input_->text();
    input_->clear();
    shells_.send(*shown_, line);
}

bool ConsolePanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched != input_ || event->type() != QEvent::KeyPress) {
        return QWidget::eventFilter(watched, event);
    }
    auto* ke = static_cast<QKeyEvent*>(event);
    // Ctrl+L clears the output buffer — convenient and Bash-ish.
    if (ke->modifiers().testFlag(Qt::ControlModifier) && ke->key() == Qt::Key_L) {
        clear_transcript();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void ConsolePanel::clear_transcript() {
    if (shown_) {
        shells_.clear(*shown_);
    }
    output_->clear();
}

}  // namespace lsw::host::manager
