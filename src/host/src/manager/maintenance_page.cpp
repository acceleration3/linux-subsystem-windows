#include "lsw/host/manager/maintenance_page.hpp"

#include "lsw/host/services/guest_update_steps.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/flow_layout.hpp"
#include "lsw/host/ui/broken_mark.hpp"
#include "lsw/host/ui/key_values.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using services::GuestUpdateStatus;
using services::GuestUpdateSteps;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using State = GuestUpdateStatus::State;
using Area = services::StatusHub::Area;

constexpr int kReleaseChars = 12;
constexpr int kToolCell = 300;

QString brief(const QString& release) {
    return release.isEmpty() ? QStringLiteral("-") : release.left(kReleaseChars);
}

QString version_text(const QString& version) {
    return version.isEmpty() ? QStringLiteral("-") : version;
}

QString time_text(const QString& finished) {
    QDateTime at = QDateTime::fromString(finished, Qt::ISODateWithMs);
    if (!at.isValid()) {
        at = QDateTime::fromString(finished, Qt::ISODate);
    }
    if (!at.isValid()) {
        return QStringLiteral("-");
    }
    return at.toLocalTime().toString(QStringLiteral("d MMM, HH:mm"));
}

bool current(const GuestUpdateStatus& update) {
    return !update.guest_release.isEmpty() && update.guest_release == update.shipped_release;
}

QWidget* tool_row(const QString& text, QPushButton* button) {
    auto* row = new QWidget;
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(16);
    line->addWidget(Widgets::prose(text), 1);
    line->addWidget(button, 0, Qt::AlignVCenter);
    return row;
}

}

MaintenancePage::MaintenancePage(services::StatusHub& status, QWidget* parent)
    : ui::ScrollPage(parent), status_(status) {
    build_update();
    build_tools();
    column()->addStretch(1);

    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Update || area == Area::Vm) {
            refresh();
        }
    });
    refresh();
}

QString MaintenancePage::summary() const {
    const auto& update = status_.update();
    const QString shipped = QStringLiteral("LSW %1").arg(version_text(update.shipped_version));
    if (update.guest_release.isEmpty()) {
        return QStringLiteral("%1 · Windows has -").arg(shipped);
    }
    if (current(update)) {
        return QStringLiteral("%1 · Windows has the same release").arg(shipped);
    }
    return QStringLiteral("%1 · Windows has %2").arg(shipped, version_text(update.guest_version));
}

void MaintenancePage::build_update() {
    auto panel = ui::Panel::area(QStringLiteral("LSW in Windows"),
                                 QStringLiteral("agent, window mirror, GPU driver, loaders, shared "
                                                "folders"));
    pill_ = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Unknown"));
    panel.header->addWidget(pill_);
    update_button_ = Widgets::button(QStringLiteral("Update"));
    connect(update_button_, &QPushButton::clicked, this, &MaintenancePage::update_requested);
    panel.header->addWidget(update_button_);

    facts_ = new QWidget;
    facts_box_ = new QVBoxLayout(facts_);
    facts_box_->setContentsMargins(0, 0, 0, 0);
    panel.body->addWidget(facts_);

    chip_row_ = new QWidget;
    chips_ = new ui::FlowLayout(chip_row_, ui::FlowLayout::Mode::Wrap, 6, 6);
    chips_->setContentsMargins(0, 0, 0, 0);
    panel.body->addWidget(chip_row_);

    panel.body->addWidget(Widgets::meta(
        QStringLiteral("Update replaces every LSW file in Windows and restarts Windows when the "
                       "driver changes. It runs by itself when the two releases differ."),
        true));
    column()->addWidget(panel.frame);
}

