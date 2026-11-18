#include "lsw/host/manager/display_page.hpp"

#include "lsw/host/config/display_modes.hpp"
#include "lsw/host/config/display_refresh.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/manager/display_facts.hpp"
#include "lsw/host/manager/resolution_editor.hpp"
#include "lsw/host/manager/screen_pixels.hpp"
#include "lsw/host/manager/setting_controls.hpp"
#include "lsw/host/platform/host_scale.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/info_tip.hpp"
#include "lsw/host/ui/key_values.hpp"
#include "lsw/host/ui/monitor_sketch.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QCursor>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QScreen>
#include <QWindow>
#include <QVBoxLayout>

#include <QPair>

#include <algorithm>
#include <cmath>

namespace lsw::host::manager {

namespace {

using config::DisplayMode;
using config::DisplayModes;
using config::DisplayRefresh;
using ui::KeyValues;
using ui::SegmentedControl;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using K = config::ManagerSettings::Key;
using Area = services::StatusHub::Area;

constexpr int kMostRateButtons = 6;
constexpr int kSketchColumn = 236;
constexpr int kFixedScales[] = {100, 125, 150, 175, 200};
const QString kMonitorValue = QString::fromLatin1(DisplayRefresh::kMonitor);
const QString kOtherValue = QStringLiteral("other");

QString size_label(const std::optional<QSize>& size) {
    return size ? DisplayMode{size->width(), size->height()}.label() : QStringLiteral("-");
}

QString scale_text(qreal ratio) {
    return QString::number(ratio, 'g', 3);
}

QWidget* toggle_line(ui::ToggleSwitch* toggle, const QString& text, const QString& about) {
    auto* line = new QWidget;
    auto* row = new QHBoxLayout(line);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(9);
    row->addWidget(toggle);
    row->addWidget(new QLabel(text));
    row->addWidget(new ui::InfoTip(about));
    row->addStretch(1);
    return line;
}

}

DisplayPage::DisplayPage(services::StatusHub& status, config::ManagerSettings& settings,
                         config::PendingChanges& pending, QWidget* parent)
    : ui::ScrollPage(parent), status_(status), settings_(settings),
      controls_(new SettingControls(settings, pending, this)) {
    build_now();
    build_resolutions();
    build_one_to_one();
    build_refresh_and_scale();
    column()->addStretch(1);

    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Graphics || area == Area::Vm) {
            refresh();
        }
    });
    connect(&settings_, &config::ManagerSettings::changed, this, [this](const QString& key) {
        if (key == K::kDisplayModes || key == K::kDisplayRefresh || key == K::kScalingPercent ||
            key == K::kMatchHostOutput) {
            refresh();
        }
    });
    refresh();
}

void DisplayPage::showEvent(QShowEvent* event) {
    ui::ScrollPage::showEvent(event);
    refresh();
}

QString DisplayPage::summary() const {
    return DisplayFacts::read(status_).summary();
}

void DisplayPage::refresh() {
    refresh_now();
    refresh_resolutions();
    refresh_refresh_and_scale();
    Q_EMIT summary_changed();
}

void DisplayPage::build_now() {
    auto panel = ui::Panel::area(
        QStringLiteral("Current display mode"),
        QStringLiteral("The resolution, refresh rate and scale Windows is using right now, as the "
                       "guest's display device reports them."));
    live_pill_ = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Guest off"));
    panel.header->addWidget(live_pill_);

    auto* row = new QHBoxLayout;
    row->setSpacing(20);
    sketch_ = new ui::MonitorSketch;
    auto* sketch_cell = new QWidget;
    sketch_cell->setFixedWidth(kSketchColumn);
    auto* sketch_layout = new QVBoxLayout(sketch_cell);
    sketch_layout->setContentsMargins(0, 0, 0, 0);
    sketch_layout->addWidget(sketch_, 0, Qt::AlignCenter);
    row->addWidget(sketch_cell);
    facts_ = new QWidget;
    facts_box_ = new QVBoxLayout(facts_);
    facts_box_->setContentsMargins(0, 0, 0, 0);
    row->addWidget(facts_, 1, Qt::AlignVCenter);
    panel.body->addLayout(row);
    column()->addWidget(panel.frame);
}

