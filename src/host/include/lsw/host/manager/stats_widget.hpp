#pragma once

#include "lsw/host/platform/gpu_memory_usage.hpp"
#include "lsw/host/vm/monitor.hpp"

#include <QFrame>
#include <optional>

class QFormLayout;
class QLabel;
class QProgressBar;

namespace lsw::host::manager {

// CPU / RAM / VRAM / disk gauges. Receives a ``VmStats`` snapshot and
// rerenders. Self-contained — no library deps beyond QtWidgets.
//
// VRAM is the host GPU's, read on the host: what the guest allocates
// lives there, and the figure Windows shows is a budget, not a place.
class StatsWidget : public QFrame {
    Q_OBJECT
public:
    explicit StatsWidget(QWidget* parent = nullptr);

public Q_SLOTS:
    void update_stats(const vm::VmStats& stats);
    // The display GPU's VRAM in use, whole device; none hides the row,
    // for a GPU whose driver does not say.
    void set_vram(const std::optional<platform::GpuMemoryUsage::Reading>& reading);
    void clear();

private:
    // One row: a bar, and what it says in words beside it.
    struct Gauge {
        QProgressBar* bar = nullptr;
        QLabel* label = nullptr;

        void set(int percent, const QString& text) const;
    };

    // Appends a row named ``name`` to ``form``, reading ``initial``.
    [[nodiscard]] static Gauge add_gauge(QFormLayout* form, const QString& name,
                                         const QString& initial);

    // ``used`` of ``total`` bytes, or a dash when the total is unknown.
    static void show_usage(const Gauge& gauge, quint64 used, quint64 total);

    QFormLayout* form_ = nullptr;
    Gauge cpu_;
    Gauge ram_;
    Gauge vram_;
    Gauge disk_;
};

}  // namespace lsw::host::manager
