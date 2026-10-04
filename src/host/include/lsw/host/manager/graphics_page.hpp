#pragma once

#include <QStringList>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace lsw::host::config { class ManagerSettings; }
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

class GraphicsAlerts;

// The Graphics page: which host GPUs the guest's adapters are, what the
// host can serve on them, what the device was started with, whether
// Windows runs the driver on each, and the driver's own log.
//
// It replaces the render channel page. With accel-virt there is nothing
// to assign, claim or size per application: every process opens the
// adapters through Windows' own loaders, as on a physical card. What is
// left to show is whether that chain is whole, and where it is not, the
// one thing to do about it.
class GraphicsPage : public QWidget {
    Q_OBJECT
public:
    // Reads ``status``'s graphics snapshot and ``alerts``' warnings;
    // polls nothing of its own.
    GraphicsPage(services::StatusHub& status, GraphicsAlerts& alerts,
                 config::ManagerSettings& settings, QWidget* parent = nullptr);

    // The content bar's line: "accel-virt · 2 adapters · avgpu.sys started".
    [[nodiscard]] QString summary() const;
    // The status bar's facts while this page is up.
    [[nodiscard]] QStringList status_facts() const;

Q_SIGNALS:
    void settings_requested();
    // The driver for new adapters is in and wants Windows restarted.
    void restart_requested();
    // Anything the page shows changed, so the bars around it may too.
    void summary_changed();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void refresh();
    void rebuild();
    void update_log();
    // Everything ``rebuild`` draws from, as text: when it has not
    // changed, neither has the page, and rebuilding would only cost the
    // scroll position.
    [[nodiscard]] QString signature() const;

    [[nodiscard]] QWidget* build_warnings();
    [[nodiscard]] QWidget* build_adapter_card(int index) const;
    [[nodiscard]] QWidget* build_apis() const;
    [[nodiscard]] QWidget* build_device();
    [[nodiscard]] QWidget* build_guest_driver() const;
    [[nodiscard]] QWidget* build_log();
    void open_log() const;

    services::StatusHub& status_;
    GraphicsAlerts& alerts_;
    config::ManagerSettings& settings_;
    QVBoxLayout* content_ = nullptr;
    QLabel* log_meta_ = nullptr;
    QLabel* log_text_ = nullptr;
    QString built_for_;
};

}  // namespace lsw::host::manager
