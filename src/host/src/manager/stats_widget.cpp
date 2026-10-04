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
    return QStringLiteral("-");
}

}  // namespace

StatsWidget::StatsWidget(QWidget* parent) : QFrame(parent) {
    setFrameShape(QFrame::NoFrame);
    auto* form = new QFormLayout(this);
    form->setHorizontalSpacing(12);
    form_ = form;

    // A dash until something has been read: a zero would be a reading.
    cpu_ = add_gauge(form, QStringLiteral("CPU"), dash());
    ram_ = add_gauge(form, QStringLiteral("RAM"), dash());
    vram_ = add_gauge(form, QStringLiteral("VRAM"), dash());
    form->setRowVisible(2, false);
    disk_ = add_gauge(form, QStringLiteral("DISK"), dash());
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
    if (!s.running) {
        // Nothing runs, so nothing is used: a dash, not the last figure.
        for (const Gauge* gauge : {&cpu_, &ram_, &disk_}) {
            gauge->set(0, dash());
        }
        return;
    }
    // Until the agent answers, the CPU is the host's view of the VM's
    // threads (libvirt's), and memory and disk in use are unknown: the
    // total memory is the definition's, the disk's is Windows' own.
    cpu_.set(s.cpu_percent, QString::number(s.cpu_percent) + QLatin1Char('%'));
    if (s.agent_reachable && s.memory_total_bytes > 0) {
        show_usage(ram_, s.memory_used_bytes, s.memory_total_bytes);
    } else if (s.memory_total_bytes > 0) {
        ram_.set(0, QStringLiteral("- / %1").arg(ui::Format::gib(
                        static_cast<qint64>(s.memory_total_bytes))));
    } else {
        ram_.set(0, dash());
    }
    if (s.agent_reachable && s.disk_total_bytes > 0) {
        show_usage(disk_, s.disk_used_bytes, s.disk_total_bytes);
    } else {
        disk_.set(0, dash());
    }
}

void StatsWidget::set_vram(const std::optional<platform::GpuMemoryUsage::Reading>& reading) {
    form_->setRowVisible(2, reading.has_value());
    if (reading) {
        show_usage(vram_, reading->used_bytes, reading->total_bytes);
    }
}

void StatsWidget::clear() {
    for (const Gauge* gauge : {&cpu_, &ram_, &vram_, &disk_}) {
        gauge->set(0, dash());
    }
}

}  // namespace lsw::host::manager