void DisplayPage::refresh_now() {
    const DisplayFacts facts = DisplayFacts::read(status_);
    const bool running = status_.vm().running();
    StatusPill::set(live_pill_, running ? StatusPill::State::Done : StatusPill::State::Pending,
                    running ? QStringLiteral("Live") : QStringLiteral("Guest off"));

    const QScreen* screen = window() != nullptr ? window()->screen() : nullptr;
    const ScreenPixels pixels(status_);
    const qreal window_scale = window() != nullptr ? window()->devicePixelRatioF() : 1.0;
    const QString mode = facts.mode_label();
    const QString refresh = facts.refresh_label();
    QString detail = QStringLiteral("-");
    if (facts.mode) {
        detail = QStringLiteral("%1 · %2").arg(facts.mode->aspect(), refresh);
    }
    sketch_->show_mode(facts.mode ? facts.mode->width : 0, facts.mode ? facts.mode->height : 0,
                       mode, detail);

    const bool follows = !DisplayRefresh::fixed(settings_.string_value(K::kDisplayRefresh));
    const int scale_setting = settings_.int_value(K::kScalingPercent);
    QString window_name = QStringLiteral("-");
    if (screen != nullptr) {
        QStringList parts{size_label(pixels.of(screen, window()->windowHandle())),
                          DisplayRefresh::label(DisplayRefresh::from_hertz(screen->refreshRate())),
                          QStringLiteral("scale %1").arg(scale_text(window_scale))};
        if (!screen->name().isEmpty()) {
            parts.prepend(screen->name());
        }
        window_name = parts.join(QStringLiteral(" · "));
    }
    const QStringList signature{
        mode,
        QString::number(facts.offered),
        refresh,
        facts.scale_label(),
        follows ? QStringLiteral("monitor") : QStringLiteral("fixed"),
        QString::number(scale_setting),
        window_name,
        running ? QStringLiteral("running") : QStringLiteral("off"),
    };
    if (signature == facts_signature_) {
        return;
    }
    facts_signature_ = signature;
    while (QLayoutItem* item = facts_box_->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    auto* holder = new QWidget;
    auto* column_layout = new QVBoxLayout(holder);
    column_layout->setContentsMargins(0, 0, 0, 0);
    KeyValues rows(column_layout);
    rows.add(QStringLiteral("Resolution"), mode, nullptr,
             facts.mode && facts.offered > 0
                 ? QStringLiteral("The preferred one of the %1 the guest's monitor offers.").arg(facts.offered)
                 : (facts.host_modes ? QStringLiteral("The connected monitors decide.")
                                     : QStringLiteral("The size Windows runs at, read from the running device.")));
    rows.add(QStringLiteral("Refresh"), refresh, nullptr,
             follows ? QStringLiteral("Follows the monitor this window is on.")
                     : QStringLiteral("Fixed on the Refresh row below."));
    rows.add(QStringLiteral("Scale"), facts.scale_label(), nullptr,
             scale_setting == 0 ? QStringLiteral("Matches this desktop, now %1.").arg(scale_text(window_scale))
                                : QStringLiteral("Fixed on the Scale row below."));
    rows.add(QStringLiteral("Manager's monitor"), window_name, nullptr,
             QStringLiteral("The monitor this manager window is on: its name, size, refresh rate and "
                            "scale. Same as monitor and Match this desktop follow it."));
    facts_box_->addWidget(holder);
}

void DisplayPage::build_resolutions() {
    auto panel = ui::Panel::area(QStringLiteral("Resolutions"),
                                 QStringLiteral("The sizes the guest's virtual monitor offers Windows to "
                                                "choose from."));
    panel.header->addWidget(Widgets::tag(false));
    panel.body->setSpacing(11);

    source_ = new SegmentedControl;
    source_->set_options({{QStringLiteral("This list"), {}, QStringLiteral("list")},
                          {QStringLiteral("The host monitors' own modes"), {},
                           QStringLiteral("host")}});
    panel.body->addWidget(source_);
    panel.body->addWidget(toggle_line(
        controls_->toggle(K::kMatchHostOutput), QStringLiteral("Follow this window's monitor"),
        QStringLiteral("Makes the size of the monitor this manager window is on the preferred one, "
                       "and follows the window when it moves to another monitor.")));

    list_box_ = new QWidget;
    auto* list_column = new QVBoxLayout(list_box_);
    list_column->setContentsMargins(0, 0, 0, 0);
    list_column->setSpacing(11);
    editor_ = new ResolutionEditor(settings_, status_);
    list_column->addWidget(editor_);
    panel.explain(QStringLiteral("Windows uses the preferred size, and no window can be larger. Sizes "
                                 "from %1 × %1 to %2 × %2. Changes reach Windows at once.")
                      .arg(DisplayModes::kMinEdge)
                      .arg(DisplayModes::kMaxEdge));
    panel.explain(QStringLiteral("With the host monitors' own modes the list is kept for when you "
                                 "switch back."));
    panel.body->addWidget(list_box_);

    host_box_ = new QWidget;
    host_column_ = new QVBoxLayout(host_box_);
    host_column_->setContentsMargins(0, 0, 0, 0);
    host_column_->setSpacing(11);
    panel.body->addWidget(host_box_);

    connect(source_, &SegmentedControl::chosen, this, [this](const QVariant& value) {
        editor_->use_host_modes(value.toString() == QLatin1String("host"));
    });
    column()->addWidget(panel.frame);
}

void DisplayPage::build_one_to_one() {
    auto panel = ui::Panel::area(
        QStringLiteral("Desktop mirroring"),
        QStringLiteral("While the desktop window is open, Windows uses exactly its size in pixels and "
                       "follows it when it is resized. Closing it brings the resolutions back."));
    panel.header->addWidget(Widgets::tag(config::ManagerSettings::needs_restart(K::kOneToOneDesktop)));
    auto* line = new QHBoxLayout;
    line->setSpacing(9);
    line->addWidget(controls_->toggle(K::kOneToOneDesktop));
    line->addWidget(new QLabel(QStringLiteral("Match the desktop window in pixels")));
    line->addStretch(1);
    panel.body->addLayout(line);
    column()->addWidget(panel.frame);
}

void DisplayPage::refresh_resolutions() {
    const bool host =
        DisplayModes::normalized(settings_.string_list_value(K::kDisplayModes)).isEmpty();
    source_->set_current(host ? QStringLiteral("host") : QStringLiteral("list"));
    list_box_->setVisible(!host);
    host_box_->setVisible(host);
    if (!host) {
        return;
    }
    if (host_facts_ != nullptr) {
        host_facts_->hide();
        host_facts_->deleteLater();
    }
    host_facts_ = new QWidget;
    auto* column_layout = new QVBoxLayout(host_facts_);
    column_layout->setContentsMargins(0, 0, 0, 0);
    column_layout->setSpacing(11);
    KeyValues rows(column_layout);
    auto screens = QGuiApplication::screens();
    const ScreenPixels pixels(status_);
    const QWindow* handle = window() != nullptr ? window()->windowHandle() : nullptr;
    const auto area = [&](const QScreen* screen) {
        const auto size = pixels.of(screen, handle);
        return size ? static_cast<qint64>(size->width()) * size->height() : qint64{0};
    };
    std::stable_sort(screens.begin(), screens.end(), [&area](const QScreen* a, const QScreen* b) {
        return area(a) > area(b);
    });
    QScreen* current = window() != nullptr ? window()->screen() : nullptr;
    if (settings_.bool_value(K::kMatchHostOutput) && current != nullptr && screens.contains(current)) {
        screens.removeOne(current);
        screens.prepend(current);
    }
    const QScreen* largest = screens.isEmpty() ? nullptr : screens.first();
    for (const QScreen* screen : screens) {
        rows.add(screen->name().isEmpty() ? QStringLiteral("Monitor") : screen->name(),
                 size_label(pixels.of(screen, handle)), nullptr,
                 screen == largest ? (settings_.bool_value(K::kMatchHostOutput)
                                          ? QStringLiteral("Preferred: the monitor this window is on.")
                                          : QStringLiteral("Preferred: the largest monitor's best mode."))
                                   : QString{});
    }
    if (screens.isEmpty()) {
        rows.add(QStringLiteral("Monitors"), QStringLiteral("-"));
    }
    host_column_->addWidget(host_facts_);
}

void DisplayPage::build_refresh_and_scale() {
    auto panel = ui::Panel::area(
        QStringLiteral("Refresh and scale"),
        QStringLiteral("How often Windows redraws, and how large it draws text and controls. Changes "
                       "apply at once."));
    panel.header->addWidget(Widgets::tag(false));
    panel.body->setSpacing(11);

    auto* rows_widget = new QWidget;
    auto* rows_column = new QVBoxLayout(rows_widget);
    rows_column->setContentsMargins(0, 0, 0, 0);
    KeyValues rows(rows_column);

    refresh_seg_ = new SegmentedControl;
    connect(refresh_seg_, &SegmentedControl::chosen, this,
            [this](const QVariant& value) { choose_refresh(value, hidden_rates_); });
    rows.add_widget(QStringLiteral("Refresh"), refresh_seg_);

    scale_seg_ = controls_->segmented(K::kScalingPercent, {});
    rows.add_widget(QStringLiteral("Scale"), scale_seg_);
    panel.body->addWidget(rows_widget);

    refresh_tip_ = panel.info;
    column()->addWidget(panel.frame);
}

QVector<DisplayPage::Rate> DisplayPage::rates() const {
    QVector<Rate> out;
    const auto add = [&out](quint32 millihz, const QStringList& monitors) {
        const auto same = std::find_if(out.begin(), out.end(), [millihz](const Rate& rate) {
            return rate.millihz == millihz;
        });
        if (same == out.end()) {
            out.push_back({static_cast<int>(std::lround(millihz / 1000.0)), millihz, {}, monitors});
            return;
        }
        for (const QString& monitor : monitors) {
            if (!same->monitors.contains(monitor)) {
                same->monitors << monitor;
            }
        }
    };
    for (const auto& choice : status_.graphics().monitor_rates) {
        add(static_cast<quint32>(choice.millihz), choice.monitors);
    }
    if (const auto stored = DisplayRefresh::fixed(settings_.string_value(K::kDisplayRefresh))) {
        add(*stored, {});
    }
    std::sort(out.begin(), out.end(),
              [](const Rate& a, const Rate& b) { return a.millihz > b.millihz; });
    for (Rate& rate : out) {
        const bool shared = std::count_if(out.begin(), out.end(), [&rate](const Rate& other) {
                                return other.hertz == rate.hertz;
                            }) > 1;
        rate.label = shared ? DisplayRefresh::label(rate.millihz)
                            : QStringLiteral("%1 Hz").arg(rate.hertz);
    }
    return out;
}

QString DisplayPage::refresh_note(const QVector<Rate>& rates, qsizetype shown) const {
    QString text = QStringLiteral(
        "Same as monitor and Match this desktop follow the screen this window is on. A higher "
        "rate is more GPU work on the host");
    QStringList all;
    for (const auto& rate : rates) {
        for (const QString& monitor : rate.monitors) {
            if (!all.contains(monitor)) {
                all << monitor;
            }
        }
    }
    QVector<QPair<QStringList, QStringList>> groups;
    for (qsizetype i = 0; i < std::min(shown, rates.size()); ++i) {
        const Rate& rate = rates.at(i);
        if (rate.monitors.isEmpty()) {
            continue;
        }
        QStringList monitors = rate.monitors;
        monitors.sort();
        const auto same = std::find_if(groups.begin(), groups.end(), [&monitors](const auto& g) {
            return g.first == monitors;
        });
        const QString hertz = rate.label.chopped(3);
        if (same != groups.end()) {
            same->second << hertz;
        } else {
            groups.push_back({monitors, {hertz}});
        }
    }
    const auto join = [](const QStringList& items) {
        return items.size() < 2
                   ? items.join(QString{})
                   : items.mid(0, items.size() - 1).join(QStringLiteral(", ")) +
                         QStringLiteral(" and ") + items.last();
    };
    QStringList clauses;
    for (const auto& [monitors, hertz] : groups) {
        const bool everywhere = monitors.size() == all.size();
        if (everywhere && all.size() < 2) {
            continue;
        }
        clauses << QStringLiteral("%1 Hz %2 %3")
                       .arg(join(hertz),
                            hertz.size() == 1 ? QStringLiteral("is") : QStringLiteral("are"),
                            everywhere ? QStringLiteral("on ") + join(monitors)
                                       : join(monitors) + QStringLiteral(" only"));
    }
    if (!clauses.isEmpty()) {
        text += QStringLiteral("; ") + clauses.join(QStringLiteral("; "));
    }
    return text + QStringLiteral(". The scale is the nearest step Windows has, and mirrored windows "
                                 "stay one guest pixel to one screen pixel.");
}

void DisplayPage::refresh_refresh_and_scale() {
    const auto all = rates();
    const auto stored = DisplayRefresh::fixed(settings_.string_value(K::kDisplayRefresh));
    const qsizetype shown_count = std::min<qsizetype>(all.size(), kMostRateButtons);
    hidden_rates_ = all.mid(shown_count);

    QVector<SegmentedControl::Option> options;
    options.push_back({QStringLiteral("Same as monitor"),
                       DisplayRefresh::label(status_.graphics().monitor_refresh_millihz),
                       kMonitorValue});
    QVariant current = kMonitorValue;
    for (qsizetype i = 0; i < shown_count; ++i) {
        const Rate& rate = all.at(i);
        options.push_back({rate.label, {}, DisplayRefresh::text(rate.millihz)});
        if (stored && rate.millihz == *stored) {
            current = DisplayRefresh::text(rate.millihz);
        }
    }
    if (!hidden_rates_.isEmpty()) {
        QString other_label;
        for (const Rate& rate : std::as_const(hidden_rates_)) {
            if (stored && rate.millihz == *stored) {
                other_label = rate.label;
            }
        }
        const bool other = !other_label.isEmpty();
        options.push_back({QStringLiteral("Other"), other_label, kOtherValue});
        if (other) {
            current = kOtherValue;
        }
    }
    refresh_seg_->set_options(options);
    refresh_seg_->set_current(current);
    refresh_tip_->set_text(QStringLiteral("How often Windows redraws, and how large it draws text "
                                          "and controls. Changes apply at once.<br><br>") +
                           refresh_note(all, shown_count));

    QVector<SegmentedControl::Option> scales;
    scales.push_back({QStringLiteral("Match this desktop"),
                      QStringLiteral("%1 %").arg(platform::HostScale::resolve(0, window())),
                      0});
    for (const int percent : kFixedScales) {
        scales.push_back({QStringLiteral("%1 %").arg(percent), {}, percent});
    }
    scale_seg_->set_options(scales);
    scale_seg_->set_current(settings_.int_value(K::kScalingPercent));
}

void DisplayPage::choose_refresh(const QVariant& value, const QVector<Rate>& hidden) {
    if (value.toString() == kOtherValue) {
        QMenu menu(this);
        for (const Rate& rate : hidden) {
            QAction* action = menu.addAction(rate.label);
            action->setData(DisplayRefresh::text(rate.millihz));
        }
        if (const QAction* picked = menu.exec(QCursor::pos())) {
            controls_->put(K::kDisplayRefresh, picked->data());
        }
        refresh_refresh_and_scale();
        return;
    }
    controls_->put(K::kDisplayRefresh, value);
}

}
