#include "lsw/host/manager/launch_splash.hpp"

#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using ui::Widgets;

constexpr int kTickMs = 500;

const char* const kPhaseNames[] = {
    "Machine started",
    "Windows answering",
    "Application running",
};

}  // namespace

LaunchSplash::LaunchSplash(QString application, QIcon icon, std::optional<qint64> last_boot_ms,
                           QWidget* parent)
    : QWidget(parent, Qt::Window), application_(std::move(application)),
      expected_seconds_(last_boot_ms ? static_cast<int>(qMax<qint64>(1, *last_boot_ms / 1000))
                                     : 0) {
    setWindowTitle(application_);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 16);
    root->setSpacing(16);

    // ---- identity -------------------------------------------------
    auto* head = new QHBoxLayout;
    head->setSpacing(14);
    auto* badge = new QLabel;
    badge->setFixedSize(52, 52);
    badge->setAlignment(Qt::AlignCenter);
    if (!icon.isNull()) {
        badge->setPixmap(icon.pixmap(48, 48));
    } else {
        badge->setText(application_.left(2));
        badge->setStyleSheet(
            QStringLiteral("background: %1; border-radius: 11px; color: #0d0f14; "
                           "font-weight: 700; font-size: 17px;")
                .arg(QString::fromLatin1(Theme::Color::kAmber)));
    }
    auto* titles = new QVBoxLayout;
    titles->setSpacing(3);
    auto* name = new QLabel(application_);
    name->setObjectName(QString::fromLatin1(Theme::Role::kHeading));
    // Sentences, not values: the subtitle and the footnote are prose and
    // read in the interface face.
    subtitle_ = Widgets::prose(QStringLiteral("Starting Windows so it has somewhere to run"));
    titles->addWidget(name);
    titles->addWidget(subtitle_);
    head->addWidget(badge);
    head->addLayout(titles, 1);
    root->addLayout(head);

    // ---- progress -------------------------------------------------
    // Against the last boot when there was one, as an expectation
    // rather than a countdown, because it is one; busy otherwise.
    bar_ = new QProgressBar;
    bar_->setRange(0, expected_seconds_);
    root->addWidget(bar_);
    timing_ = Widgets::meta(QString{}, true);
    root->addWidget(timing_);

    // ---- phases ---------------------------------------------------
    auto* steps = new QVBoxLayout;
    steps->setSpacing(7);
    for (int i = 0; i < static_cast<int>(Phase::Count); ++i) {
        steps_[i] = new QLabel(QStringLiteral("○  %1")
                                   .arg(QString::fromLatin1(kPhaseNames[i])));
        steps_[i]->setProperty(Theme::Tone::kProperty, Theme::Tone::kFaint);
        steps->addWidget(steps_[i]);
    }
    root->addLayout(steps);
    show_phases();

    stall_text_ = Widgets::prose(QString{});
    stall_text_->setProperty(Theme::Tone::kProperty, Theme::Tone::kWarm);
    stall_text_->setVisible(false);
    root->addWidget(stall_text_);

    // ---- footers --------------------------------------------------
    // Two of them, swapped rather than rebuilt: the normal one, and the
    // one that appears when the guest has gone quiet.
    footer_ = new QWidget;
    auto* footer_layout = new QHBoxLayout(footer_);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->addWidget(
        Widgets::prose(QStringLiteral("Closing this leaves the guest starting.")), 1);
    auto* cancel = Widgets::button(QStringLiteral("Cancel"), Theme::Role::kQuiet);
    auto* hide_button = new QPushButton(QStringLiteral("Hide"));
    footer_layout->addWidget(cancel);
    footer_layout->addWidget(hide_button);
    root->addWidget(footer_);

    stalled_footer_ = new QWidget;
    auto* stalled_layout = new QHBoxLayout(stalled_footer_);
    stalled_layout->setContentsMargins(0, 0, 0, 0);
    stalled_layout->addStretch(1);
    auto* open_log = Widgets::button(QStringLiteral("Open the log"), Theme::Role::kQuiet);
    auto* keep = new QPushButton(QStringLiteral("Keep waiting"));
    auto* restart = Widgets::button(QStringLiteral("Restart the guest"), Theme::Role::kPrimary);
    stalled_layout->addWidget(open_log);
    stalled_layout->addWidget(keep);
    stalled_layout->addWidget(restart);
    stalled_footer_->setVisible(false);
    root->addWidget(stalled_footer_);

    connect(cancel, &QPushButton::clicked, this, [this] {
        Q_EMIT cancelled();
        close();
    });
    connect(hide_button, &QPushButton::clicked, this, [this] {
        // Hide, not cancel: the application still opens when it is
        // ready, it just stops occupying the screen while it waits.
        hide();
    });
    connect(open_log, &QPushButton::clicked, this, &LaunchSplash::log_requested);
    connect(keep, &QPushButton::clicked, this, &LaunchSplash::resume);
    connect(restart, &QPushButton::clicked, this, &LaunchSplash::restart_requested);

    clock_.start();
    ticker_.setInterval(kTickMs);
    connect(&ticker_, &QTimer::timeout, this, &LaunchSplash::tick);
    ticker_.start();
    tick();
}

void LaunchSplash::tick() {
    const int seconds = static_cast<int>(clock_.elapsed() / 1000);
    if (expected_seconds_ <= 0) {
        timing_->setText(ui::Format::clock(seconds));
        return;
    }
    bar_->setValue(qMin(seconds, expected_seconds_));
    timing_->setText(QStringLiteral("%1 · last boot took %2")
                         .arg(ui::Format::clock(seconds), ui::Format::clock(expected_seconds_)));
}

void LaunchSplash::reach(Phase phase) {
    const int index = static_cast<int>(phase);
    if (index <= reached_) {
        return;
    }
    reached_ = index;
    show_phases();
    switch (phase) {
    case Phase::MachineStarted:
        subtitle_->setText(QStringLiteral("Windows is booting"));
        break;
    case Phase::GuestAnswering:
        subtitle_->setText(QStringLiteral("Opening %1").arg(application_));
        break;
    case Phase::ApplicationRunning:
        ticker_.stop();
        close();
        break;
    case Phase::Count:
        break;
    }
}

void LaunchSplash::show_phases() {
    // Done up to the one reached, the next one current, the rest to come.
    for (int i = 0; i < static_cast<int>(Phase::Count); ++i) {
        const bool done = i <= reached_;
        const bool current = i == reached_ + 1;
        steps_[i]->setText(QStringLiteral("%1  %2")
                               .arg(done ? QStringLiteral("✓")
                                         : (current ? QStringLiteral("●")
                                                    : QStringLiteral("○")),
                                    QString::fromLatin1(kPhaseNames[i])));
        // The current phase is in the plain foreground, which is no tone.
        Widgets::set_flag(steps_[i], Theme::Tone::kProperty,
                          done      ? QVariant(Theme::Tone::kOk)
                          : current ? QVariant()
                                    : QVariant(Theme::Tone::kFaint));
    }
}

void LaunchSplash::resume() {
    stall_text_->setVisible(false);
    stalled_footer_->setVisible(false);
    footer_->setVisible(true);
    clock_.restart();
    tick();
}

void LaunchSplash::stall(const QString& reason) {
    stall_text_->setText(reason);
    stall_text_->setVisible(true);
    footer_->setVisible(false);
    stalled_footer_->setVisible(true);
    subtitle_->setText(QStringLiteral("Windows did not answer"));
}

}  // namespace lsw::host::manager
