#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QList>
#include <QString>

namespace lsw::host::apps {

// Writes XDG launcher entries for guest Windows apps so the host's
// application menu lists them under a "Windows" category. Clicking
// an entry runs ``lsw-manager --launch <app-id>``, which the running
// manager picks up via its D-Bus SingleInstance interface (or starts
// itself if no manager is running and pops a "Starting Windows…"
// splash).
//
// Files written (XDG base dirs):
//   * ``$XDG_DATA_HOME/applications/lsw-app-<hash>.desktop``
//     — the launcher entry. ``Categories=Windows;``, ``Exec=...``,
//       ``Icon=<absolute PNG path>``.
//   * ``$XDG_DATA_HOME/icons/lsw-manager/<hash>.png``
//     — extracted icon, referenced by absolute path from the .desktop.
//   * ``$XDG_DATA_HOME/desktop-directories/lsw-windows.directory``
//     — folder metadata for the "Windows" category (created once).
//   * ``$XDG_CONFIG_HOME/menus/applications-merged/lsw-windows.menu``
//     — menu fragment that includes every .desktop tagged with
//       ``Categories=Windows`` under a top-level "Windows" submenu
//       (created once). KDE Plasma, GNOME Shell, and lxqt-menu all
//       honour ``applications-merged``.
//
// The ``<hash>`` is a stable digest of the guest ``app_id`` (which is
// the Windows-side install path). Using a hash avoids embedding
// backslashes / spaces / drive letters in the filename and keeps the
// XDG filename ASCII-clean.
class DesktopLauncher {
public:
    // The launcher entry's basename for ``app_config_id``, without the
    // ``.desktop`` suffix — ``lsw-app-<digest>``.
    //
    // Also the desktop identity the app's mirrors take: a window whose
    // app_id is this resolves, in every compositor's task manager, to
    // the very entry written for it here. So the mirrors of one guest
    // application group together, under that application's own name and
    // icon, against the launcher the user may already have pinned —
    // rather than under the manager that happens to host them.
    [[nodiscard]] static QString entry_id(const QString& app_config_id);

    // Returns true if the entry was (re)written. Idempotent — calling
    // again with the same config just rewrites the same files. Side
    // effect: ensures the ``Windows`` category and .menu fragment
    // exist on first call.
    [[nodiscard]] static bool install(const AppConfig& cfg);

    // Which submenu the entries appear under, and whether they are
    // written at all. Both come from Settings; the manager installs
    // them once, at startup and whenever they change, so no caller has
    // to pass them through.
    static void set_category(const QString& name);
    [[nodiscard]] static QString category();
    static void set_publishing(bool enabled);
    [[nodiscard]] static bool publishing();
    // Whether a published entry also declares itself a handler for the
    // Windows file types its application named.
    static void set_file_associations(bool enabled);

    // Remove the .desktop and PNG for this app. Leaves the category
    // .menu / .directory files in place — they're cheap and other
    // apps may still use them.
    static void uninstall(const AppConfig& cfg);

    // True iff the .desktop file for this app currently exists.
    [[nodiscard]] static bool is_installed(const AppConfig& cfg);

    // Reconcile the on-disk launcher entries with ``live``: install
    // (or refresh) one per entry, then delete any leftover
    // ``lsw-app-*.desktop`` / cached PNG whose hash matches no entry
    // in ``live``. Called every time the AppConfigStore changes so
    // app removals/additions in the manager propagate to the host
    // menu automatically without user action.
    // Writes an entry for every config that asks for one and removes
    // every entry that no longer should exist, including the ones an
    // application was just un-published from. Idempotent.
    static void sync(const QList<AppConfig>& live);
};

}  // namespace lsw::host::apps
