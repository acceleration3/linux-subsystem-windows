#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

namespace lsw::host::vm {

// A libvirt domain definition read back: the parts of it the manager
// shows as status. Read from the running domain it is what the machine
// actually has, whatever the settings say now; read from the stored
// one it is what the next start boots.
//
// The GPUs are accel-virt's virtio-gpu-av-pci devices, which libvirt
// does not know and which DomainXmlBuilder therefore passes as QEMU
// arguments: each is a ``-device`` argument in <qemu:commandline>,
// parsed here option by option.
struct DomainDescription {
    // One virtio-gpu-av-pci line.
    struct GpuDevice {
        QString id;
        // "auto" or a Vulkan device UUID, as the line has it.
        QString host_gpu;
        // False for a render-only adapter (display=off).
        bool display = true;
        // The host window, hostmem=; 0 when the line names none.
        int hostmem_mib = 0;
        // The modes= list, commas restored; unset when the line has none.
        std::optional<QString> modes;
    };

    int vcpus = 0;
    qint64 memory_mib = 0;
    // The system disk (the first <disk device='disk'>).
    QString disk_path;
    QString disk_bus;  // its <target bus=...>: "nvme", "sata", ...
    // In command-line order, which DomainXmlBuilder makes display first.
    QVector<GpuDevice> gpus;
    // The install-time definition: a VGA adapter beside the GPU device,
    // for Setup to draw on. Setup's end redefines the domain without it,
    // so a definition without one is a guest whose setup has finished,
    // whatever release made it.
    bool installing = false;

    // The display adapter, or null when there is no GPU device at all.
    [[nodiscard]] const GpuDevice* display() const noexcept;

    // The definition in ``xml`` (virDomainGetXMLDesc's output, or
    // DomainXmlBuilder's); nullopt when it does not parse as one.
    [[nodiscard]] static std::optional<DomainDescription> parse(const QString& xml);

    // A QEMU -device string split at its option commas, ",," read back
    // as a literal comma: "virtio-gpu-av-pci,id=a,modes=1x1,,2x2" is
    // {"virtio-gpu-av-pci", "id=a", "modes=1x1,2x2"}.
    [[nodiscard]] static QStringList split_options(const QString& line);

    // A QEMU size, "8192M" or "8G" or a plain byte count, in MiB; 0 when
    // it is not one.
    [[nodiscard]] static int size_mib(const QString& value);
};

// The size the guest sees of a disk image, without running qemu-img.
class DiskImage {
public:
    DiskImage() = delete;

    // The virtual size of the image at ``path``: a qcow2 file's from its
    // header, anything else's from its length. nullopt when it cannot
    // be read.
    [[nodiscard]] static std::optional<quint64> virtual_size(const QString& path);

    // The virtual size a qcow2 header gives (big-endian, at offset 24);
    // nullopt when ``header`` is not one.
    [[nodiscard]] static std::optional<quint64> qcow2_virtual_size(const QByteArray& header);
};

}  // namespace lsw::host::vm
