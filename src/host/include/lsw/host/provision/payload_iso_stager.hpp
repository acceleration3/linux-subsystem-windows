#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/pipeline_inputs.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::config { class Settings; }

namespace lsw::host::provision {

// Builds the payload ISO that rides alongside the Windows media.
//
// It carries the rendered autounattend.xml plus the guest agent binary,
// and is what turns a stock Windows ISO into an unattended install that
// comes up already talking to the manager.
//
// Runs the actual ISO build on a worker thread — libisofs writes
// hundreds of megabytes and would otherwise freeze the wizard.
class PayloadIsoStager : public QObject {
    Q_OBJECT
public:
    explicit PayloadIsoStager(config::Settings& settings, QObject* parent = nullptr);

    // Render the answer file for ``inputs`` and build the ISO for
    // ``record``. Emits exactly one of ``finished`` / ``failed``.
    void build(const config::VmRecord& record, const PipelineInputs& inputs);

Q_SIGNALS:
    void finished(const QString& payload_iso_path);
    void failed(const QString& message);

private:
    config::Settings& settings_;
};

}  // namespace lsw::host::provision
