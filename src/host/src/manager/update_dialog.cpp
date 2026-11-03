#include "lsw/host/manager/update_dialog.hpp"

#include "lsw/host/services/guest_update_status.hpp"
#include "lsw/host/services/guest_update_steps.hpp"
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

constexpr const char* kPreparing = "Preparing files";
constexpr const char* kSending = "Sending files to Windows";
constexpr int kHostSteps = 2;

const QStringList& host_step_names() {
    static const QStringList names{QString::fromLatin1(kPreparing), QString::fromLatin1(kSending)};
    return names;
}

void clear(QWidget* box) {
    auto* layout = box->layout();
    while (QLayoutItem* item = layout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

}

UpdateDialog::UpdateDialog(services::StatusHub& status, QWidget* parent)
    : QDialog(parent), status_(status) {
    setWindowTitle(QStringLiteral("Updating LSW in Windows"));
    setModal(true);
    setMinimumWidth(460);

    auto* root = new QVBoxLayout(this);
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
    host_steps_->setMinimumWidth(416);
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

    restart_line_ = new QWidget;
    auto* restart_layout = new QVBoxLayout(restart_line_);
    restart_layout->setContentsMargins(0, 0, 0, 0);
    restart_layout->setSpacing(0);
    root->addWidget(restart_line_);

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

    if (running && !was_running_) {
        reached_ = -1;
        opened_ = false;
    }
    if (running) {
        int index = static_cast<int>(host_step_names().indexOf(update.step));
        if (index < 0 && !update.step.isEmpty()) {
            index = kHostSteps;
        }
        reached_ = std::max(reached_, index);
    }
    const bool restarting = update.restart == GuestUpdateStatus::Restart::Running;
    const bool entered_failure = update.state == State::Failed && last_state_ != State::Failed;
    if (!opened_ && ((running && (!update.quiet || restarting)) || entered_failure)) {
        opened_ = true;
        open();
    }
    was_running_ = running;
    last_state_ = update.state;

    if (update.state == State::Succeeded) {
        line_->setText(QStringLiteral("Windows has LSW %1").arg(shipped));
    } else if (update.quiet) {
        line_->setText(QStringLiteral("Applying changed settings in Windows (LSW %1)").arg(shipped));
    } else if (update.guest_version.isEmpty()) {
        line_->setText(QStringLiteral("Windows has an earlier LSW, updating to %1").arg(shipped));
    } else {
        line_->setText(QStringLiteral("Windows has LSW %1, updating to %2")
                           .arg(update.guest_version, shipped));
    }

    rebuild_host_steps();
    rebuild_guest_steps();
    rebuild_restart_line();
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
        QLabel* label = nullptr;
        if (state == State::Succeeded || i < reached_) {
            label = Widgets::step_line(QStringLiteral("✓"), names[i], Theme::Tone::kOk);
        } else if (i == reached_) {
            label = state == State::Failed
                        ? Widgets::step_line(QStringLiteral("✗"), names[i], Theme::Tone::kBad)
                        : Widgets::step_line(QStringLiteral("●"), names[i], nullptr);
        } else {
            label = Widgets::step_line(QStringLiteral("○"), names[i], Theme::Tone::kFaint);
        }
        host_steps_->layout()->addWidget(label);
    }
}

void UpdateDialog::rebuild_guest_steps() {
    const auto& steps = status_.update().live_steps;
    clear(guest_steps_);
    for (const auto& entry : services::GuestUpdateSteps::all()) {
        const QString key = QString::fromLatin1(entry.key);
        const QString name = QString::fromLatin1(entry.title);
        GuestUpdateStatus::GuestStep step{key, QStringLiteral("pending"), {}};
        for (const auto& live : steps) {
            if (live.name == key) {
                step = live;
            }
        }
        QLabel* label = nullptr;
        if (step.status == QLatin1String("done")) {
            label = Widgets::step_line(QStringLiteral("✓"), QStringLiteral("%1: %2").arg(name, QString::fromLatin1(entry.done)), Theme::Tone::kOk);
        } else if (step.status == QLatin1String("skipped")) {
            label = Widgets::step_line(QStringLiteral("✓"), QStringLiteral("%1: already current").arg(name),
                           Theme::Tone::kFaint);
        } else if (step.status == QLatin1String("failed")) {
            label = Widgets::step_line(QStringLiteral("✗"),
                           step.message.isEmpty()
                               ? QStringLiteral("%1: failed").arg(name)
                               : QStringLiteral("%1: failed, %2").arg(name, step.message),
                           Theme::Tone::kBad);
        } else if (step.status == QLatin1String("running")) {
            label = Widgets::step_line(QStringLiteral("●"), name, nullptr);
        } else if (step.status == QLatin1String("not run")) {
            label = Widgets::step_line(QStringLiteral("○"), QStringLiteral("%1: not run").arg(name),
                           Theme::Tone::kFaint);
        } else {
            label = Widgets::step_line(QStringLiteral("○"), name, Theme::Tone::kFaint);
        }
        guest_steps_->layout()->addWidget(label);
    }
}

void UpdateDialog::rebuild_restart_line() {
    using Restart = GuestUpdateStatus::Restart;
    const auto& update = status_.update();
    clear(restart_line_);
    QLabel* label = nullptr;
    switch (update.restart) {
    case Restart::Pending:
        label = Widgets::step_line(QStringLiteral("○"), QStringLiteral("Restarting Windows"), Theme::Tone::kFaint);
        break;
    case Restart::NotNeeded:
        label = Widgets::step_line(QStringLiteral("✓"), QStringLiteral("Restarting Windows: not needed"),
                                   Theme::Tone::kFaint);
        break;
    case Restart::Running:
        label = Widgets::step_line(
            QStringLiteral("●"),
            update.restart_reasons.isEmpty()
                ? QStringLiteral("Restarting Windows")
                : QStringLiteral("Restarting Windows: %1").arg(update.restart_reasons.join(QStringLiteral(", "))),
            nullptr);
        break;
    case Restart::Done:
        label = Widgets::step_line(QStringLiteral("✓"), QStringLiteral("Restarting Windows"), Theme::Tone::kOk);
        break;
    }
    restart_line_->layout()->addWidget(label);
}

}
