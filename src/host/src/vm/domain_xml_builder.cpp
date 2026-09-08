#include "lsw/host/vm/domain_xml_builder.hpp"

#include "lsw/host/vm/render_server.hpp"

#include "domain_template.hpp"

#include <QHash>
#include <QStringList>
#include <QProcessEnvironment>

namespace lsw::host::vm {

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

    // IVSHMEM-doorbell wiring. ``ivshmem_socket`` is the chardev path
    // ``IvshmemServer`` binds before ``virDomainCreate`` and is the
    // same path libvirt's ``<shmem><server>`` element points to.
    // Size matches the manager-side ``ShmemRegion`` allocation.
    vars["ivshmem_socket"]   = inputs.ivshmem_socket;
    vars["ivshmem_size_mib"] = QString::number(inputs.ivshmem_size_mib);

    // The render server's region: a second, plain ivshmem device
    // backed by the file rvk-render-server made. Both the name and the
    // size come from RenderServer rather than from a second copy here
    // — the guest maps whichever file this names, and a name that
    // disagrees is a guest whose drivers find nothing.
    // One ivshmem device per render channel. The server addresses its
    // sessions by device index, so a guest with four channels
    // configured and a single device has three of them nowhere.
    const auto sizing = RenderServer::sizing();
    const int region_mib = RenderServer::region_mib();
    QStringList regions;
    for (int i = 0; i < qMax(1, sizing.sessions); ++i) {
        regions << QStringLiteral("    <shmem name='%1'>\n"
                                  "      <model type='ivshmem-plain'/>\n"
                                  "      <size unit='M'>%2</size>\n"
                                  "    </shmem>")
                       .arg(RenderServer::region_name(vm.uuid, i))
                       .arg(region_mib);
    }
    vars["rvk_regions_inner"] = regions.join(QLatin1Char('\n'));

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

    vars["video_vram_kib"] = QString::number(inputs.video_vram_kib);

    // Shared folders, and the memory backing they require.
    //
    // virtiofs reads the guest's RAM directly, so that RAM has to be a
    // shared mapping rather than the anonymous one QEMU uses by
    // default. Both halves are emitted together or not at all: a domain
    // with no shares is exactly the domain that existed before this,
    // which is what keeps a feature nobody asked for from changing how
    // everybody else's memory is allocated.
    QString shares_inner;
    QString memory_backing_inner;
    if (!inputs.shares.isEmpty()) {
        memory_backing_inner =
            QStringLiteral("  <memoryBacking>\n"
                           "    <source type='memfd'/>\n"
                           "    <access mode='shared'/>\n"
                           "  </memoryBacking>");
        QStringList blocks;
        for (const auto& share : inputs.shares) {
            blocks << QStringLiteral(
                          "    <filesystem type='mount' accessmode='passthrough'>\n"
                          "      <driver type='virtiofs'/>\n"
                          "      <source dir='%1'/>\n"
                          "      <target dir='%2'/>\n"
                          "%3"
                          "    </filesystem>")
                          .arg(share.host_path, share.tag,
                               share.read_only ? QStringLiteral("      <readonly/>\n")
                                               : QString{});
        }
        shares_inner = blocks.join(QLatin1Char('\n'));
    }
    vars["host_shares_inner"] = shares_inner;
    vars["memory_backing_inner"] = memory_backing_inner;

    // One template now — the high-performance virtio-from-install
    // shape. ``use_virtio`` is retained on DomainXmlInputs for ABI
    // stability but no longer selects a template.
    return render_template(QString::fromUtf8(embedded::kDomainTemplate), vars);
}

}  // namespace lsw::host::vm
