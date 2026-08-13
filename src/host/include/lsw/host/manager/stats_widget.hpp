#pragma once

#include "lsw/host/vm/monitor.hpp"

#include <QFrame>

class QProgressBar;
class QLabel;

namespace lsw::host::manager {

// CPU / RAM / VRAM / disk gauges. Receives a ``VmStats`` snapshot and
// rerenders. Self-contained — no library deps beyond QtWidgets.
class StatsWidget : public QFrame {
    Q_OBJECT
public:
    explicit StatsWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void update_stats(const vm::VmStats& stats);
    void clear();

private:
    QProgressBar* cpu_bar_ = nullptr;
    QLabel* cpu_label_ = nullptr;
    QProgressBar* ram_bar_ = nullptr;
    QLabel* ram_label_ = nullptr;
    QProgressBar* vram_bar_ = nullptr;
    QLabel* vram_label_ = nullptr;
    QProgressBar* disk_bar_ = nullptr;
    QLabel* disk_label_ = nullptr;
};

}  // namespace lsw::host::manager
