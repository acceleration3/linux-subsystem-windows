#include "lsw/host/manager/gpu_status_page.hpp"

#include "lsw/host/manager/gpu_status_alerts.hpp"
#include "lsw/host/manager/warning_strip.hpp"
#include "lsw/host/services/host_window_alerts.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/ui/key_values.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/status_pill.hpp"
#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/usage_graph.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using ui::KeyValues;
using ui::StatusPill;
using ui::Theme;
using ui::Widgets;
using Adapter = services::GraphicsAdapter;
using Warning = GpuStatusAlerts::Warning;
using Area = services::StatusHub::Area;

constexpr int kLogLines = 6;

QString short_name(const QString& name) {
    static const QStringList vendor_words = {
        QStringLiteral("AMD"),    QStringLiteral("Radeon"),   QStringLiteral("Intel"),
        QStringLiteral("Intel(R)"), QStringLiteral("NVIDIA"), QStringLiteral("GeForce"),
        QStringLiteral("Graphics"), QStringLiteral("(TM)"),   QStringLiteral("Mesa")};
    QStringList words = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    words.removeIf([](const QString& w) {
        return vendor_words.contains(w) || w == QLatin1StringView("Processor")
               || w.endsWith(QLatin1StringView("-Core"));
    });
    const qsizetype bracket = words.indexOf(QRegularExpression(QStringLiteral("^\\(.*")));
    if (bracket > 0) {
        words = words.mid(0, bracket);
    }
    QString out = words.isEmpty() ? name : words.join(QLatin1Char(' '));
    constexpr qsizetype kMost = 12;
    if (out.size() > kMost) {
        out = out.left(kMost - 1) + QStringLiteral("…");
    }
    return out;
}

ui::Panel card(const QString& name, const QString& caption, QWidget* right = nullptr) {
    auto panel = ui::Panel::area(name, caption);
    if (right != nullptr) {
        panel.header->addWidget(right);
    }
    return panel;
}

QLabel* adapter_pill(const services::StatusHub& status, const Adapter& adapter) {
    if (!status.vm().running()) {
        return StatusPill::make(StatusPill::State::Pending, QStringLiteral("Guest off"));
    }
    if (!status.graphics().guest.valid) {
        return StatusPill::make(StatusPill::State::Pending, QStringLiteral("Waiting for the agent"));
    }
    if (!adapter.guest) {
        return StatusPill::make(StatusPill::State::Failed, QStringLiteral("Not in Windows"));
    }
    if (adapter.guest->running()) {
        return StatusPill::make(StatusPill::State::Done, QStringLiteral("Started"));
    }
    if (adapter.guest->no_driver()) {
        return StatusPill::make(StatusPill::State::Warning, QStringLiteral("No driver"));
    }
    return StatusPill::make(StatusPill::State::Failed,
                            QStringLiteral("Problem code %1").arg(adapter.guest->problem_code));
}

}

GpuStatusPage::GpuStatusPage(services::StatusHub& status, GpuStatusAlerts& alerts, QWidget* parent)
    : QWidget(parent), status_(status), alerts_(alerts) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* holder = new QWidget;
    content_ = new QVBoxLayout(holder);
    content_->setContentsMargins(14, 14, 14, 14);
    content_->setSpacing(12);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    connect(&status_, &services::StatusHub::changed, this, [this](Area area) {
        if (area == Area::Graphics) {
            refresh();
        } else if (area == Area::HostWindow) {
            update_window();
        }
    });
    connect(&alerts_, &GpuStatusAlerts::changed, this, &GpuStatusPage::refresh);
    rebuild();
}

void GpuStatusPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    status_.set_detail(Area::Graphics, true);
}

void GpuStatusPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    status_.set_detail(Area::Graphics, false);
}

QString GpuStatusPage::summary() const {
    const auto& adapters = status_.graphics().adapters;
    if (!status_.vm().running()) {
        return QStringLiteral("guest off");
    }
    if (!status_.graphics().guest.valid) {
        return QStringLiteral("waiting for the agent");
    }
    int started = 0;
    for (const auto& adapter : adapters) {
        started += adapter.guest && adapter.guest->running() ? 1 : 0;
    }
    return QStringLiteral("avgpu.sys · %1 of %2 adapters started").arg(started).arg(adapters.size());
}

void GpuStatusPage::refresh() {
    if (signature() != built_for_) {
        rebuild();
        Q_EMIT summary_changed();
    }
    update_log();
}

