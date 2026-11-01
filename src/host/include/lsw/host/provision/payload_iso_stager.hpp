#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/pipeline_inputs.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::config { class Settings; }
namespace lsw::host::provision::install { struct PayloadInputs; }

namespace lsw::host::provision {

// Builds the payload ISO that rides alongside the Windows media.
//
// It carries the rendered autounattend.xml, the guest agent binary and
// the accel-virt display driver package, and is what turns a stock
// Windows ISO into an unattended install that comes up already talking
// to the manager, on its own GPU driver.
//
// Runs the actual ISO build on a worker thread — libisofs writes
// hundreds of megabytes and would otherwise freeze the wizard.
class PayloadIsoStager : public QObject {
    Q_OBJECT
public:
    explicit PayloadIsoStager(config::Settings& settings, QObject* parent = nullptr);

    // Render the answer file for ``inputs`` and build the ISO for
    // ``record``. Looks up this host's GPUs' current driver versions in
    // the Microsoft Update Catalog on the way (DriverCatalog), which takes
    // up to a few seconds with the GUI thread free. Emits exactly one of
    // ``finished`` / ``failed`` — ``failed`` possibly before this returns,
    // when something the guest cannot do without (the driver package) is
    // not on this machine.
    void build(const config::VmRecord& record, const PipelineInputs& inputs);

Q_SIGNALS:
    void finished(const QString& payload_iso_path);
    void failed(const QString& message);

private:
    // The rest of the build, once the adapter names are as they will be
    // carried: the vendor loaders, the shared folders' files, the ISO.
    void build_iso(install::PayloadInputs payload);

    config::Settings& settings_;
};

}  // namespace lsw::host::provision
