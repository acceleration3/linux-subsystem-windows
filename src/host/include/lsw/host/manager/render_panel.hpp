#pragma once

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/pending_changes.hpp"
#include "lsw/host/vm/render_stats.hpp"

#include <QWidget>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace lsw::host::manager {

// The render channel page: which process holds which channel, how hard
// each of its three areas is being pushed, and what size would have
// avoided the last stall.
//
// The numbers are the render server's own. So are the recommendations:
// when it says an answer needed a 16 MiB reply slot, the button here
// stages 16 MiB, rather than the manager guessing a round number.
class RenderPanel : public QWidget {
    Q_OBJECT
public:
    RenderPanel(vm::RenderStats& stats, config::ManagerSettings& settings,
                config::PendingChanges& pending, QWidget* parent = nullptr);

Q_SIGNALS:
    // A staged size change wants the settings page, so the user can see
    // it sitting in the apply bar with everything else.
    void settings_requested();

private:
    void rebuild();
    void select(int channel);

    vm::RenderStats& stats_;
    config::ManagerSettings& settings_;
    config::PendingChanges& pending_;

    QHBoxLayout* strip_ = nullptr;
    QVBoxLayout* areas_ = nullptr;
    QLabel* summary_ = nullptr;
    int selected_ = 0;
};

}  // namespace lsw::host::manager