QString GpuStatusPage::signature() const {
    QStringList parts;
    const auto& g = status_.graphics();
    for (const auto& adapter : g.adapters) {
        parts << adapter.gpu.uuid << QString::number(adapter.display);
        if (adapter.guest) {
            parts << adapter.guest->name << QString::number(adapter.guest->problem_code)
                  << QString::number(adapter.guest->started) << adapter.guest->driver_version;
        }
    }
    for (const auto& warning : alerts_.warnings()) {
        parts << warning.headline << warning.detail;
    }
    const auto& guest = g.guest;
    const auto& facts = g.log_facts;
    parts << QString::number(status_.vm().running()) << QString::number(guest.valid)
          << QString::number(guest.test_signing) << QString::number(guest.driver_search_off)
          << QString::number(facts.user_mode_submission.value_or(false))
          << QString::number(facts.user_mode_submission.has_value())
          << QString::number(facts.native_fences.value_or(false))
          << QString::number(facts.native_fences.has_value())
          << QString::number(guest.user_mode_submission.value_or(false))
          << QString::number(guest.user_mode_submission.has_value())
          << QString::number(guest.native_fences.value_or(false))
          << QString::number(guest.native_fences.has_value());
    return parts.join(QLatin1Char('\x1f'));
}

void GpuStatusPage::rebuild() {
    built_for_ = signature();
    while (auto* item = content_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    log_meta_ = nullptr;
    log_text_ = nullptr;
    graph_ = nullptr;
    window_use_ = nullptr;
    window_peak_ = nullptr;
    holders_box_ = nullptr;
    transfers_box_ = nullptr;

    if (auto* warnings = build_warnings()) {
        content_->addWidget(warnings);
    }
    content_->addWidget(build_adapters());
    content_->addWidget(build_guest_driver());
    content_->addWidget(build_host_window());
    content_->addWidget(build_transfers());
    content_->addWidget(build_log());
    content_->addStretch(1);
    update_log();
    update_window();
}

QWidget* GpuStatusPage::build_warnings() {
    const auto warnings = alerts_.warnings();
    if (warnings.isEmpty()) {
        return nullptr;
    }
    auto* box = new QFrame;
    Theme::surface(box, Theme::Role::kPanel);
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    for (const auto& warning : warnings) {
        auto* strip = new WarningStrip;
        strip->set_message(warning.critical, warning.headline, warning.detail);
        switch (warning.kind) {
        case Warning::Kind::DisplayNotRunning:
        case Warning::Kind::DisplayNoDriver:
            connect(strip->add_button(QStringLiteral("Open the driver log")),
                    &QPushButton::clicked, this, &GpuStatusPage::open_log);
            break;
        case Warning::Kind::NoDriver:
        case Warning::Kind::WindowFull:
        case Warning::Kind::Refusals:
        case Warning::Kind::SplitTransfers:
        case Warning::Kind::CopiedMappings:
        case Warning::Kind::ArenaFull:
            break;
        }
        column->addWidget(strip);
    }
    return box;
}

QWidget* GpuStatusPage::build_adapters() const {
    const auto& adapters = status_.graphics().adapters;
    auto panel = card(QStringLiteral("Adapters"), QStringLiteral("what Windows lists"));
    if (adapters.isEmpty()) {
        panel.body->addWidget(Widgets::meta(QStringLiteral("-")));
        return panel.frame;
    }
    auto* table = new QTableWidget(static_cast<int>(adapters.size()), 3);
    Theme::rows_table(table, {QStringLiteral("ADAPTER"), QStringLiteral("ROLE"),
                              QStringLiteral("IN WINDOWS")});
    auto* head = table->horizontalHeader();
    head->setSectionResizeMode(0, QHeaderView::Stretch);
    head->setSectionResizeMode(1, QHeaderView::Fixed);
    head->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(1, 112);
    table->setColumnWidth(2, 148);
    table->verticalHeader()->setDefaultSectionSize(34);
    for (int row = 0; row < adapters.size(); ++row) {
        const auto& adapter = adapters.at(row);
        table->setItem(row, 0, new QTableWidgetItem(platform::HostGpus::adapter_name(adapter.gpu)));
        table->setItem(row, 1,
                       new QTableWidgetItem(adapter.display ? QStringLiteral("Display")
                                                            : QStringLiteral("Render only")));
        auto* holder = new QWidget;
        auto* line = new QHBoxLayout(holder);
        line->setContentsMargins(8, 0, 8, 0);
        QLabel* pill = adapter_pill(status_, adapter);
        pill->ensurePolished();
        pill->setMinimumWidth(pill->sizeHint().width());
        line->addWidget(pill, 0, Qt::AlignLeft | Qt::AlignVCenter);
        line->addStretch(1);
        table->setCellWidget(row, 2, holder);
    }
    Theme::fit_rows(table);
    panel.body->addWidget(table);
    return panel.frame;
}

QWidget* GpuStatusPage::build_guest_driver() const {
    const auto adapters = status_.graphics().adapters;
    const auto& guest = status_.graphics().guest;
    QString version;
    if (!adapters.isEmpty() && adapters.first().guest) {
        version = adapters.first().guest->driver_version;
    }
    int started = 0;
    for (const auto& adapter : adapters) {
        started += adapter.guest && adapter.guest->running() ? 1 : 0;
    }
    QLabel* pill = nullptr;
    if (!status_.vm().running()) {
        pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Guest off"));
    } else if (!guest.valid) {
        pill = StatusPill::make(StatusPill::State::Pending, QStringLiteral("Unknown"));
    } else if (started == adapters.size() && started > 0) {
        pill = StatusPill::make(StatusPill::State::Done, QStringLiteral("Started"));
    } else if (started == 0) {
        pill = StatusPill::make(StatusPill::State::Failed, QStringLiteral("Not running"));
    } else {
        pill = StatusPill::make(StatusPill::State::Warning,
                                QStringLiteral("%1 of %2").arg(started).arg(adapters.size()));
    }
    auto panel = card(QStringLiteral("Guest driver"),
                      version.isEmpty() ? QStringLiteral("avgpu.sys")
                                        : QStringLiteral("avgpu.sys %1").arg(version),
                      pill);
    KeyValues rows(panel.body);

    const bool known = status_.vm().running() && guest.valid;
    const QString off_note = status_.vm().running() ? QStringLiteral("the agent has not answered")
                                              : QStringLiteral("the guest is off");
    for (const auto& adapter : adapters) {
        const QString key = adapters.size() == 1
                                ? QStringLiteral("Device")
                                : short_name(platform::HostGpus::adapter_name(adapter.gpu));
        if (!known) {
            rows.add(key, QStringLiteral("-"), Theme::Tone::kFaint, off_note);
        } else if (!adapter.guest) {
            rows.add(key, QStringLiteral("Absent"), Theme::Tone::kBad,
                     QStringLiteral("Windows lists no adapter for it"));
        } else if (adapter.guest->running()) {
            rows.add(key, QStringLiteral("Running"), Theme::Tone::kOk,
                     QStringLiteral("no problem code"));
        } else if (adapter.guest->no_driver()) {
            rows.add(key, QStringLiteral("No driver"), Theme::Tone::kAmber,
                     QStringLiteral("problem code %1").arg(adapter.guest->problem_code));
        } else {
            rows.add(key, QStringLiteral("Problem"), Theme::Tone::kBad,
                     QStringLiteral("problem code %1").arg(adapter.guest->problem_code));
        }
    }
    if (!known) {
        rows.add(QStringLiteral("Test signing"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("the driver needs it"));
        rows.add(QStringLiteral("Driver search"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("must be off"));
    } else {
        rows.add(QStringLiteral("Test signing"),
                 guest.test_signing ? QStringLiteral("On") : QStringLiteral("Off"),
                 guest.test_signing ? Theme::Tone::kOk : Theme::Tone::kBad,
                 QStringLiteral("the driver needs it"));
        rows.add(QStringLiteral("Driver search"),
                 guest.driver_search_off ? QStringLiteral("Off") : QStringLiteral("On"),
                 guest.driver_search_off ? Theme::Tone::kOk : Theme::Tone::kBad,
                 guest.driver_search_off ? QStringLiteral("Update cannot replace it")
                                         : QStringLiteral("Update may replace it"));
    }
    const auto& facts = status_.graphics().log_facts;
    const std::optional<bool> ums =
        facts.user_mode_submission.has_value() ? facts.user_mode_submission
                                               : guest.user_mode_submission;
    const std::optional<bool> fences =
        facts.native_fences.has_value() ? facts.native_fences : guest.native_fences;
    if (!ums.has_value()) {
        rows.add(QStringLiteral("Submission"), QStringLiteral("-"), Theme::Tone::kFaint,
                 QStringLiteral("not reported yet"));
    } else {
        rows.add(QStringLiteral("Submission"),
                 *ums ? QStringLiteral("User mode") : QStringLiteral("Kernel"), nullptr,
                 fences.has_value()
                     ? (*fences ? QStringLiteral("native fences on")
                                : QStringLiteral("native fences off"))
                     : QString{});
    }
    panel.body->addStretch(1);
    return panel.frame;
}

QWidget* GpuStatusPage::build_host_window() {
    auto panel = card(QStringLiteral("Host window"), QStringLiteral("last 10 minutes"));
    graph_ = new ui::UsageGraph;
    panel.body->addWidget(graph_);
    auto* legend = Widgets::meta(
        QStringLiteral("<span style=\"color:%1\">&#9632;</span> write-back &nbsp; "
                       "<span style=\"color:%2\">&#9632;</span> write-combined &nbsp; "
                       "<span style=\"color:%3\">- -</span> window size")
            .arg(QString::fromLatin1(Theme::Color::kInfo), QString::fromLatin1(Theme::Color::kAmber),
                 QString::fromLatin1(Theme::Color::kFgDim)));
    legend->setTextFormat(Qt::RichText);
    panel.body->addWidget(legend);
    KeyValues rows(panel.body);
    window_use_ = qobject_cast<QLabel*>(
        rows.add(QStringLiteral("In use"), QStringLiteral("-"))->itemAt(0)->widget());
    window_peak_ = qobject_cast<QLabel*>(
        rows.add(QStringLiteral("Peak"), QStringLiteral("-"))->itemAt(0)->widget());
    panel.body->addWidget(Widgets::caption(QStringLiteral("Held most by")));
    auto* holders = new QWidget;
    holders_box_ = new QVBoxLayout(holders);
    holders_box_->setContentsMargins(0, 0, 0, 0);
    panel.body->addWidget(holders);
    return panel.frame;
}

QWidget* GpuStatusPage::build_transfers() {
    auto panel = card(QStringLiteral("Transfers"),
                      QStringLiteral("rates per second, counts since the app started"));
    auto* holder = new QWidget;
    transfers_box_ = new QVBoxLayout(holder);
    transfers_box_->setContentsMargins(0, 0, 0, 0);
    panel.body->addWidget(holder);
    return panel.frame;
}

void GpuStatusPage::update_window() {
    if (graph_ == nullptr) {
        return;
    }
    const auto& w = status_.host_window();
    QVector<ui::UsageGraph::Point> points;
    for (const auto& sample : w.history) {
        points.push_back({sample.at_ms, sample.write_back, sample.write_back + sample.write_combined});
    }
    graph_->set_series(points, w.window_bytes, w.at_ms, services::HostWindowStatus::kHistoryMs);
    const QString window = w.window_bytes ? ui::Format::bytes(*w.window_bytes) : QStringLiteral("-");
    window_use_->setText(w.read ? QStringLiteral("%1 of %2").arg(ui::Format::bytes(w.used_bytes), window)
                                : QStringLiteral("-"));
    window_peak_->setText(w.peak_bytes ? ui::Format::bytes(*w.peak_bytes) : QStringLiteral("-"));
    fill_holders();
    fill_transfers();
}

void GpuStatusPage::fill_holders() {
    constexpr int kRows = 6;
    while (auto* item = holders_box_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto& w = status_.host_window();
    if (!w.read || w.holders.isEmpty()) {
        holders_box_->addWidget(Widgets::meta(w.read ? QStringLiteral("Nothing is held") : QStringLiteral("-")));
        return;
    }
    const int rows = static_cast<int>(qMin<qsizetype>(w.holders.size(), kRows));
    auto* table = new QTableWidget(rows, 3);
    Theme::rows_table(table, {QStringLiteral("APP"), QStringLiteral("HELD"), QStringLiteral("BLOBS")});
    auto* head = table->horizontalHeader();
    head->setSectionResizeMode(0, QHeaderView::Stretch);
    head->setSectionResizeMode(1, QHeaderView::Fixed);
    head->setSectionResizeMode(2, QHeaderView::Fixed);
    table->setColumnWidth(1, 112);
    table->setColumnWidth(2, 80);
    table->verticalHeader()->setDefaultSectionSize(30);
    for (int row = 0; row < rows; ++row) {
        const auto& holder = w.holders.at(row);
        table->setItem(row, 0, new QTableWidgetItem(holder.label()));
        table->setItem(row, 1, new QTableWidgetItem(ui::Format::bytes(holder.bytes)));
        table->setItem(row, 2, new QTableWidgetItem(QString::number(holder.blobs)));
    }
    Theme::fit_rows(table);
    holders_box_->addWidget(table);
}

void GpuStatusPage::fill_transfers() {
    while (auto* item = transfers_box_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto& w = status_.host_window();
    if (!w.read || !w.channels_known) {
        transfers_box_->addWidget(Widgets::meta(QStringLiteral("-")));
        return;
    }
    const auto shown = services::HostWindowAlerts::shown_transfers(w);
    if (shown.isEmpty()) {
        transfers_box_->addWidget(
            Widgets::meta(QStringLiteral("No transfers needed more than one trip")));
        return;
    }
    auto* table = new QTableWidget(static_cast<int>(shown.size()), 10);
    Theme::rows_table(table, {QStringLiteral("APP"), QStringLiteral("SPLITS"), QStringLiteral("EXTRA"),
                              QStringLiteral("WAITS"), QStringLiteral("AVG WAIT"),
                              QStringLiteral("INLINE"), QStringLiteral("COPIED"),
                              QStringLiteral("SLICE"), QStringLiteral("REFUSED"), QStringLiteral("ARENA")});
    auto* head = table->horizontalHeader();
    head->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 10; ++column) {
        head->setSectionResizeMode(column, QHeaderView::Fixed);
        table->setColumnWidth(column, column == 4 ? 84 : column == 9 ? 130 : 70);
    }
    table->verticalHeader()->setDefaultSectionSize(30);
    const auto rate = [](double value) { return QString::number(value, 'f', 1); };
    for (int row = 0; row < shown.size(); ++row) {
        const auto& t = *shown.at(row);
        const auto peak = t.arena_peak_percent();
        const QString arena = t.arena_bytes && *t.arena_bytes > 0
                                  ? (peak ? QStringLiteral("%1, peak %2%").arg(ui::Format::bytes(*t.arena_bytes)).arg(*peak)
                                          : ui::Format::bytes(*t.arena_bytes))
                                  : QStringLiteral("-");
        const QStringList cells = {
            t.label(),
            rate(t.splits_per_s),
            rate(t.extra_pieces_per_s),
            rate(t.waits_per_s),
            t.avg_wait_ms ? QStringLiteral("%1 ms").arg(*t.avg_wait_ms, 0, 'f', 1) : QStringLiteral("-"),
            QString::number(t.inline_bulk),
            QString::number(t.copied_mappings),
            QString::number(t.slice_waits),
            QString::number(t.refusals),
            arena};
        for (int column = 0; column < cells.size(); ++column) {
            table->setItem(row, column, new QTableWidgetItem(cells.at(column)));
        }
    }
    Theme::fit_rows(table);
    transfers_box_->addWidget(table);
}

QWidget* GpuStatusPage::build_log() {
    auto panel = ui::Panel::make(QStringLiteral("Driver log"));
    panel.header->setContentsMargins(12, 6, 8, 6);
    panel.header->setSpacing(10);
    log_meta_ = Widgets::meta(QString{});
    panel.header->addWidget(log_meta_);
    panel.header->addStretch(1);
    auto* open = Widgets::button(QStringLiteral("Open the full log"), Theme::Role::kQuiet);
    connect(open, &QPushButton::clicked, this, &GpuStatusPage::open_log);
    panel.header->addWidget(open);
    panel.body->setContentsMargins(0, 0, 0, 0);
    log_text_ = new QLabel;
    log_text_->setObjectName(QString::fromLatin1(Theme::Role::kTerm));
    log_text_->setTextFormat(Qt::RichText);
    log_text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    log_text_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    log_text_->setMinimumHeight(kLogLines * 19 + 18);
    panel.body->addWidget(log_text_);
    return panel.frame;
}

void GpuStatusPage::update_log() {
    if (log_text_ == nullptr) {
        return;
    }
    const auto& g = status_.graphics();
    log_meta_->setText(QStringLiteral("%1 · %2 lines%3")
                           .arg(QFileInfo(g.log_path).fileName())
                           .arg(QLocale().toString(g.log_line_count))
                           .arg(status_.vm().running() ? QStringLiteral(" · live") : QString{}));
    const QStringList& lines = g.log_lines;
    if (lines.isEmpty()) {
        log_text_->setText(
            status_.vm().running()
                ? QStringLiteral("Nothing from the driver yet.")
                : QStringLiteral("Nothing yet. The driver writes here from its first line on "
                                 "the next boot."));
        return;
    }
    QStringList shown;
    const qsizetype from = qMax<qsizetype>(0, lines.size() - kLogLines);
    for (qsizetype i = from; i < lines.size(); ++i) {
        const QString text = lines.at(i).toHtmlEscaped();
        shown << (i + 1 == lines.size()
                      ? QStringLiteral("<span style=\"color:%1\">%2</span>")
                            .arg(QString::fromLatin1(Theme::Color::kFg), text)
                      : text);
    }
    log_text_->setText(shown.join(QStringLiteral("<br>")));
}

void GpuStatusPage::open_log() const {
    QDesktopServices::openUrl(QUrl::fromLocalFile(status_.graphics().log_path));
}

}
