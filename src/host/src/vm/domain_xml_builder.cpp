#include "lsw/host/vm/domain_xml_builder.hpp"

#include "domain_template.hpp"

#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <utility>

namespace lsw::host::vm {

namespace {

// The GPU device's PCI slots on the root bus. Explicit, because libvirt
// does not know about anything passed through <qemu:commandline> and
// would happily put one of its own devices where QEMU then puts this.
// It allocates from the low slots up (video at 0x01, root ports from
// 0x02), keeps 0x1b and 0x1f for sound and the chipset, and virtio-net
// already sits at 0x10, so the GPUs take the run after it.
constexpr int kGpuSlot = 0x11;
// Render-only adapters, one slot each, stopping short of 0x1b.
constexpr int kMaxRenderOnlyGpus = 8;

// The device id the display adapter is given, and so its QOM path
// (/machine/peripheral/<id>): Lifecycle::set_display_modes addresses
// it there, and an id that drifted would make every resolution change
// a "device not found".
constexpr QLatin1StringView kGpuId{"lsw-gpu"};

// ``value`` as one option of a QEMU -device / -chardev string. QEMU
// splits those at commas and reads ",," as a literal one, so a path
// with a comma in it — or a mode list, which is nothing but commas —
// has to be doubled, or the rest of it becomes options of its own.
QString qemu_opt(const QString& value) {
    QString out = value;
    return out.replace(QLatin1Char(','), QStringLiteral(",,"));
}

// One <qemu:arg>, escaped for the attribute it lands in.
QString qemu_arg(const QString& value) {
    return QStringLiteral("    <qemu:arg value='%1'/>").arg(DomainXmlBuilder::xml_escape(value));
}

// The options every virtio-gpu-av-pci here shares: which GPU, the
// executor that runs commands on it, and the host window. vectors=11 is
// the INF's MessageNumberLimit: three for the device's own interrupt
// sources and eight the contexts' fence queues share.
QString gpu_common(const DomainXmlInputs& inputs, const QString& host_gpu) {
    QString out = QStringLiteral(",host-gpu=%1").arg(qemu_opt(host_gpu));
    if (!inputs.executor.isEmpty()) {
        out += QStringLiteral(",executor=%1").arg(qemu_opt(inputs.executor));
    }
    return out;
}

QString gpu_tail(const DomainXmlInputs& inputs) {
    QString out = QStringLiteral(",vectors=11,hostmem=%1M").arg(inputs.host_window_mib);
    if (inputs.isolate_renderer && !inputs.renderer.isEmpty()) {
        out += QStringLiteral(",renderer=%1").arg(qemu_opt(inputs.renderer));
    }
    return out;
}

}  // namespace

QString DomainXmlBuilder::gpu_device_id() {
    return kGpuId;
}

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

    // accel-virt's QEMU, which is the only one that has the GPU device.
    vars["emulator"] = esc(inputs.qemu_binary);

    // The environment QEMU runs in. libvirt's session mode starts QEMU
    // with a scrubbed environment, so anything it needs from ours has to
    // be named here:
    //   QEMU_MODULE_DIR  the patched QEMU's own modules; without it QEMU
    //                    looks in the distribution's directory and finds
    //                    a module table with no GPU device in it.
    //   AV_D3D_PATH      DXVK and vkd3d-proton for the executor. Without
    //                    them the host offers no Direct3D, and the
    //                    guest's desktop, which is Direct3D 11,
    //                    crash-loops.
    //   XDG_RUNTIME_DIR  the PipeWire audio backend's socket lives under
    //                    it. Absent on the host, the line is left out and
    //                    QEMU fails closed at startup with a clear error.
    const std::pair<const char*, QString> env[] = {
        {"QEMU_MODULE_DIR", inputs.qemu_module_dir},
        {"AV_D3D_PATH", inputs.d3d_lib_dir},
        {"XDG_RUNTIME_DIR", QProcessEnvironment::systemEnvironment().value("XDG_RUNTIME_DIR")},
    };
    QStringList env_lines;
    for (const auto& [name, value] : env) {
        if (!value.isEmpty()) {
            env_lines << QStringLiteral("    <qemu:env name='%1' value='%2'/>")
                             .arg(QLatin1String(name), esc(value));
        }
    }
    vars["qemu_env_inner"] = env_lines.join(QLatin1Char('\n'));

