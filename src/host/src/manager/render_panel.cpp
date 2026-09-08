#include "lsw/host/manager/render_panel.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::Theme;
using vm::AreaPressure;
using vm::ChannelPressure;
using K = config::ManagerSettings::Key;

// The channel strip is a fixed band: its cards are buttons with a
// layout inside and no text of their own, so their sizeHint is one
// line and they clip whatever the layout put in them.
constexpr int kCardHeight = 62;

QLabel* caption(const QString& text) {
    auto* label = new QLabel(text.toUpper());
    label->setObjectName(QString::fromLatin1(Theme::Role::kCaption));
    return label;
}

QLabel* meta(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kMeta));
    return label;
}

QLabel* prose(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(Theme::Role::kBody));
    label->setWordWrap(true);
    return label;
}

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

const char* pill_for(AreaPressure::State state) {
    switch (state) {
    case AreaPressure::State::Congested:
        return Theme::Role::kPillFail;
    case AreaPressure::State::Tight:
        return Theme::Role::kPillRun;
    case AreaPressure::State::Healthy:
        break;
    }
    return Theme::Role::kPillDone;
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

// How full an area is, as a percentage. Events are what is known when
// the server has published counters; a quiet area reads as its share of
// the region so the bar is not simply empty.
int area_fill(const AreaPressure& area) {
    if (area.state == AreaPressure::State::Congested) {
        return 96;
    }
    if (area.state == AreaPressure::State::Tight) {
        return 72;
    }
    return area.configured_kib == 0 ? 0 : 34;
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

QString size_label(quint64 kib) {
    if (kib >= 1024 && (kib % 1024) == 0) {
        return QStringLiteral("%1 MiB").arg(kib / 1024);
    }
    return QStringLiteral("%1 KiB").arg(kib);
}

QString bytes_label(quint64 bytes) {
    if (bytes >= 1024ULL * 1024ULL) {
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024) {
        return QStringLiteral("%1 KiB").arg(bytes / 1024);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

// A key/value tile. Several sit in a row under each area's meter.
QWidget* kpi(const QString& key, const QString& value, const char* colour) {
    auto* box = new QWidget;
    Theme::surface(box, Theme::Role::kTile);
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(11, 9, 11, 9);
    layout->setSpacing(3);
    layout->addWidget(caption(key));
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
        return QLatin1String(K::kRingKib);
    }
    if (name.startsWith(QLatin1String("Reply"))) {
        return QLatin1String(K::kReplyKib);
    }
    return QLatin1String(K::kBlobMib);
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
    auto* strip_holder = new QWidget;
    strip_holder->setFixedHeight(kCardHeight + 18);
    strip_ = new QHBoxLayout(strip_holder);
    strip_->setContentsMargins(14, 12, 14, 6);
    strip_->setSpacing(8);
    root->addWidget(strip_holder);

    summary_ = meta(QString{});
    summary_->setContentsMargins(14, 6, 14, 0);
    summary_->setWordWrap(true);
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
    const double mib = channel.bytes_per_second / (1024.0 * 1024.0);
    QString text = QStringLiteral("%1 cmd/s").arg(
        QLocale::system().toString(channel.commands_per_second, 'f', 0));
    if (mib >= 0.05) {
        text += QStringLiteral("  ·  %1 MiB/s").arg(mib, 0, 'f', 1);
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
        card->setFixedHeight(kCardHeight);

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

        auto* owner = meta(channel.claimed
                               ? (channel.owner_is_guest()
                                      ? QStringLiteral("guest pid %1").arg(channel.owner())
                                      : QStringLiteral("pid %1").arg(channel.owner()))
                               : QStringLiteral("free"));
        owner->setAttribute(Qt::WA_TransparentForMouseEvents);
        layout->addWidget(owner);

        // What it is carrying right now. A claimed channel that is
        // drawing nothing reads "idle" rather than a row of zeroes.
        auto* rate = meta(channel.claimed ? rate_line(channel) : QString{});
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

        connect(card, &QPushButton::clicked, this,
                [this, index = channel.index] { select(index); });
        strip_->addWidget(card);
    }
    // Adding one is a startup argument to the server, so it stages like
    // every other size on the page.
    auto* add = new QPushButton(QStringLiteral("+  Add a channel"));
    add->setObjectName(QString::fromLatin1(Theme::Role::kAddCard));
    add->setFixedHeight(kCardHeight);
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QPushButton::clicked, this, [this] {
        const int now = pending_.staged_or_current(QLatin1String(K::kChannels)).toInt();
        pending_.stage(QLatin1String(K::kChannels), qMin(8, now + 1));
        Q_EMIT settings_requested();
    });
    strip_->addWidget(add);
    strip_->addStretch(1);

    if (channels.isEmpty()) {
        return;
    }

    const auto& channel = channels.at(selected_);
    const int region_mib = settings_.int_value(QLatin1String(K::kBlobMib))
                           + settings_.int_value(QLatin1String(K::kRingKib)) / 1024
                           + settings_.int_value(QLatin1String(K::kReplyKib)) / 1024;
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
                prose(QStringLiteral("The process that claimed this channel is gone. "
                                     "Nothing else can take it until the claim is "
                                     "cleared.")),
                1);
        } else {
            layout->addStretch(1);
        }
        auto* own_sizes = new QPushButton(QStringLiteral("Give this channel its own sizes"));
        own_sizes->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
        connect(own_sizes, &QPushButton::clicked, this,
                &RenderPanel::settings_requested);
        layout->addWidget(own_sizes);

        auto* release = new QPushButton(QStringLiteral("Release it"));
        release->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
        release->setEnabled(stale);
        connect(release, &QPushButton::clicked, this, [this, index = channel.index] {
            (void)stats_.release_channel(region_prefix_, index);
        });
        layout->addWidget(release);
        areas_->addWidget(row);
    }

    for (const auto& area : channel.areas) {
        auto* frame = new QFrame;
        Theme::surface(frame, Theme::Role::kPanel);
        auto* outer = new QVBoxLayout(frame);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(0);

        auto* header = new QWidget;
        Theme::surface(header, Theme::Role::kPanelHeader);
        auto* header_layout = new QHBoxLayout(header);
        header_layout->setContentsMargins(12, 8, 12, 8);
        header_layout->setSpacing(10);
        auto* name = new QLabel(area.name);
        header_layout->addWidget(name);
        header_layout->addWidget(meta(size_label(area.configured_kib)));
        header_layout->addStretch(1);
        auto* state = new QLabel(QStringLiteral("\u25cf  %1").arg(word_for(area.state)));
        state->setObjectName(QString::fromLatin1(pill_for(area.state)));
        header_layout->addWidget(state);
        outer->addWidget(header);

        auto* body = new QVBoxLayout;
        body->setContentsMargins(12, 12, 12, 12);
        body->setSpacing(11);

        // How full it is. The one number that says whether the rest of
        // the row matters.
        auto* meter = new QProgressBar;
        meter->setObjectName(QString::fromLatin1(Theme::Role::kMeter));
        meter->setTextVisible(false);
        meter->setRange(0, 100);
        meter->setValue(area_fill(area));
        meter->setProperty("pressure", QString::fromLatin1(state_key(area.state)));
        body->addWidget(meter);

        auto* grid = new QFrame;
        Theme::surface(grid, Theme::Role::kKpiGrid);
        auto* row = new QHBoxLayout(grid);
        row->setContentsMargins(1, 1, 1, 1);
        row->setSpacing(1);
        row->addWidget(kpi(QStringLiteral("Events"), QString::number(area.events),
                           area.events > 0 ? colour_for(area.state) : Theme::Color::kFgDim));
        row->addWidget(kpi(QStringLiteral("Extra trips"),
                           QString::number(area.extra_trips), Theme::Color::kFgDim));
        row->addWidget(kpi(QStringLiteral("Largest"), bytes_label(area.largest_bytes),
                           Theme::Color::kFgDim));
        row->addWidget(kpi(QStringLiteral("Suggested"),
                           area.suggested_kib > 0 ? size_label(area.suggested_kib)
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
            auto* text = prose(advice);
            text->setStyleSheet(QStringLiteral("color: #f0c393;"));
            advice_row->addWidget(text, 1);

            auto* stage = new QPushButton(
                QStringLiteral("Stage %1").arg(size_label(area.suggested_kib)));
            const QString key = key_for_area(area.name);
            const int value = key == QLatin1String(K::kBlobMib)
                                  ? static_cast<int>(area.suggested_kib / 1024)
                                  : static_cast<int>(area.suggested_kib);
            connect(stage, &QPushButton::clicked, this, [this, key, value] {
                pending_.stage(key, value);
                Q_EMIT settings_requested();
            });
            advice_row->addWidget(stage);
            body->addWidget(advice_frame);
        }

        outer->addLayout(body);
        areas_->addWidget(frame);
    }
    areas_->addStretch(1);
}

}  // namespace lsw::host::manager
