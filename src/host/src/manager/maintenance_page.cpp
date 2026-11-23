#include "lsw/host/manager/maintenance_page.hpp"

#include "lsw/host/services/guest_update_steps.hpp"
#include "lsw/host/services/startup_header.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/flow_layout.hpp"
#include "lsw/host/ui/broken_mark.hpp"
#include "lsw/host/ui/key_values.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace lsw::host::manager {

namespace {

using services::GuestUpdateStatus;
using services::ReleaseStatus;
using services::StartupHeader;
using services::GuestUpdateSteps;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using State = GuestUpdateStatus::State;
using Area = services::StatusHub::Area;

constexpr int kReleaseChars = 12;
constexpr int kToolCell = 300;
constexpr int kReleasesShown = 5;
constexpr int kHeadlinesShown = 6;
constexpr int kCopiedMs = 1500;

QString brief(const QString& release) {
    return release.isEmpty() ? QStringLiteral("-") : release.left(kReleaseChars);
}

QString version_text(const QString& version) {
    return version.isEmpty() ? QStringLiteral("-") : version;
}

QString shipped_version(const GuestUpdateStatus& update) {
    return update.shipped_version.isEmpty() ? StartupHeader::number() : update.shipped_version;
}

QString day_text(const QString& published) {
    const QDateTime at = QDateTime::fromString(published, Qt::ISODate);
    return at.isValid() ? at.toLocalTime().toString(QStringLiteral("d MMM yyyy")) : QString{};
}

QString news_html(const QVector<ReleaseStatus::Release>& newer) {
    QString html;
    const int releases = std::min<int>(static_cast<int>(newer.size()), kReleasesShown);
    for (int i = 0; i < releases; ++i) {
        const auto& release = newer[i];
        html += QStringLiteral("<p style=\"margin:0 0 4px 0\"><b>LSW %1</b>").arg(release.version.toHtmlEscaped());
        const QString day = day_text(release.published);
        if (!day.isEmpty()) {
            html += QStringLiteral(" &middot; %1").arg(day);
        }
        if (release.page.isValid()) {
            html += QStringLiteral(" &middot; <a href=\"%1\">Release notes</a>")
                        .arg(release.page.toString(QUrl::FullyEncoded).toHtmlEscaped());
        }
        html += QStringLiteral("</p>");
        if (!release.headlines.isEmpty()) {
            html += QStringLiteral("<ul style=\"margin:0 0 10px 0\">");
            const int lines = std::min<int>(static_cast<int>(release.headlines.size()), kHeadlinesShown);
            for (int j = 0; j < lines; ++j) {
                html += QStringLiteral("<li>%1</li>").arg(release.headlines[j].toHtmlEscaped());
            }
            if (release.headlines.size() > lines) {
                html += QStringLiteral("<li>%1 more in the release notes</li>").arg(release.headlines.size() - lines);
            }
            html += QStringLiteral("</ul>");
        }
    }
    if (newer.size() > releases) {
        html += QStringLiteral("<p style=\"margin:0\">and %1 older releases</p>").arg(newer.size() - releases);
    }
    return html;
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
    build_release();
    build_update();
    build_tools();
    column()->addStretch(1);

    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Update || area == Area::Vm) {
            refresh();
        } else if (area == Area::Release) {
            refresh_release();
        }
    });
    refresh_release();
    refresh();
}

QString MaintenancePage::summary() const {
    const auto& update = status_.update();
    QString shipped = QStringLiteral("LSW %1").arg(shipped_version(update));
    if (status_.release().state == ReleaseStatus::State::Newer) {
        shipped += QStringLiteral(" · LSW %1 is out").arg(status_.release().newer.first().version);
    }
    if (update.guest_release.isEmpty()) {
        return QStringLiteral("%1 · Windows has -").arg(shipped);
    }
    if (current(update)) {
        return QStringLiteral("%1 · Windows has the same release").arg(shipped);
    }
    return QStringLiteral("%1 · Windows has %2").arg(shipped, version_text(update.guest_version));
}

void MaintenancePage::build_release() {
    auto panel = ui::Panel::area(QStringLiteral("This LSW"),
                                 QStringLiteral("The version of LSW installed on this computer."));
    release_pill_ = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Not checked"));
    panel.header->addWidget(release_pill_);
    copy_button_ = Widgets::button(QStringLiteral("Copy version"));
    connect(copy_button_, &QPushButton::clicked, this, [this] {
        QGuiApplication::clipboard()->setText(StartupHeader::version());
        copy_button_->setText(QStringLiteral("Copied"));
        QTimer::singleShot(kCopiedMs, copy_button_, [button = copy_button_] {
            button->setText(QStringLiteral("Copy version"));
        });
    });
    panel.header->addWidget(copy_button_);
    check_button_ = Widgets::button(QStringLiteral("Check for updates"));
    connect(check_button_, &QPushButton::clicked, this, &MaintenancePage::check_requested);
    panel.header->addWidget(check_button_);

    auto* holder = new QWidget;
    release_box_ = new QVBoxLayout(holder);
    release_box_->setContentsMargins(0, 0, 0, 0);
    release_box_->setSpacing(11);
    panel.body->addWidget(holder);

    panel.explain(QStringLiteral("Check for updates asks github.com for LSW's releases. The manager "
                                 "never checks by itself. A newer LSW comes from your package "
                                 "manager or the release page; once it is installed, the manager "
                                 "updates the software in Windows by itself."));
    column()->addWidget(panel.frame);
}

