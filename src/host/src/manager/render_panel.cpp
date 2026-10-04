#include "lsw/host/manager/render_panel.hpp"

#include "lsw/host/manager/pressure_meter.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace lsw::host::manager {

namespace {

using ui::Format;
using ui::Theme;
using ui::Widgets;
using vm::AreaPressure;
using vm::ChannelPressure;
using K = config::ManagerSettings::Key;

// The channel strip is a fixed band: its cards are buttons with a
// layout inside and no text of their own, so their sizeHint is one
// line and they clip whatever the layout put in them.
constexpr int kCardHeight = 62;
// What the band around the cards adds to their height.
constexpr int kStripPadding = 18;

const char* colour_for(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return Theme::Color::kBad;
    case AreaPressure::State::Tight:
        return Theme::Color::kAmber;
    case AreaPressure::State::Healthy:
        break;
    }
    return Theme::Color::kOk;
}

// An area's state as a pill: congested reads as a failure, tight as
// something in progress, healthy as done.
ui::StatusPill::State pill_for(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return ui::StatusPill::State::Failed;
    case AreaPressure::State::Tight:
        return ui::StatusPill::State::Running;
    case AreaPressure::State::Healthy:
        break;
    }
    return ui::StatusPill::State::Done;
}

// The property the stylesheet colours a bar by.
const char* state_key(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return "crit";
    case AreaPressure::State::Tight:
        return "hot";
    case AreaPressure::State::Healthy:
        break;
    }
    return "ok";
}

// How full an area is, as a percentage.
//
// Occupancy where somebody measures it — the arena's bytes in use, the
// ring's high-water mark, the largest answer the reply slot has held.
// The largest single payload stood in for this and was the wrong
// reading: a 1 MiB ring cycling 877,000 commands through reported the
// biggest one of them at 3.8 KiB, which is 0.3% and a bar that never
// moved off its floor.
int area_fill(const AreaPressure& area) {
    if (area.state == AreaPressure::State::Congested) {
        return 96;
    }
    if (area.state == AreaPressure::State::Tight) {
        return 72;
    }
    if (area.configured_kib == 0) {
        return 0;
    }
    const double capacity = static_cast<double>(area.configured_kib * 1024ULL);
    if (area.measured) {
        // What is in there now. The worst it has ever been is the other
        // band, drawn behind this one by area_peak_fill.
        const double used = static_cast<double>(area.used_bytes) / capacity;
        // A floor of 1 rather than 0: an area holding something is not
        // the same picture as one holding nothing, even when the
        // fraction rounds away.
        return std::clamp(static_cast<int>(used * 100.0),
                          area.used_bytes > 0 ? 1 : 0, 96);
    }
    // Nothing has published an occupancy. The largest payload is what
    // is left to say anything at all with.
    if (area.largest_bytes > 0) {
        const double used = static_cast<double>(area.largest_bytes) / capacity;
        return std::clamp(static_cast<int>(used * 100.0), 1, 96);
    }
    return 2;
}

// The band behind the live one: the most this area has ever held.
//
// Nothing to draw where nothing has published an occupancy — the bar
// then shows only what area_fill could infer, and a peak band invented
// to sit behind it would be a second reading of the same guess.
int area_peak_fill(const AreaPressure& area) {
    if (!area.measured || area.configured_kib == 0 || area.peak_bytes == 0) {
        return 0;
    }
    const double capacity = static_cast<double>(area.configured_kib * 1024ULL);
    const double peak = static_cast<double>(area.peak_bytes) / capacity;
    return std::clamp(static_cast<int>(peak * 100.0), 1, 96);
}

int fill_percent(const ChannelPressure& channel) {
    int worst = 0;
    for (const auto& area : channel.areas) {
        worst = qMax(worst, area_fill(area));
    }
    return worst;
}

QString word_for(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return QStringLiteral("Congested");
    case AreaPressure::State::Tight:
        return QStringLiteral("Tight");
    case AreaPressure::State::Healthy:
        break;
    }
    return QStringLiteral("Healthy");
}

