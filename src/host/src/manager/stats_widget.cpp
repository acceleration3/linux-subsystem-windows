#include "lsw/host/manager/stats_widget.hpp"

#include <QFormLayout>
#include <QLabel>
#include <QProgressBar>

namespace lsw::host::manager {

namespace {

QString fmt_gib(quint64 bytes) {
    return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GB";
}

QProgressBar* make_bar() {
    auto* b = new QProgressBar;
    b->setRange(0, 100);
    b->setTextVisible(false);
    b->setMaximumHeight(10);
    return b;
}

}  // namespace

StatsWidget::StatsWidget(QWidget* parent) : QFrame(parent) {
    setFrameShape(QFrame::StyledPanel);
    auto* form = new QFormLayout(this);
    form->setHorizontalSpacing(12);

    cpu_bar_ = make_bar();
    cpu_label_ = new QLabel("0%");
    auto* cpu_row = new QWidget;
    auto* cpu_lay = new QHBoxLayout(cpu_row);
    cpu_lay->setContentsMargins(0, 0, 0, 0);
    cpu_lay->addWidget(cpu_bar_, 1);
    cpu_lay->addWidget(cpu_label_);
    form->addRow("CPU", cpu_row);

    ram_bar_ = make_bar();
    ram_label_ = new QLabel("0 / 0 GB");
    auto* ram_row = new QWidget;
    auto* ram_lay = new QHBoxLayout(ram_row);
    ram_lay->setContentsMargins(0, 0, 0, 0);
    ram_lay->addWidget(ram_bar_, 1);
    ram_lay->addWidget(ram_label_);
    form->addRow("RAM", ram_row);

    vram_bar_ = make_bar();
    vram_label_ = new QLabel("0 GB");
    auto* vram_row = new QWidget;
    auto* vram_lay = new QHBoxLayout(vram_row);
    vram_lay->setContentsMargins(0, 0, 0, 0);
    vram_lay->addWidget(vram_bar_, 1);
    vram_lay->addWidget(vram_label_);
    form->addRow("VRAM", vram_row);

    disk_bar_ = make_bar();
    disk_label_ = new QLabel("0 GB");
    auto* disk_row = new QWidget;
    auto* disk_lay = new QHBoxLayout(disk_row);
    disk_lay->setContentsMargins(0, 0, 0, 0);
    disk_lay->addWidget(disk_bar_, 1);
    disk_lay->addWidget(disk_label_);
    form->addRow("Disk", disk_row);
}

void StatsWidget::update_stats(const vm::VmStats& s) {
    cpu_bar_->setValue(s.cpu_percent);
    cpu_label_->setText(QString::number(s.cpu_percent) + "%");

    if (s.memory_total_bytes > 0) {
        const int pct = static_cast<int>((s.memory_used_bytes * 100ULL) / s.memory_total_bytes);
        ram_bar_->setValue(std::clamp(pct, 0, 100));
        ram_label_->setText(QStringLiteral("%1 / %2").arg(fmt_gib(s.memory_used_bytes),
                                                          fmt_gib(s.memory_total_bytes)));
    }
    // No render-server yet — VRAM stays at "—" until that lands.
    vram_label_->setText(QStringLiteral("—"));
    vram_bar_->setValue(0);

    if (s.disk_total_bytes > 0) {
        const int pct = static_cast<int>((s.disk_used_bytes * 100ULL) / s.disk_total_bytes);
        disk_bar_->setValue(std::clamp(pct, 0, 100));
        disk_label_->setText(QStringLiteral("%1 / %2").arg(fmt_gib(s.disk_used_bytes),
                                                            fmt_gib(s.disk_total_bytes)));
    } else {
        disk_bar_->setValue(0);
        disk_label_->setText(QStringLiteral("—"));
    }
}

void StatsWidget::clear() {
    cpu_bar_->setValue(0);
    cpu_label_->setText("—");
    ram_bar_->setValue(0);
    ram_label_->setText("—");
    vram_bar_->setValue(0);
    vram_label_->setText("—");
    disk_bar_->setValue(0);
    disk_label_->setText("—");
}

}  // namespace lsw::host::manager
