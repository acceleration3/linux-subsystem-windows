#include "lsw/host/manager/update_dialog.hpp"

#include "lsw/host/services/guest_update_status.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace lsw::host::manager {

namespace {

using services::GuestUpdateStatus;
using ui::Theme;
using ui::Widgets;

// The host's steps, in order. These are the step texts GuestUpdate
// publishes, so the current one is found by its text.
constexpr const char* kPreparing = "Preparing files";
constexpr const char* kSending = "Sending files to Windows";
constexpr const char* kUpdating = "Updating Windows";
constexpr const char* kRestarting = "Restarting Windows";
constexpr const char* kDone = "Done";

const QStringList& host_step_names() {
    static const QStringList names{QString::fromLatin1(kPreparing), QString::fromLatin1(kSending),
                                   QString::fromLatin1(kUpdating), QString::fromLatin1(kRestarting),
                                   QString::fromLatin1(kDone)};
    return names;
}

QLabel* marked(const QString& mark, const QString& text, const char* tone) {
    auto* label = new QLabel(QStringLiteral("%1  %2").arg(mark, text));
    label->setWordWrap(true);
    // The current step is in the plain foreground, which is no tone.
    Widgets::set_flag(label, Theme::Tone::kProperty, tone != nullptr ? QVariant(tone) : QVariant());
    return label;
}

void clear(QWidget* box) {
    auto* layout = box->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

}  // namespace

UpdateDialog::UpdateDialog(services::StatusHub& status, QWidget* parent)
    : QDialog(parent), status_(status) {
    setWindowTitle(QStringLiteral("Updating LSW in Windows"));
    setModal(true);
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
    // The dialog is always the size of what it shows: it grows when a run's
    // outcome is added, and shrinks back when the next run starts, so every
    // run opens at the same size.
    root->setSizeConstraint(QLayout::SetFixedSize);
    root->setContentsMargins(22, 20, 22, 16);
    root->setSpacing(14);

    auto* heading = new QLabel(QStringLiteral("Updating LSW in Windows"));
    Widgets::set_role(heading, Theme::Role::kHeading);
    root->addWidget(heading);
    line_ = Widgets::prose(QString{});
    root->addWidget(line_);

    host_steps_ = new QWidget;
    auto* host_layout = new QVBoxLayout(host_steps_);
    host_layout->setContentsMargins(0, 0, 0, 0);
    host_layout->setSpacing(7);
    root->addWidget(host_steps_);

    guest_box_ = new QWidget;
    auto* guest_layout = new QVBoxLayout(guest_box_);
    guest_layout->setContentsMargins(0, 0, 0, 0);
    guest_layout->setSpacing(7);
    guest_layout->addWidget(Widgets::caption(QStringLiteral("IN WINDOWS")));
    guest_steps_ = new QWidget;
    auto* steps_layout = new QVBoxLayout(guest_steps_);
    steps_layout->setContentsMargins(0, 0, 0, 0);
    steps_layout->setSpacing(5);
    guest_layout->addWidget(guest_steps_);
    root->addWidget(guest_box_);

    failure_ = Widgets::prose(QString{});
    failure_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Widgets::set_role(failure_, Theme::Role::kCalloutCritical);
    root->addWidget(failure_);

    note_ = Widgets::meta(QStringLiteral("Apps in Windows cannot be opened until this finishes."), true);
    root->addWidget(note_);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    retry_ = new QPushButton(QStringLiteral("Try again"));
    close_ = new QPushButton(QStringLiteral("Close"));
    retry_->setCursor(Qt::PointingHandCursor);
    close_->setCursor(Qt::PointingHandCursor);
    Widgets::set_role(close_, Theme::Role::kPrimary);
    foot->addWidget(retry_);
    foot->addWidget(close_);
    root->addLayout(foot);

    connect(retry_, &QPushButton::clicked, this, &UpdateDialog::retry_requested);
    connect(close_, &QPushButton::clicked, this, &QDialog::accept);
    connect(&status_, &services::StatusHub::changed, this,
            [this](services::StatusHub::Area area) {
                if (area == services::StatusHub::Area::Update) {
                    refresh();
                }
            });
    refresh();
}

void UpdateDialog::reject() {
    if (!status_.update().running()) {
        QDialog::reject();
    }
}

void UpdateDialog::closeEvent(QCloseEvent* event) {
    if (status_.update().running()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void UpdateDialog::refresh() {
    using State = GuestUpdateStatus::State;
    const auto& update = status_.update();
    const bool running = update.running();
    const QString shipped = update.shipped_version.isEmpty() ? QStringLiteral("-")
                                                              : update.shipped_version;

    if (running) {
        if (!was_running_) {
            reached_ = -1;
            restart_seen_ = false;
            // A new run opens the dialog, also over one that was closed.
            open();
        }
        const int index = static_cast<int>(host_step_names().indexOf(update.step));
        if (index == 3) {
            restart_seen_ = true;
        }
        reached_ = std::max(reached_, index);
    }
    was_running_ = running;

    if (update.state == State::Succeeded) {
        line_->setText(QStringLiteral("Windows has LSW %1").arg(shipped));
    } else if (update.guest_version.isEmpty()) {
        line_->setText(QStringLiteral("Windows has an earlier LSW, updating to %1").arg(shipped));
    } else {
        line_->setText(QStringLiteral("Windows has LSW %1, updating to %2")
                           .arg(update.guest_version, shipped));
    }

    rebuild_host_steps();
    rebuild_guest_steps();
    failure_->setVisible(update.state == State::Failed);
    failure_->setText(update.message);
    note_->setVisible(running);
    retry_->setVisible(update.state == State::Failed);
    close_->setVisible(!running);
}

void UpdateDialog::rebuild_host_steps() {
    using State = GuestUpdateStatus::State;
    const auto state = status_.update().state;
    clear(host_steps_);
    const auto& names = host_step_names();
    for (int i = 0; i < names.size(); ++i) {
        if (i == 3 && !restart_seen_) {
            continue;
        }
        QLabel* label = nullptr;
        if (state == State::Succeeded) {
            label = marked(QStringLiteral("✓"), names[i], Theme::Tone::kOk);
        } else if (state == State::Failed) {
            // The step it stopped on failed; before it done, after it to come.
            label = i < reached_    ? marked(QStringLiteral("✓"), names[i], Theme::Tone::kOk)
                    : i == reached_ ? marked(QStringLiteral("✗"), names[i], Theme::Tone::kBad)
                                    : marked(QStringLiteral("○"), names[i], Theme::Tone::kFaint);
        } else {
            label = i < reached_    ? marked(QStringLiteral("✓"), names[i], Theme::Tone::kOk)
                    : i == reached_ ? marked(QStringLiteral("●"), names[i], nullptr)
                                    : marked(QStringLiteral("○"), names[i], Theme::Tone::kFaint);
        }
        host_steps_->layout()->addWidget(label);
    }
}

void UpdateDialog::rebuild_guest_steps() {
    const auto& steps = status_.update().last_steps;
    guest_box_->setVisible(!steps.isEmpty() && !status_.update().running());
    clear(guest_steps_);
    for (const auto& step : steps) {
        QString name = step.name;
        if (!name.isEmpty()) {
            name[0] = name[0].toUpper();
        }
        QLabel* label = nullptr;
        if (step.status == QLatin1String("done")) {
            label = marked(QStringLiteral("✓"), QStringLiteral("%1: updated").arg(name), Theme::Tone::kOk);
        } else if (step.status == QLatin1String("skipped")) {
            label = marked(QStringLiteral("✓"), QStringLiteral("%1: already current").arg(name),
                           Theme::Tone::kFaint);
        } else if (step.status == QLatin1String("failed")) {
            label = marked(QStringLiteral("✗"),
                           step.message.isEmpty()
                               ? QStringLiteral("%1: failed").arg(name)
                               : QStringLiteral("%1: failed, %2").arg(name, step.message),
                           Theme::Tone::kBad);
        } else {
            label = marked(QStringLiteral("○"), QStringLiteral("%1: not run").arg(name),
                           Theme::Tone::kFaint);
        }
        guest_steps_->layout()->addWidget(label);
    }
}

}  // namespace lsw::host::manager
