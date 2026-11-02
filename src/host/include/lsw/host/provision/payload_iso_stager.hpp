#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/provision/pipeline_inputs.hpp"
#include "lsw/host/services/guest_bundle.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::config { class ManagerSettings; class Settings; }
namespace lsw::host::provision::install { struct PayloadInputs; }

namespace lsw::host::provision {
class PayloadIsoStager : public QObject {
    Q_OBJECT
public:
    explicit PayloadIsoStager(config::Settings& settings,
                              const config::ManagerSettings& manager_settings,
                              QObject* parent = nullptr);

    void build(const config::VmRecord& record, const PipelineInputs& inputs);

Q_SIGNALS:
    void finished(const QString& payload_iso_path);
    void failed(const QString& message);

private:
    void build_iso(install::PayloadInputs payload);

    config::Settings& settings_;
    const config::ManagerSettings& manager_settings_;
    services::GuestBundle bundle_;
};

}
