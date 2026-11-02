#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"

#include <QHash>
#include <QString>

namespace lsw::host::vm {
class DomainXmlBuilder {
public:
    DomainXmlBuilder() = delete;

    [[nodiscard]] static QString build(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    [[nodiscard]] static QString gpu_device_id();

    [[nodiscard]] static QString render_template(const QString& tmpl,
                                                 const QHash<QString, QString>& vars);

    [[nodiscard]] static QString xml_escape(const QString& text);
};

}
