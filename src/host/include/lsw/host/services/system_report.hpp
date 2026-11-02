#pragma once

#include "lsw/host/platform/host_gpus.hpp"

#include <QString>
#include <QVector>

namespace lsw::host::services {

class SystemReport {
public:
    struct Inputs {
        QString shipped_release;
        QString guest_release;
        QString domain_xml;
        bool agent_online = false;
        bool gpus_known = false;
        QVector<platform::HostGpu> gpus;
        QString agent_system_info;
        QString agent_gpu_status;
        QString agent_note;
    };

    SystemReport() = delete;

    [[nodiscard]] static QString render(const Inputs& inputs);
};

}