// A key/value tile. Several sit in a row under each area's meter.
QWidget* kpi(const QString& key, const QString& value, const char* colour) {
    auto* box = new QWidget;
    Theme::surface(box, Theme::Role::kTile);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(11, 9, 11, 9);
    layout->setSpacing(3);
    layout->addWidget(Widgets::caption(key));
    auto* number = new QLabel(value);
    number->setObjectName(QString::fromLatin1(Theme::Role::kMono));
    number->setStyleSheet(QStringLiteral("font-size: 15px; color: %1;")
                              .arg(QString::fromLatin1(colour)));
    layout->addWidget(number);
    return box;
}

// Which settings key resizes which area, so a suggestion can stage the
// right one.
QString key_for_area(const QString& name) {
    if (name.startsWith(QLatin1String("Command"))) {
        return K::kRingKib;
    }
    if (name.startsWith(QLatin1String("Reply"))) {
        return K::kReplyKib;
    }
    return K::kBlobMib;
}

}  // namespace

RenderPanel::RenderPanel(vm::RenderStats& stats, config::ManagerSettings& settings,
                         config::PendingChanges& pending, QWidget* parent)
    : QWidget(parent), stats_(stats), settings_(settings), pending_(pending) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // The strip is a fixed band. Its cards are buttons with a layout
    // inside and no text, so neither they nor their holder have a
    // sizeHint worth anything; both are told their height instead of
    // being asked for it.
    strip_holder_ = new QWidget;
    strip_holder_->setFixedHeight(kCardHeight + kStripPadding);
    strip_ = new QHBoxLayout(strip_holder_);
    strip_->setContentsMargins(14, 12, 14, 6);
    strip_->setSpacing(8);
    root->addWidget(strip_holder_);

    summary_ = Widgets::meta(QString{}, true);
    summary_->setContentsMargins(14, 6, 14, 0);
    root->addWidget(summary_);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* holder = new QWidget;
    areas_ = new QVBoxLayout(holder);
    areas_->setContentsMargins(14, 12, 14, 14);
    areas_->setSpacing(12);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    connect(&stats_, &vm::RenderStats::updated, this, &RenderPanel::rebuild);

    // Ownership is a word in each region's header, so reading it costs
    // an open and a read and asks the server nothing. Two seconds is
    // fast enough for "who has channel 2" and slow enough to be free.
    poll_ = new QTimer(this);
    poll_->setInterval(2000);
    connect(poll_, &QTimer::timeout, this, [this] {
        if (!region_prefix_.isEmpty()) {
            stats_.poll_regions(region_prefix_);
        }
    });
    rebuild();
}

void RenderPanel::set_region_prefix(const QString& prefix) {
    region_prefix_ = prefix;
    if (prefix.isEmpty()) {
        poll_->stop();
        return;
    }
    stats_.poll_regions(prefix);
    poll_->start();
}

void RenderPanel::select(int channel) {
    selected_ = channel;
    rebuild();
}

// Commands, bytes and frames a second, in the units a reader thinks in.
QString RenderPanel::rate_line(const vm::ChannelPressure& channel) {
    if (channel.commands_per_second < 1.0 && channel.frames_per_second < 0.5) {
        return QStringLiteral("idle");
    }
    QString text = QStringLiteral("%1 cmd/s").arg(
        QLocale::system().toString(channel.commands_per_second, 'f', 0));
    if (channel.bytes_per_second >= 0.05 * 1024.0 * 1024.0) {
        text += QStringLiteral("  ·  ") + Format::mib_per_second(channel.bytes_per_second);
    }
    if (channel.frames_per_second >= 0.5) {
        text += QStringLiteral("  ·  %1 fps").arg(channel.frames_per_second, 0, 'f', 0);
    }
    return text;
}

