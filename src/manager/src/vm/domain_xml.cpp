#include "lsw/manager/vm/domain_xml.hpp"

#include "domain_template.hpp"

#include <QHash>
#include <QProcessEnvironment>

namespace lsw::manager::vm {

QString DomainXmlBuilder::render_template(const QString& tmpl,
                                          const QHash<QString, QString>& vars) {
    QString out = tmpl;
    for (auto it = vars.begin(); it != vars.end(); ++it) {
        out.replace(QStringLiteral("{{%1}}").arg(it.key()), it.value());
    }
    return out;
}

QString DomainXmlBuilder::build(const config::VmRecord& vm, const DomainXmlInputs& inputs) {
    QHash<QString, QString> vars;
    // Stable libvirt domain name — matches the on-disk artifact names
    // (see config::kVmName). Re-running the wizard re-defines the same
    // domain instead of leaving UUID-suffixed orphans.
    vars["domain_name"] = vm.name;
    vars["uuid"] = vm.uuid.toString(QUuid::WithoutBraces);
    vars["ram_mib"] = QString::number(vm.sizing.memory_mib);
    vars["cpus"] = QString::number(vm.sizing.cpu_cores);
    vars["ovmf_code"] = inputs.ovmf_code;
    vars["ovmf_vars_template"] = inputs.ovmf_vars_template;
    vars["nvram_path"] = vm.nvram_path;
    vars["disk_path"] = vm.disk_path;
    vars["cid"] = QString::number(vm.vsock_cid);

    // Install media. The single domain template always references all
    // three: the Windows ISO (boot), the LSW payload ISO
    // (autounattend + agent), and virtio-win.iso (the viostor/NetKVM
    // drivers the autounattend injects so the virtio-blk system disk
    // is bootable from first boot — no controller swap).
    vars["windows_iso"] = vm.iso_path;
    vars["payload_iso"] = vm.payload_iso_path;
    vars["virtio_iso"] = inputs.virtio_iso;

    // The PipeWire audio backend needs XDG_RUNTIME_DIR to find its
    // socket under $XDG_RUNTIME_DIR/pipewire-0. libvirt session-mode
    // strips most env vars when spawning QEMU, so we re-inject it via
    // <qemu:env>. Both templates emit a placeholder slot where this
    // line lands (or stays empty when XDG_RUNTIME_DIR isn't set on
    // the host — qemu fails closed at startup with a clear error).
    QString xdg_inner;
    const QString xdg =
        QProcessEnvironment::systemEnvironment().value("XDG_RUNTIME_DIR");
    if (!xdg.isEmpty()) {
        xdg_inner = QStringLiteral("    <qemu:env name='XDG_RUNTIME_DIR' value='%1'/>")
                        .arg(xdg);
    }
    vars["xdg_env_inner"] = xdg_inner;

    // One template now — the high-performance virtio-from-install
    // shape. ``use_virtio`` is retained on DomainXmlInputs for ABI
    // stability but no longer selects a template.
    return render_template(QString::fromUtf8(embedded::kDomainTemplate), vars);
}

}  // namespace lsw::manager::vm