    // The display adapter during Windows setup, and none after it.
    //
    // The GPU device presents its host GPU's PCI vendor and device IDs,
    // so OVMF's virtio-gpu GOP driver does not bind it, and until
    // avgpu.sys is installed nothing in the guest drives it either: with
    // no other adapter, Setup and the VNC install console are black.
    // So the first definition carries a plain VGA as the primary display
    // as well. The GPU device is there too, because provision.ps1's sign
    // stage names every virtio-gpu-av-pci present in the INF, and one
    // that is absent then gets no driver. Once setup is done the domain
    // is redefined without the VGA, which would otherwise keep the
    // desktop.
    vars["video_inner"] =
        inputs.install_display
            ? QStringLiteral("    <video>\n"
                             "      <model type='vga' vram='16384' heads='1' primary='yes'/>\n"
                             "    </video>")
            : QStringLiteral("    <video>\n"
                             "      <model type='none'/>\n"
                             "    </video>");

    // The GPU itself, as QEMU arguments: libvirt has no element for a
    // device it does not know. One display adapter on the chosen host
    // GPU, one output, and the resolutions the user asked for (empty is
    // the host monitors' own); then, optionally, a render-only adapter
    // per other host GPU.
    QStringList gpu_lines;
    gpu_lines << QStringLiteral(
        "    <!-- accel-virt's virtio-gpu-av-pci: the guest's GPU, on a host GPU. -->");
    QString display = QStringLiteral("virtio-gpu-av-pci,id=%1,bus=pcie.0,addr=0x%2")
                          .arg(kGpuId)
                          .arg(kGpuSlot, 0, 16);
    display += gpu_common(inputs, inputs.host_gpu_uuid.isEmpty() ? QStringLiteral("auto")
                                                                 : inputs.host_gpu_uuid);
    display += QStringLiteral(",max_outputs=1");
    display += gpu_tail(inputs);
    if (!inputs.display_modes.isEmpty()) {
        display += QStringLiteral(",modes=%1").arg(qemu_opt(inputs.display_modes));
    }
    gpu_lines << qemu_arg(QStringLiteral("-device")) << qemu_arg(display);
    const qsizetype render_only =
        qMin<qsizetype>(inputs.render_only_gpu_uuids.size(), kMaxRenderOnlyGpus);
    for (qsizetype i = 0; i < render_only; ++i) {
        QString device = QStringLiteral("virtio-gpu-av-pci,id=%1-%2,bus=pcie.0,addr=0x%3")
                             .arg(kGpuId)
                             .arg(i + 1)
                             .arg(kGpuSlot + 1 + static_cast<int>(i), 0, 16);
        device += gpu_common(inputs, inputs.render_only_gpu_uuids.at(i));
        device += QStringLiteral(",display=off");
        device += gpu_tail(inputs);
        gpu_lines << qemu_arg(QStringLiteral("-device")) << qemu_arg(device);
    }
    // The driver's log. avgpu.sys writes "avgpu: ..." lines to I/O port
    // 0x402, and the debug console turns them into a host file, which
    // keeps them across guest reboots and up to the instant of a
    // bugcheck, where nothing inside the guest survives. QEMU truncates
    // it when it starts, so it holds one run of the VM.
    if (!inputs.avgpu_log.isEmpty()) {
        gpu_lines
            << qemu_arg(QStringLiteral("-chardev"))
            << qemu_arg(QStringLiteral("file,id=avgpu-log,path=%1").arg(qemu_opt(inputs.avgpu_log)))
            << qemu_arg(QStringLiteral("-device"))
            << qemu_arg(QStringLiteral("isa-debugcon,iobase=0x402,chardev=avgpu-log"));
    }
    vars["gpu_args_inner"] = gpu_lines.join(QLatin1Char('\n'));

    // Shared folders. virtiofs reads the guest's RAM directly, which is
    // why the template always backs it with a shared memfd.
    QString shares_inner;
    if (!inputs.shares.isEmpty()) {
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

    // One template — the high-performance virtio-from-install shape.
    return render_template(QString::fromUtf8(embedded::kDomainTemplate), vars);
}

}  // namespace lsw::host::vm