void MaintenancePage::build_tools() {
    auto* pair = new QWidget;
    auto* grid = new ui::FlowLayout(pair, ui::FlowLayout::Mode::Even, 12, 12);
    grid->set_cell_width(kToolCell);
    grid->setContentsMargins(0, 0, 0, 0);

    auto download = ui::Panel::area(QStringLiteral("Download a Windows image"));
    auto* download_button = Widgets::button(QStringLiteral("Download"));
    connect(download_button, &QPushButton::clicked, this, &MaintenancePage::download_requested);
    download.body->addWidget(tool_row(
        QStringLiteral("Fetch an installation ISO from microsoft.com into any folder, for a "
                       "reinstall or a second edition."),
        download_button));
    ui::BrokenMark::apply(download.frame);
    grid->addWidget(download.frame);

    auto trim = ui::Panel::area(QStringLiteral("Trim a Windows image"));
    auto* trim_button = Widgets::button(QStringLiteral("Trim"));
    connect(trim_button, &QPushButton::clicked, this, &MaintenancePage::trim_requested);
    trim.body->addWidget(tool_row(
        QStringLiteral("Build a smaller install ISO from one you have. Does not touch the running "
                       "guest."),
        trim_button));
    grid->addWidget(trim.frame);
    column()->addWidget(pair);

    auto danger = ui::Panel::area(QStringLiteral("Reinstall the guest"), {}, true);
    auto* reinstall = Widgets::button(QStringLiteral("Reinstall"), Theme::Role::kStop);
    connect(reinstall, &QPushButton::clicked, this, &MaintenancePage::reinstall_requested);
    danger.body->addWidget(tool_row(
        QStringLiteral("Runs the first-run wizard again. Its install replaces the disk, and "
                       "applications installed in Windows are lost; closing the wizard before the "
                       "install starts changes nothing."),
        reinstall));
    column()->addWidget(danger.frame);
}

void MaintenancePage::refresh() {
    const auto& update = status_.update();
    const auto& vm = status_.vm();

    switch (update.state) {
    case State::Running:
        StatusPill::set(pill_, StatusPill::State::Running, QStringLiteral("Updating"));
        break;
    case State::Failed:
        StatusPill::set(pill_, StatusPill::State::Failed, QStringLiteral("Failed"));
        break;
    case State::Idle:
    case State::Succeeded:
        if (update.guest_release.isEmpty()) {
            StatusPill::set(pill_, StatusPill::State::Pending, QStringLiteral("Unknown"));
        } else if (current(update)) {
            StatusPill::set(pill_, StatusPill::State::Done, QStringLiteral("Up to date"));
        } else {
            StatusPill::set(pill_, StatusPill::State::Warning, QStringLiteral("Update available"));
        }
        break;
    }
    update_button_->setEnabled(!update.running() && vm.running() && vm.agent_online());

    while (QLayoutItem* item = facts_box_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    auto* holder = new QWidget;
    auto* holder_column = new QVBoxLayout(holder);
    holder_column->setContentsMargins(0, 0, 0, 0);
    ui::KeyValues rows(holder_column);
    rows.add(QStringLiteral("Windows has"), brief(update.guest_release), nullptr,
             update.guest_version.isEmpty() ? QString{}
                                            : QStringLiteral("LSW %1").arg(update.guest_version));
    rows.add(QStringLiteral("This LSW ships"), brief(update.shipped_release), nullptr,
             update.shipped_version.isEmpty() ? QString{}
                                              : QStringLiteral("LSW %1").arg(update.shipped_version));
    rows.add(QStringLiteral("Last update"), update.finished.isEmpty() ? QStringLiteral("-")
                                                                      : time_text(update.finished),
             nullptr,
             update.finished.isEmpty() ? QStringLiteral("none recorded")
                                       : (!update.last_ok ? QStringLiteral("failed")
                                                                        : QStringLiteral("finished")));
    facts_box_->addWidget(holder);

    while (QLayoutItem* item = chips_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto& chip_steps = update.running() ? update.live_steps : update.last_steps;
    for (const auto& step : chip_steps) {
        QString mark = QStringLiteral("○");
        const char* tone = Theme::Tone::kFaint;
        if (step.status == QLatin1String("done")) {
            mark = QStringLiteral("✓");
            tone = Theme::Tone::kOk;
        } else if (step.status == QLatin1String("running")) {
            mark = QStringLiteral("●");
            tone = nullptr;
        } else if (step.status == QLatin1String("skipped")) {
            mark = QStringLiteral("–");
        } else if (step.status == QLatin1String("failed")) {
            mark = QStringLiteral("✗");
            tone = Theme::Tone::kBad;
        }
        auto* chip = new QLabel(QStringLiteral("%1 %2").arg(mark, GuestUpdateSteps::title_of(step.name)));
        chip->setObjectName(QString::fromLatin1(Theme::Role::kStepChip));
        chip->setProperty(Theme::Tone::kProperty, QString::fromLatin1(tone));
        if (!step.message.isEmpty()) {
            chip->setToolTip(step.message);
        }
        chips_->addWidget(chip);
    }
    chip_row_->setVisible(!chip_steps.isEmpty());
    Q_EMIT summary_changed();
}

}
