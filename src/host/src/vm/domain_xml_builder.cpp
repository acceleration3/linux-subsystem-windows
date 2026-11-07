#include "lsw/host/vm/domain_xml_builder.hpp"

#include "domain_template.hpp"

#include "lsw/base/log.hpp"

#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStringList>
#include <utility>

namespace lsw::host::vm {

namespace {
constexpr int kGpuSlot = 0x11;
constexpr int kMaxRenderOnlyGpus = 8;

constexpr QLatin1StringView kGpuId{"lsw-gpu"};

QString qemu_opt(const QString& value) {
    QString out = value;
    return out.replace(QLatin1Char(','), QStringLiteral(",,"));
}

QString qemu_arg(const QString& value) {
    return QStringLiteral("    <qemu:arg value='%1'/>").arg(DomainXmlBuilder::xml_escape(value));
}

QString gpu_common(const DomainXmlInputs& inputs, const QString& host_gpu) {
    QString out = QStringLiteral(",host-gpu=%1").arg(qemu_opt(host_gpu));
    if (!inputs.executor.isEmpty()) {
        out += QStringLiteral(",executor=%1").arg(qemu_opt(inputs.executor));
    }
    return out;
}

QString gpu_tail(const DomainXmlInputs& inputs) {
    QString out = QStringLiteral(",vectors=11,hostmem=%1M").arg(inputs.host_window_mib);
    if (inputs.vram_reserve_mib > 0) {
        out += QStringLiteral(",vram-reserve=%1").arg(inputs.vram_reserve_mib);
    }
    return out;
}

}

QString DomainXmlBuilder::gpu_device_id() {
    return kGpuId;
}

QString DomainXmlBuilder::render_template(const QString& tmpl,
                                          const QHash<QString, QString>& vars) {
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
    const auto esc = &DomainXmlBuilder::xml_escape;

    QHash<QString, QString> vars;
    vars["domain_name"] = esc(vm.name);
    vars["uuid"] = vm.uuid.toString(QUuid::WithoutBraces);
    vars["ram_mib"] = QString::number(vm.sizing.memory_mib);
    vars["cpus"] = QString::number(vm.sizing.cpu_cores);
    vars["nvram_path"] = esc(vm.nvram_path);
    vars["disk_path"] = esc(vm.disk_path);
    vars["cid"] = QString::number(vm.vsock_cid);

    vars["install_media"] = esc(vm.install_media_path);
    vars["payload_iso"] = esc(vm.payload_iso_path);

    vars["emulator"] = esc(inputs.qemu_binary);

    const std::pair<const char*, QString> env[] = {
        {"QEMU_MODULE_DIR", inputs.qemu_module_dir},
        {"AV_D3D_PATH", inputs.d3d_lib_dir},
        {"XDG_RUNTIME_DIR", QProcessEnvironment::systemEnvironment().value("XDG_RUNTIME_DIR")},
        {"MESA_SHADER_CACHE_DIR", inputs.shader_cache_dir},
        {"__GL_SHADER_DISK_CACHE_PATH", inputs.shader_cache_dir},
        {"DXVK_STATE_CACHE_PATH", inputs.shader_cache_dir},
        {"VKD3D_SHADER_CACHE_PATH", inputs.shader_cache_dir},
        {"DXVK_LOG_LEVEL", inputs.dxvk_log_level},
        {"VKD3D_DEBUG", inputs.vkd3d_debug},
    };
    QStringList env_lines;
    for (const auto& [name, value] : env) {
        if (!value.isEmpty()) {
            env_lines << QStringLiteral("    <qemu:env name='%1' value='%2'/>")
                             .arg(QLatin1String(name), esc(value));
        }
    }
    vars["qemu_env_inner"] = env_lines.join(QLatin1Char('\n'));

    vars["video_inner"] =
        inputs.install_display
            ? QStringLiteral("    <video>\n"
                             "      <model type='vga' vram='16384' heads='1' primary='yes'/>\n"
                             "    </video>")
            : QStringLiteral("    <video>\n"
                             "      <model type='none'/>\n"
                              "    </video>");
    vars["graphics_inner"] = inputs.install_display
        ? QStringLiteral("    <graphics type='vnc' autoport='yes' listen='127.0.0.1' sharePolicy='ignore'>\n"
                         "      <listen type='address' address='127.0.0.1'/>\n"
                         "    </graphics>")
        : QString{};

    QStringList gpu_lines;
    if (!inputs.qemu_data_dir.isEmpty()) {
        gpu_lines << qemu_arg(QStringLiteral("-L")) << qemu_arg(inputs.qemu_data_dir);
    }
    gpu_lines << QStringLiteral(
        "    <!-- accel-virt's virtio-gpu-av-pci: the guest's GPU, on a host GPU. -->");
    QString display = QStringLiteral("virtio-gpu-av-pci,id=%1,bus=pcie.0,addr=0x%2")
                          .arg(kGpuId)
                          .arg(kGpuSlot, 0, 16);
    display += gpu_common(inputs, inputs.host_gpu_uuid.isEmpty() ? QStringLiteral("auto")
                                                                 : inputs.host_gpu_uuid);
    if (inputs.install_display) {
        display += QStringLiteral(",max_outputs=0");
        display += gpu_tail(inputs);
    } else {
        display += QStringLiteral(",max_outputs=1");
        display += gpu_tail(inputs);
        if (!inputs.export_socket.isEmpty()) {
            display += QStringLiteral(",export-socket=%1").arg(qemu_opt(inputs.export_socket));
        }
        if (!inputs.display_modes.isEmpty()) {
            display += QStringLiteral(",modes=%1").arg(qemu_opt(inputs.display_modes));
        }
        if (inputs.display_refresh_millihz != 0) {
            display += QStringLiteral(",refresh=%1").arg(inputs.display_refresh_millihz);
        }
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
        if (inputs.install_display) {
            device += QStringLiteral(",max_outputs=0");
        }
        device += gpu_tail(inputs);
        gpu_lines << qemu_arg(QStringLiteral("-device")) << qemu_arg(device);
    }
    if (!inputs.avgpu_log.isEmpty()) {
        gpu_lines
            << qemu_arg(QStringLiteral("-chardev"))
            << qemu_arg(QStringLiteral("file,id=avgpu-log,path=%1,append=on").arg(qemu_opt(inputs.avgpu_log)))
            << qemu_arg(QStringLiteral("-device"))
            << qemu_arg(QStringLiteral("isa-debugcon,iobase=0x402,chardev=avgpu-log"));
    }
    vars["gpu_args_inner"] = gpu_lines.join(QLatin1Char('\n'));

    QStringList input_lines;
    if (!inputs.input_socket.isEmpty()) {
        input_lines
            << qemu_arg(QStringLiteral("-chardev"))
            << qemu_arg(QStringLiteral("socket,id=lsw-input,path=%1,server=on,wait=off")
                            .arg(qemu_opt(inputs.input_socket)))
            << qemu_arg(QStringLiteral("-mon"))
            << qemu_arg(QStringLiteral("chardev=lsw-input,mode=control"));
    }
    vars["input_args_inner"] = input_lines.join(QLatin1Char('\n'));
    LSW_DEBUG("domain: {} MiB, {} cpus, {} render-only GPUs, {} shares, display {} refresh {} mHz, "
              "host window {} MiB, install display {}",
              vm.sizing.memory_mib, vm.sizing.cpu_cores, render_only, inputs.shares.size(),
              inputs.display_modes.isEmpty() ? "host modes" : inputs.display_modes.toStdString(),
              inputs.display_refresh_millihz, inputs.host_window_mib, inputs.install_display);

    QString shares_inner;
    if (!inputs.shares.isEmpty()) {
        QStringList blocks;
        for (const auto& share : inputs.shares) {
            blocks << QStringLiteral("    <filesystem type='mount' accessmode='passthrough'>\n"
                                     "      <driver type='virtiofs'/>\n"
                                     "      <source dir='%1'/>\n"
                                     "      <target dir='%2'/>\n"
                                     "      <alias name='%3'/>\n"
                                     "%4"
                                     "    </filesystem>")
                          .arg(esc(share.host_path), esc(share.tag),
                               esc(QString::fromLatin1(DomainShare::kAliasPrefix) + share.drive),
                               share.read_only ? QStringLiteral("      <readonly/>\n") : QString{});
        }
        shares_inner = blocks.join(QLatin1Char('\n'));
    }
    vars["host_shares_inner"] = shares_inner;

    return render_template(QString::fromUtf8(embedded::kDomainTemplate), vars);
}

}
