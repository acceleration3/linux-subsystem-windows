#pragma once

#include "lsw/host/config/vm_record.hpp"
#include "lsw/host/vm/domain_xml_inputs.hpp"

#include <QHash>
#include <QString>

namespace lsw::host::vm {

// VmRecord + DomainXmlInputs -> libvirt domain XML string. Centralized
// here so the wizard, the lifecycle layer, and tests all agree on the
// schema.
class DomainXmlBuilder {
public:
    DomainXmlBuilder() = delete;

    [[nodiscard]] static QString build(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    // The id ``build`` gives the GPU's display adapter on QEMU's command
    // line, and so the last element of its QOM path,
    // /machine/peripheral/<id>, which is where its ``modes`` are set on
    // a running VM. Render-only adapters are "<id>-1", "<id>-2", ...
    [[nodiscard]] static QString gpu_device_id();

    // Test hook — exposed so unit tests can pin the exact output.
    [[nodiscard]] static QString render_template(const QString& tmpl,
                                                 const QHash<QString, QString>& vars);

    // ``text`` made safe for both element content and an attribute in
    // either quote: & < > " and ' become entities. Applied by ``build``
    // to every value that came from outside it.
    [[nodiscard]] static QString xml_escape(const QString& text);
};

}  // namespace lsw::host::vm