void RenderPanel::rebuild() {
    // Both lists are rebuilt wholesale. They are a handful of widgets
    // and they change when a counter moves, which is rare enough that
    // diffing them would be more code than it saves.
    const auto clear = [](QLayout* layout) {
        while (auto* item = layout->takeAt(0)) {
            if (auto* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
    };
    clear(strip_);
    clear(areas_);

    const auto& channels = stats_.channels();
    if (selected_ >= channels.size()) {
        selected_ = 0;
    }

    card_height_ = kCardHeight;
    for (const auto& channel : channels) {
        // A bordered card, selected by its edge rather than by a rail
        // marker: it is a thing you pick, and the design draws it so.
        auto* card = new QPushButton;
        card->setObjectName(QString::fromLatin1(Theme::Role::kChannelCard));
        card->setCheckable(true);
        card->setChecked(channel.index == selected_);
        card->setCursor(Qt::PointingHandCursor);
        card->setMinimumWidth(158);
        // A QPushButton sizes itself from its text, and this one has
        // none: everything in it is a laid-out child. Left to its own
        // sizeHint it comes out one line tall and clips its contents.
        // Fixed to what the rows in it actually need rather than to a
        // constant — a constant is right until a row is added, and then
        // it silently cuts the new one off.
        card->setFixedHeight(kCardHeight);
        card_height_ = kCardHeight;

        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(11, 9, 11, 9);
        layout->setSpacing(6);

        auto* top = new QHBoxLayout;
        auto* name = new QLabel(QStringLiteral("Channel %1").arg(channel.index));
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto* dot = new QLabel(QStringLiteral("●"));
        dot->setAttribute(Qt::WA_TransparentForMouseEvents);
        dot->setStyleSheet(
            QStringLiteral("color: %1;")
                .arg(QString::fromLatin1(channel.claimed ? colour_for(channel.worst())
                                                         : "#39414f")));
        top->addWidget(name);
        top->addStretch(1);
        top->addWidget(dot);
        layout->addLayout(top);

        auto* owner = Widgets::meta(channel.claimed
                               ? (channel.owner_is_guest()
                                      ? QStringLiteral("guest pid %1").arg(channel.owner())
                                      : QStringLiteral("pid %1").arg(channel.owner()))
                               : QStringLiteral("free"));
        owner->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(owner);

        // What it is carrying right now. A claimed channel that is
        // drawing nothing reads "idle" rather than a row of zeroes.
        auto* rate = Widgets::meta(channel.claimed ? rate_line(channel) : QString{});
        rate->setAttribute(Qt::WA_TransparentForMouseEvents);
        rate->setVisible(channel.claimed);
        layout->addWidget(rate);

        // A strip of the worst area's pressure, so the strip reads at a
        // glance without opening each channel.
        auto* mini = new QProgressBar;
        mini->setObjectName(QString::fromLatin1(Theme::Role::kMiniBar));
        mini->setTextVisible(false);
        mini->setRange(0, 100);
        mini->setValue(channel.claimed ? fill_percent(channel) : 0);
        mini->setAttribute(Qt::WA_TransparentForMouseEvents);
        mini->setProperty("pressure", QString::fromLatin1(state_key(channel.worst())));
        layout->addWidget(mini);

        // Everything is in it now, so ask.
        if (const int needed = layout->sizeHint().height(); needed > card->height()) {
            card->setFixedHeight(needed);
            card_height_ = std::max(card_height_, needed);
        }

        connect(card, &QPushButton::clicked, this,
                [this, index = channel.index] { select(index); });
        strip_->addWidget(card);
    }
    // Adding one is a startup argument to the server, so it stages like
    // every other size on the page.
    auto* add = new QPushButton(QStringLiteral("+  Add a channel"));
    add->setObjectName(QString::fromLatin1(Theme::Role::kAddCard));
    add->setFixedHeight(card_height_);
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QPushButton::clicked, this, [this] {
        const int now = pending_.staged_or_current(K::kChannels).toInt();
        pending_.stage(K::kChannels, qMin(8, now + 1));
        Q_EMIT settings_requested();
    });
    strip_->addWidget(add);
    if (strip_holder_ != nullptr) {
        strip_holder_->setFixedHeight(card_height_ + kStripPadding);
    }
    strip_->addStretch(1);

    if (channels.isEmpty()) {
        return;
    }

    const auto& channel = channels.at(selected_);
    // Added up from the areas as the region carries them, not from the
    // settings. A channel already running keeps the sizes it started
    // with, so the settings describe the next one and not this one —
    // and a number that moves when a spin box does, while the channel
    // it names has not changed, is worse than no number.
    quint64 region_kib = 0;
    for (const auto& area : channel.areas) {
        region_kib += area.configured_kib;
    }
    const int region_mib =
        region_kib > 0
            ? static_cast<int>(region_kib / 1024)
            : settings_.int_value(K::kBlobMib)
                  + settings_.int_value(K::kRingKib) / 1024
                  + settings_.int_value(K::kReplyKib) / 1024;
    summary_->setText(
        QStringLiteral("Channel %1  ·  about %2 MiB  ·  %3")
            .arg(channel.index)
            .arg(region_mib)
            .arg(channel.claimed
                     ? QStringLiteral("claimed by %1pid %2")
                           .arg(channel.owner_is_guest() ? QStringLiteral("guest ")
                                                         : QString{})
                           .arg(channel.owner())
                     : (channel.owner_pid != 0
                            ? QStringLiteral("claimed by pid %1, which is gone")
                                  .arg(channel.owner())
                            : QStringLiteral("not claimed"))));

    // What can be done to this channel. Sizing it apart from the rest
    // is always on offer; releasing a claim is not, because taking a
    // channel from a running process corrupts the stream it is in the
    // middle of and the release refuses that anyway.
    {
        auto* row = new QWidget;
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 4);
        const bool stale = channel.owner_pid != 0 && !channel.claimed;
        if (stale) {
            layout->addWidget(
                Widgets::prose(QStringLiteral("The process that claimed this channel is gone. "
                                     "Nothing else can take it until the claim is "
                                     "cleared.")),
                1);
        } else {
            layout->addStretch(1);
        }
        auto* own_sizes = Widgets::button(QStringLiteral("Give this channel its own sizes"),
                                          Theme::Role::kQuiet);
        connect(own_sizes, &QPushButton::clicked, this,
                &RenderPanel::settings_requested);
        layout->addWidget(own_sizes);

        auto* release = Widgets::button(QStringLiteral("Release it"), Theme::Role::kQuiet);
        release->setEnabled(stale);
        connect(release, &QPushButton::clicked, this, [this, index = channel.index] {
            (void)stats_.release_channel(region_prefix_, index);
        });
        layout->addWidget(release);
        areas_->addWidget(row);
    }

    for (const auto& area : channel.areas) {
        // Named in the interface face rather than as a caption: the
        // header also carries the area's size and its state.
        const auto panel = ui::Panel::make();
        panel.header->setSpacing(10);
        panel.header->addWidget(new QLabel(area.name));
        panel.header->addWidget(Widgets::meta(Format::kib(area.configured_kib)));
        panel.header->addStretch(1);
        panel.header->addWidget(ui::StatusPill::make(pill_for(area.state), word_for(area.state)));
        auto* body = panel.body;
        body->setSpacing(11);

        // How full it is now, over how full it has ever been. Two
        // readings of one area, drawn as two bands of one colour:
        // filling to either alone throws the other away.
        auto* meter = new PressureMeter;
        meter->setObjectName(QString::fromLatin1(Theme::Role::kMeter));
        meter->set_readings(area_fill(area), area_peak_fill(area));
        meter->setProperty("pressure", QString::fromLatin1(state_key(area.state)));
        body->addWidget(meter);

        auto* grid = new QFrame;
        Theme::surface(grid, Theme::Role::kKpiGrid);
        auto* row = new QHBoxLayout(grid);
        row->setContentsMargins(1, 1, 1, 1);
        row->setSpacing(1);
        // The two bands of the meter, in the units the size in the
        // card's header is written in. A bar without its numbers says
        // "about a third" and never which third.
        //
        // Two tiles, because they are two measurements: what is in
        // there now, and how big the area has to be. The second is the
        // reason the page exists, and it does not belong inside the
        // first one's label.
        row->addWidget(kpi(QStringLiteral("Used"),
                           area.measured ? Format::bytes(area.used_bytes)
                                         : QStringLiteral("—"),
                           area.measured ? Theme::Color::kFgDim
                                         : Theme::Color::kFgFaint));
        row->addWidget(kpi(QStringLiteral("Peak"),
                           area.measured ? Format::bytes(area.peak_bytes)
                                         : QStringLiteral("—"),
                           area.measured ? Theme::Color::kFgDim
                                         : Theme::Color::kFgFaint));
        row->addWidget(kpi(QStringLiteral("Events"), QString::number(area.events),
                           area.events > 0 ? colour_for(area.state) : Theme::Color::kFgDim));
        // The second reading is a different one per area, because the
        // three fail differently. "Extra trips" on the ring card was a
        // column that could only ever read zero — extra trips are what
        // an answer costs, and the ring does not carry answers.
        {
            QString key = QStringLiteral("Extra trips");
            QString value = QString::number(area.extra_trips);
            const char* colour = Theme::Color::kFgDim;
            if (area.name.startsWith(QLatin1String("Command"))) {
                // What says the ring is too small. Not its depth: a
                // pipe that is empty is a pipe that is keeping up.
                key = QStringLiteral("Waits");
                value = QString::number(area.waits);
                colour = area.waits > 0 ? colour_for(area.state) : Theme::Color::kFgDim;
            } else if (area.name.startsWith(QLatin1String("Blob"))) {
                // Fragmentation. An arena with room to spare that
                // cannot find a long enough run is a different problem
                // from one that is simply too small.
                key = QStringLiteral("Blocks");
                value = area.measured ? QString::number(area.blocks)
                                      : QStringLiteral("\u2014");
                colour = area.measured ? Theme::Color::kFgDim : Theme::Color::kFgFaint;
            }
            row->addWidget(kpi(key, value, colour));
        }
        // A dash rather than "0 B": nothing has been measured is a
        // different statement from the largest thing was empty, and the
        // blob arena is carved in the guest so nothing here ever
        // measures it.
        row->addWidget(kpi(QStringLiteral("Largest"),
                           area.largest_bytes > 0 ? Format::bytes(area.largest_bytes)
                                                  : QStringLiteral("—"),
                           area.largest_bytes > 0 ? Theme::Color::kFgDim
                                                  : Theme::Color::kFgFaint));
        row->addWidget(kpi(QStringLiteral("Suggested"),
                           area.suggested_kib > 0 ? Format::kib(area.suggested_kib)
                                                  : QStringLiteral("—"),
                           area.suggested_kib > 0 ? Theme::Color::kAmber
                                                  : Theme::Color::kFgFaint));
        auto* grid_row = new QHBoxLayout;
        grid_row->addWidget(grid);
        grid_row->addStretch(1);
        body->addLayout(grid_row);

        const QString advice = area.advice();
        if (!advice.isEmpty()) {
            auto* advice_frame = new QFrame;
            Theme::surface(advice_frame, Theme::Role::kAdvice);
            auto* advice_row = new QHBoxLayout(advice_frame);
            advice_row->setContentsMargins(12, 9, 12, 9);
            advice_row->setSpacing(10);
            auto* text = Widgets::prose(advice);
            text->setProperty(Theme::Tone::kProperty, Theme::Tone::kWarm);
            advice_row->addWidget(text, 1);

            auto* stage = new QPushButton(
                QStringLiteral("Stage %1").arg(Format::kib(area.suggested_kib)));
            const QString key = key_for_area(area.name);
            const int value = key == K::kBlobMib
                                  ? static_cast<int>(area.suggested_kib / 1024)
                                  : static_cast<int>(area.suggested_kib);
            connect(stage, &QPushButton::clicked, this, [this, key, value] {
                pending_.stage(key, value);
                Q_EMIT settings_requested();
            });
            advice_row->addWidget(stage);
            body->addWidget(advice_frame);
        }

        areas_->addWidget(panel.frame);
    }
    areas_->addStretch(1);
}

}  // namespace lsw::host::manager
