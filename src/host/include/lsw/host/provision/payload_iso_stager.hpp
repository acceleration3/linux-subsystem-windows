#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/pipeline_inputs.hpp"
#include "lsw/host/services/guest_bundle.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::config { class ManagerSettings; class Settings; }
namespace lsw::host::provision::install { struct PayloadInputs; }

namespace lsw::host::provision {

// Builds the payload ISO that rides alongside the Windows media.
//
// It carries the rendered autounattend.xml and the release an update
// would ship (services::GuestBundle: the guest agent, the accel-virt
// display driver package and the rest), which SetupComplete puts in
// place with the same script, and is what turns a stock Windows ISO into
// an unattended install that comes up already talking to the manager,
// on its own GPU driver, at the release the manager ships.
//
// Runs the actual ISO build on a worker thread — libisofs writes
// hundreds of megabytes and would otherwise freeze the wizard.
class PayloadIsoStager : public QObject {
    Q_OBJECT
public:
    explicit PayloadIsoStager(config::Settings& settings,
                              const config::ManagerSettings& manager_settings,
                              QObject* parent = nullptr);

    // Render the answer file for ``inputs`` and build the ISO for
    // ``record``. Makes the bundle on the way (which looks up this host's
    // GPUs' current driver versions in the Microsoft Update Catalog and
    // takes up to a few seconds, with the GUI thread free). Emits exactly
    // one of ``finished`` / ``failed``, ``failed`` when something the guest
    // cannot do without (the agent, the driver package) is not on this
    // machine.
    void build(const config::VmRecord& record, const PipelineInputs& inputs);

Q_SIGNALS:
    void finished(const QString& payload_iso_path);
    void failed(const QString& message);

private:
    // The ISO, once the bundle it carries is made.
    void build_iso(install::PayloadInputs payload);

    config::Settings& settings_;
    const config::ManagerSettings& manager_settings_;
    services::GuestBundle bundle_;
};

}  // namespace lsw::host::provision
