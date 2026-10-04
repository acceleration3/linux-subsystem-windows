#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::config {

// Wraps QStandardPaths to give canonical XDG locations for everything
// LSW persists, plus a tiny UI-prefs store backed by ``QSettings``.
//
// Layout (per the spec):
//   ~/.local/share/lsw/disks/      qcow2 files
//   ~/.local/share/lsw/isos/       cached Windows + payload + virtio ISOs
//   ~/.local/share/lsw/nvram/      per-VM OVMF_VARS files
//   ~/.local/share/lsw/logs/       logs the VM writes on the host
//   ~/.config/lsw/manager.toml     UI preferences
//
// We deliberately don't persist a vms.json manifest — there's exactly
// one VM, so libvirt's domain list is the source of truth.
class Settings : public QObject {
    Q_OBJECT
public:
    explicit Settings(QObject* parent = nullptr);

    // Ensures all directories exist; returns false on failure (logged).
    [[nodiscard]] bool ensure_directories() const;

    [[nodiscard]] QString data_dir() const;    // ~/.local/share/lsw
    [[nodiscard]] QString config_dir() const;  // ~/.config/lsw
    [[nodiscard]] QString cache_dir() const;   // ~/.cache/lsw

    [[nodiscard]] QString disks_dir() const;
    [[nodiscard]] QString isos_dir() const;
    [[nodiscard]] QString nvram_dir() const;
    [[nodiscard]] QString logs_dir() const;

    // Where the VM's own files live. Derived from ``kVmName`` rather
    // than stored, so the pipeline that creates them and the importer
    // that adopts them on the next start name the same files.
    [[nodiscard]] QString vm_disk_path() const;         // disks/lsw.qcow2
    [[nodiscard]] QString vm_nvram_path() const;        // nvram/lsw.fd
    [[nodiscard]] QString vm_payload_iso_path() const;  // isos/lsw-payload.iso
    // The guest GPU driver's own log. avgpu.sys writes its lines to I/O
    // port 0x402 and QEMU's isa-debugcon puts them here, so they survive
    // guest reboots and a bugcheck, which nothing inside the guest does.
    // Written by QEMU, one file per QEMU run.
    [[nodiscard]] QString vm_avgpu_log_path() const;  // logs/lsw-avgpu.log
    // Where the virtio-win driver ISO is cached. Every domain definition
    // names it, the media ejected or not.
    [[nodiscard]] QString virtio_iso_path() const;  // isos/virtio-win.iso

    // UI preferences, persisted via QSettings into ``manager.ini``.
    [[nodiscard]] bool is_first_run() const;
    void mark_first_run_complete();

private:
    QString data_root_;
    QString config_root_;
    QString cache_root_;
};

}  // namespace lsw::host::config
