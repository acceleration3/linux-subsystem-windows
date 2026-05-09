#pragma once

#include <QObject>
#include <QString>

namespace lsw::manager::config {

// Wraps QStandardPaths to give canonical XDG locations for everything
// LSW persists, plus a tiny UI-prefs store backed by ``QSettings``.
//
// Layout (per the spec):
//   ~/.local/share/lsw/disks/      qcow2 files
//   ~/.local/share/lsw/isos/       cached Windows + payload + virtio ISOs
//   ~/.local/share/lsw/nvram/      per-VM OVMF_VARS files
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

    // UI preferences (persisted via QSettings into ``manager.toml``-style
    // INI). Each accessor pair owns one key; the underlying QSettings is
    // exposed for code that needs to roam outside this minimal set.
    [[nodiscard]] QString last_iso_path() const;
    void set_last_iso_path(const QString& path);

    [[nodiscard]] bool is_first_run() const;
    void mark_first_run_complete();

private:
    QString data_root_;
    QString config_root_;
    QString cache_root_;
};

}  // namespace lsw::manager::config
