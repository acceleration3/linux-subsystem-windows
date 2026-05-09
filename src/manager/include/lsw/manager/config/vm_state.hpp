#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace lsw::manager::config {

// LSW is a single-VM system (the "Linux Subsystem for Windows" guest),
// so the VM and all its on-disk artifacts use a fixed name. Stable
// across runs and across re-runs of the wizard — re-running the wizard
// re-uses the same disk image, NVRAM, and payload ISO paths.
inline constexpr auto kVmName = "lsw";

// Sizing knobs the wizard captures and the domain XML consumes. Defaults
// are sensible for a Win11 install on a modern desktop.
struct VmSizing {
    int cpu_cores = 4;      // 1 .. host_threads
    int memory_mib = 8192;  // 2048 .. 90% of host RAM
    int disk_gib = 60;      // 30 .. host free space - margin
};

// One persisted VM. The full ``vms.json`` is a list of these. Identifiers
// are UUIDs so directories never collide; user-facing ``name`` stays
// editable.
struct VmRecord {
    QUuid uuid;
    QString name;              // user-facing label, e.g. "Win11-Main"
    QString edition;           // "Win11 Pro", "Win10 LTSC", ...
    quint32 vsock_cid = 0;     // unique per VM, picked by the manager
    QString disk_path;         // qcow2 path under disks/
    QString nvram_path;        // OVMF_VARS path under nvram/
    QString iso_path;          // path of the Windows ISO used at install
    QString payload_iso_path;  // path of the LSW payload ISO
    VmSizing sizing;
    bool first_boot_done = false;  // flipped when the guest agent first pings
};

// In-memory holder for the (single) LSW VM. We deliberately do NOT
// persist this — there's exactly one VM, all its on-disk artifacts use
// stable paths derived from ``kVmName``, and the source of truth for
// "does the VM exist" is libvirt itself. The ``records()`` API + the
// ``changed`` signal are kept so the manager UI panels still work.
class VmState : public QObject {
    Q_OBJECT
public:
    explicit VmState(QObject* parent = nullptr);

    [[nodiscard]] const QList<VmRecord>& records() const noexcept { return records_; }

    [[nodiscard]] QList<VmRecord>& mutable_records() noexcept { return records_; }

    // Append; returns the assigned UUID. ``vsock_cid`` is auto-allocated
    // to the next free integer >= 100.
    QUuid add(VmRecord rec);

    // Remove by uuid. No-op if not found.
    void remove(const QUuid& uuid);

    // Lookup by uuid; nullptr if not found.
    [[nodiscard]] VmRecord* find(const QUuid& uuid);

Q_SIGNALS:
    void changed();

private:
    [[nodiscard]] quint32 allocate_cid() const;

    QList<VmRecord> records_;
};

}  // namespace lsw::manager::config
