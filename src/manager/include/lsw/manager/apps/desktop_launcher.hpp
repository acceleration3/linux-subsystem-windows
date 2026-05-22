#pragma once

#include "lsw/manager/apps/app_config.hpp"

#include <QList>
#include <QString>

namespace lsw::manager::apps {

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
    // Returns true if the entry was (re)written. Idempotent — calling
    // again with the same config just rewrites the same files. Side
    // effect: ensures the ``Windows`` category and .menu fragment
    // exist on first call.
    [[nodiscard]] static bool install(const AppConfig& cfg);

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
    static void sync(const QList<AppConfig>& live);
};

}  // namespace lsw::manager::apps
