#include "lsw/host/manager/stats_widget.hpp"

#include "lsw/host/ui/format.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>

#include <algorithm>

namespace lsw::host::manager {

namespace {

// What a gauge says when there is nothing to read.
QString dash() {
    return QStringLiteral("—");
}

}  // namespace

StatsWidget::StatsWidget(QWidget* parent) : QFrame(parent) {
    setFrameShape(QFrame::NoFrame);
    auto* form = new QFormLayout(this);
    form->setHorizontalSpacing(12);

    cpu_ = add_gauge(form, QStringLiteral("CPU"), QStringLiteral("0%"));
    ram_ = add_gauge(form, QStringLiteral("RAM"), QStringLiteral("0 / 0 GiB"));
    vram_ = add_gauge(form, QStringLiteral("VRAM"), QStringLiteral("0 GiB"));
    disk_ = add_gauge(form, QStringLiteral("DISK"), QStringLiteral("0 GiB"));
}

StatsWidget::Gauge StatsWidget::add_gauge(QFormLayout* form, const QString& name,
                                          const QString& initial) {
    Gauge gauge;
    gauge.bar = new QProgressBar;
    gauge.bar->setRange(0, 100);
    gauge.bar->setTextVisible(false);
    gauge.bar->setMaximumHeight(10);

    gauge.label = ui::Widgets::meta(initial);
    gauge.label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gauge.label->setMinimumWidth(92);

    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(gauge.bar, 1);
    layout->addWidget(gauge.label);
    // The row's name, in the same uppercase mono every other caption in
    // the application uses, so a gauge reads as one of them rather than
    // as a form field.
    form->addRow(ui::Widgets::caption(name), row);
    return gauge;
}

void StatsWidget::Gauge::set(int percent, const QString& text) const {
    bar->setValue(percent);
    label->setText(text);
}

void StatsWidget::show_usage(const Gauge& gauge, quint64 used, quint64 total) {
    const int pct = static_cast<int>((used * 100ULL) / total);
    gauge.set(std::clamp(pct, 0, 100),
              QStringLiteral("%1 / %2").arg(ui::Format::gib(static_cast<qint64>(used)),
                                            ui::Format::gib(static_cast<qint64>(total))));
}

void StatsWidget::update_stats(const vm::VmStats& s) {
    cpu_.set(s.cpu_percent, QString::number(s.cpu_percent) + QLatin1Char('%'));

    if (s.memory_total_bytes > 0) {
        show_usage(ram_, s.memory_used_bytes, s.memory_total_bytes);
    }
    // No render-server yet — VRAM stays at "—" until that lands.
    vram_.set(0, dash());

    if (s.disk_total_bytes > 0) {
        show_usage(disk_, s.disk_used_bytes, s.disk_total_bytes);
    } else {
        disk_.set(0, dash());
    }
}

void StatsWidget::clear() {
    for (const Gauge* gauge : {&cpu_, &ram_, &vram_, &disk_}) {
        gauge->set(0, dash());
    }
}

}  // namespace lsw::host::manager
