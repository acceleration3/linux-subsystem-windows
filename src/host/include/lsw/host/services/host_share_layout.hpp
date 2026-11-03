#pragma once

#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

namespace lsw::host::services {
class HostShareLayout {
public:
    HostShareLayout() = delete;

    static constexpr int kMaxTagBytes = 36;

    [[nodiscard]] static QVector<vm::DomainShare> assign(const QVector<config::HostShare>& shares);

    [[nodiscard]] static QString tag_for(const QString& host_path, const QStringList& taken);
};

}
