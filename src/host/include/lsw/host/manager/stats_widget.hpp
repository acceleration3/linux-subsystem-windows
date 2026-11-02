#pragma once

#include "lsw/host/platform/gpu_memory_usage.hpp"
#include "lsw/host/vm/monitor.hpp"

#include <QFrame>
#include <optional>

class QFormLayout;
class QLabel;
class QProgressBar;

namespace lsw::host::manager {
class StatsWidget : public QFrame {
    Q_OBJECT
public:
    explicit StatsWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void update_stats(const vm::VmStats& stats);
    void set_vram(const std::optional<platform::GpuMemoryUsage::Reading>& reading);
    void clear();

private:
    struct Gauge {
        QProgressBar* bar = nullptr;
        QLabel* label = nullptr;

        void set(int percent, const QString& text) const;
    };

    [[nodiscard]] static Gauge add_gauge(QFormLayout* form, const QString& name,
                                         const QString& initial);

    static void show_usage(const Gauge& gauge, quint64 used, quint64 total);

    QFormLayout* form_ = nullptr;
    Gauge cpu_;
    Gauge ram_;
    Gauge vram_;
    Gauge disk_;
};

}
