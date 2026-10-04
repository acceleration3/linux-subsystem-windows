#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QWidget>

class QComboBox;
class QTableWidget;

namespace lsw::host::config { class PendingChanges; }
namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {

// Which host GPUs the guest gets, and which of them is its display: one
// row per host Vulkan device with a role each — Display, Render only, or
// Not used. Restart settings, so every choice stages
// (ManagerSettings::Key::kHostGpu and kRenderGpus) for the apply bar.
//
// There is always exactly one display. Choosing it on another row moves
// it there and makes the old display render only, and the display row
// offers no other role. Until a display is picked the stored value is
// "auto", which is the first Vulkan device, as the device itself has it.
class AdapterRoles : public QWidget {
    Q_OBJECT
public:
    // The GPUs are the status hub's, and the rows follow its list: the
    // host is probed in the background, so the first answer may come
    // after the page is built.
    AdapterRoles(config::PendingChanges& pending, const services::StatusHub& status,
                 QWidget* parent = nullptr);

    // The GPUs a row is offered for: every real one. A software
    // rasterizer only when it is all there is, or already chosen.
    [[nodiscard]] static QVector<platform::HostGpu>
    listed(const QVector<platform::HostGpu>& gpus, const QString& display_uuid,
           const QStringList& render);

private:
    void rebuild();
    void choose(const QString& uuid, int role);
    void set_role_cell(int row, QComboBox* combo);

    config::PendingChanges& pending_;
    const services::StatusHub& status_;
    // The list the rows were last built from.
    QVector<platform::HostGpu> gpus_;
    bool probed_ = false;
    QTableWidget* table_ = nullptr;
};

}  // namespace lsw::host::manager