void MaintenancePage::refresh_release() {
    const ReleaseStatus& release = status_.release();
    switch (release.state) {
    case ReleaseStatus::State::Unchecked:
        StatusPill::set(release_pill_, StatusPill::State::Pending, QStringLiteral("Not checked"));
        break;
    case ReleaseStatus::State::Checking:
        StatusPill::set(release_pill_, StatusPill::State::Running, QStringLiteral("Checking"));
        break;
    case ReleaseStatus::State::Latest:
        StatusPill::set(release_pill_, StatusPill::State::Done, QStringLiteral("Latest release"));
        break;
    case ReleaseStatus::State::Newer:
        StatusPill::set(release_pill_, StatusPill::State::Warning,
                        QStringLiteral("LSW %1 available").arg(release.newer.first().version));
        break;
    case ReleaseStatus::State::Failed:
        StatusPill::set(release_pill_, StatusPill::State::Failed, QStringLiteral("Check failed"));
        break;
    }
    check_button_->setEnabled(release.state != ReleaseStatus::State::Checking);

    while (QLayoutItem* item = release_box_->takeAt(0)) {
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
    rows.add(QStringLiteral("Version"), StartupHeader::version());
    if (release.state == ReleaseStatus::State::Failed) {
        rows.add(QStringLiteral("Latest release"), version_text(release.error), Theme::Tone::kBad);
    } else if (release.state == ReleaseStatus::State::Newer) {
        const auto& newest = release.newer.first();
        const QString day = day_text(newest.published);
        rows.add(QStringLiteral("Latest release"),
                 day.isEmpty() ? QStringLiteral("LSW %1").arg(newest.version)
                               : QStringLiteral("LSW %1 · %2").arg(newest.version, day));
    } else if (release.state == ReleaseStatus::State::Latest) {
        rows.add(QStringLiteral("Latest release"), QStringLiteral("LSW %1").arg(StartupHeader::number()));
    } else {
        rows.add(QStringLiteral("Latest release"), QStringLiteral("-"));
    }
    rows.add(QStringLiteral("Checked"), time_text(release.checked));
    release_box_->addWidget(holder);

    if (release.state == ReleaseStatus::State::Newer) {
        auto* news = new QLabel(news_html(release.newer));
        news->setTextFormat(Qt::RichText);
        news->setWordWrap(true);
        news->setOpenExternalLinks(true);
        news->setTextInteractionFlags(Qt::TextBrowserInteraction);
        release_box_->addWidget(news);
    }
    Q_EMIT summary_changed();
}

void MaintenancePage::build_update() {
    auto panel = ui::Panel::area(QStringLiteral("LSW software in Windows"),
                                 QStringLiteral("The software LSW puts inside Windows: the service "
                                                "the manager talks to, window mirroring, the "
                                                "graphics driver and shared folder support."));
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

    panel.explain(
        QStringLiteral("Update copies this version of LSW's software into Windows, and "
                       "restarts Windows if the graphics driver changed. It runs by itself "
                       "when the versions differ. Out of date means Windows has older software "
                       "than this LSW, not that a newer LSW is out."));
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
    download.explain(QStringLiteral("Fetch an installation ISO from microsoft.com into any folder, "
                                    "for a reinstall or a second edition."));
    download.body->addWidget(download_button, 0, Qt::AlignLeft);
    ui::BrokenMark::apply(download.frame);
    grid->addWidget(download.frame);

    auto trim = ui::Panel::area(QStringLiteral("Trim a Windows image"));
    auto* trim_button = Widgets::button(QStringLiteral("Trim"));
    connect(trim_button, &QPushButton::clicked, this, &MaintenancePage::trim_requested);
    trim.explain(QStringLiteral("Build a smaller install ISO from one you have. Does not touch the "
                                "running guest."));
    trim.body->addWidget(trim_button, 0, Qt::AlignLeft);
    grid->addWidget(trim.frame);
    column()->addWidget(pair);

    auto danger = ui::Panel::area(QStringLiteral("Reinstall the guest"), {}, true);
    auto* reinstall = Widgets::button(QStringLiteral("Reinstall"), Theme::Role::kStop);
    connect(reinstall, &QPushButton::clicked, this, &MaintenancePage::reinstall_requested);
    danger.explain(QStringLiteral("Runs the first-run wizard again. Closing the wizard before the "
                                  "install starts changes nothing."));
    danger.body->addWidget(
        tool_row(QStringLiteral("Replaces the disk: applications installed in Windows are lost."),
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
            StatusPill::set(pill_, StatusPill::State::Warning, QStringLiteral("Out of date"));
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
    const auto release = [](const QString& version, const QString& sha) {
        if (sha.isEmpty()) {
            return version.isEmpty() ? QStringLiteral("-") : QStringLiteral("LSW %1").arg(version);
        }
        return version.isEmpty() ? brief(sha)
                                 : QStringLiteral("LSW %1 · %2").arg(version, brief(sha));
    };
    rows.add(QStringLiteral("Windows has"), release(update.guest_version, update.guest_release));
    rows.add(QStringLiteral("This LSW ships"),
             release(shipped_version(update), update.shipped_release));
    if (update.finished.isEmpty()) {
        rows.add(QStringLiteral("Last update"), QStringLiteral("-"));
    } else if (update.last_ok) {
        rows.add(QStringLiteral("Last update"), time_text(update.finished));
    } else {
        rows.add(QStringLiteral("Last update"), time_text(update.finished), Theme::Tone::kBad,
                 QStringLiteral("Failed"));
    }
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
