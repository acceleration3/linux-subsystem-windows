#include "lsw/host/vm/domain_xml_builder.hpp"

#include "domain_template.hpp"
#include "lsw/host/vm/render_server.hpp"

#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>

namespace lsw::host::vm {

QString DomainXmlBuilder::render_template(const QString& tmpl,
                                          const QHash<QString, QString>& vars) {
    // One pass over the template, so a substituted value is never
    // scanned again: a path that happens to contain "{{uuid}}" stays a
    // path. A placeholder with no value is left as it is.
    static const QRegularExpression placeholder(QStringLiteral("\\{\\{(\\w+)\\}\\}"));
    QString out;
    out.reserve(tmpl.size());
    qsizetype last = 0;
    for (auto it = placeholder.globalMatch(tmpl); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(tmpl).sliced(last, m.capturedStart() - last);
        const auto value = vars.constFind(m.captured(1));
        out += value != vars.constEnd() ? *value : m.captured(0);
        last = m.capturedEnd();
    }
    out += QStringView(tmpl).sliced(last);
    return out;
}

QString DomainXmlBuilder::xml_escape(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        switch (c.unicode()) {
        case '&':
            out += QLatin1String("&amp;");
            break;
        case '<':
            out += QLatin1String("&lt;");
            break;
        case '>':
            out += QLatin1String("&gt;");
            break;
        case '"':
            out += QLatin1String("&quot;");
            break;
        case '\'':
            out += QLatin1String("&apos;");
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

QString DomainXmlBuilder::build(const config::VmRecord& vm, const DomainXmlInputs& inputs) {
    // Every string that did not come from this file is escaped on its
    // way in. They are paths and names a user can choose — a shared
    // folder called "Tom & Jerry", a home directory with an apostrophe
    // — and the template quotes its attributes with ', so either one
    // unescaped is a definition libvirt rejects, or worse, one it reads
    // differently. The *_inner blocks are XML built here and go in as
    // they are, with their own substitutions escaped.
    const auto esc = &DomainXmlBuilder::xml_escape;

    QHash<QString, QString> vars;
    // Stable libvirt domain name — matches the on-disk artifact names
    // (see config::kVmName). Re-running the wizard re-defines the same
    // domain instead of leaving UUID-suffixed orphans.
    vars["domain_name"] = esc(vm.name);
    vars["uuid"] = vm.uuid.toString(QUuid::WithoutBraces);
    vars["ram_mib"] = QString::number(vm.sizing.memory_mib);
    vars["cpus"] = QString::number(vm.sizing.cpu_cores);
    vars["ovmf_code"] = esc(inputs.ovmf_code);
    vars["ovmf_vars_template"] = esc(inputs.ovmf_vars_template);
    vars["nvram_path"] = esc(vm.nvram_path);
    vars["disk_path"] = esc(vm.disk_path);
    vars["cid"] = QString::number(vm.vsock_cid);

    // Install media. The single domain template always references all
    // three: the Windows ISO (boot), the LSW payload ISO
    // (autounattend + agent), and virtio-win.iso (the viostor/NetKVM
    // drivers the autounattend injects so the virtio-blk system disk
    // is bootable from first boot — no controller swap).
    vars["windows_iso"] = esc(vm.iso_path);
    vars["payload_iso"] = esc(vm.payload_iso_path);
    vars["virtio_iso"] = esc(inputs.virtio_iso);

    // IVSHMEM-doorbell wiring. ``ivshmem_socket`` is the chardev path
    // ``IvshmemServer`` binds before ``virDomainCreate`` and is the
    // same path libvirt's ``<shmem><server>`` element points to.
    // Size matches the manager-side ``ShmemRegion`` allocation.
    vars["ivshmem_socket"] = esc(inputs.ivshmem_socket);
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
                       .arg(esc(RenderServer::region_name(vm.uuid, i)))
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
    const QString xdg = QProcessEnvironment::systemEnvironment().value("XDG_RUNTIME_DIR");
    if (!xdg.isEmpty()) {
        xdg_inner =
            QStringLiteral("    <qemu:env name='XDG_RUNTIME_DIR' value='%1'/>").arg(esc(xdg));
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
        memory_backing_inner = QStringLiteral("  <memoryBacking>\n"
                                              "    <source type='memfd'/>\n"
                                              "    <access mode='shared'/>\n"
                                              "  </memoryBacking>");
        QStringList blocks;
        for (const auto& share : inputs.shares) {
            blocks << QStringLiteral("    <filesystem type='mount' accessmode='passthrough'>\n"
                                     "      <driver type='virtiofs'/>\n"
                                     "      <source dir='%1'/>\n"
                                     "      <target dir='%2'/>\n"
                                     "%3"
                                     "    </filesystem>")
                          .arg(esc(share.host_path), esc(share.tag),
                               share.read_only ? QStringLiteral("      <readonly/>\n") : QString{});
        }
        shares_inner = blocks.join(QLatin1Char('\n'));
    }
    vars["host_shares_inner"] = shares_inner;
    vars["memory_backing_inner"] = memory_backing_inner;

    // One template — the high-performance virtio-from-install shape.
    return render_template(QString::fromUtf8(embedded::kDomainTemplate), vars);
}

}  // namespace lsw::host::vm
