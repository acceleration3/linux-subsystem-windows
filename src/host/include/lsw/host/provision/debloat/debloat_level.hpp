#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::provision::debloat {

// How much of Windows to take out of the image before installing it.
//
// The work happens on the host, offline, against install.wim: wimlib
// deletes paths inside the image and hivex edits the offline registry
// hives. Nothing runs in the guest, so nothing has to be undone after
// the fact and the removed bytes are never written to the VM's disk.
//
// This is the same idea as MicroWin and tiny11, which is where the
// target lists came from — but not the same mechanism. Both of those
// run on Windows and drive DISM, which does the servicing-stack
// bookkeeping as it removes a package. There is no DISM here, so a
// package's payload and its registry entries are removed directly.
// That is why the levels differ in how serviceable they leave the
// result, and why ``Maximum`` says so out loud.
enum class DebloatLevel {
    // Install Windows as Microsoft ships it.
    None,
    // Consumer apps and the telemetry/advertising surface, and nothing
    // that services the machine. Windows Update, Defender, WinRE and
    // the component store are all intact, so the image takes updates
    // and can have features added back.
    Sane,
    // Also the servicing layer. Smaller and faster to install, and a
    // one-way door: no Windows Update, no Defender, no recovery
    // environment, and optional features cannot be added later.
    Maximum,
};

// What a level does, for the wizard and the logs.
class DebloatPlan {
public:
    explicit DebloatPlan(DebloatLevel level);

    [[nodiscard]] DebloatLevel level() const noexcept { return level_; }

    // Provisioned application families to remove, by the leading part
    // of their package family name. A prefix rather than a full name
    // because the version and the publisher hash move with every build.
    [[nodiscard]] const QStringList& appx_prefixes() const noexcept { return appx_prefixes_; }

    // Paths inside the image to delete, as they appear in the WIM
    // (forward slashes, no drive letter).
    [[nodiscard]] const QStringList& paths() const noexcept { return paths_; }

    // Windows component packages to strip, by the leading part of their
    // package name under the SOFTWARE hive's "Microsoft/Windows/
    // CurrentVersion/Component Based Servicing/Packages".
    [[nodiscard]] const QStringList& component_prefixes() const noexcept {
        return component_prefixes_;
    }

    // WinSxS is a keep-list, not a remove-list: it holds thousands of
    // component directories whose names carry a version and a hash, so
    // naming what goes is not possible and naming what stays is. A
    // directory is kept when its name contains any of these.
    [[nodiscard]] const QStringList& winsxs_keep() const noexcept { return winsxs_keep_; }

    // Driver packages under DriverStore/FileRepository, by the leading
    // part of their INF name. A remove-list and deliberately so: this
    // one fails safe. Keeping a driver nothing needs costs megabytes;
    // removing the wrong storage or input driver produces an image that
    // installs and then will not boot.
    [[nodiscard]] const QStringList& driver_remove() const noexcept { return driver_remove_; }

    // One line per level, for the wizard's summary label.
    [[nodiscard]] QString summary() const;

    // Empty unless the level costs something the user should be told
    // about before choosing it.
    [[nodiscard]] QString warning() const;

    [[nodiscard]] static QString to_string(DebloatLevel level);
    [[nodiscard]] static DebloatLevel from_string(const QString& text);

private:
    DebloatLevel level_;
    QStringList appx_prefixes_;
    QStringList paths_;
    QStringList component_prefixes_;
    QStringList winsxs_keep_;
    QStringList driver_remove_;
};

}  // namespace lsw::host::provision::debloat
