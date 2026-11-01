#pragma once

#include "lsw/host/apps/app_config.hpp"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace lsw::host::apps {

class HostMimeHandlers;

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
    // The same with the host's handlers already read, which a pass over many
    // applications does once.
    [[nodiscard]] static bool install(const AppConfig& cfg, const HostMimeHandlers& handlers);

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
    [[nodiscard]] static bool file_associations();

    // Every MIME type an application opens: the ones typed in its settings
    // and those of the file types Windows says it opens
    // (``file_extensions``), by what the host's MIME database calls a file
    // of that extension. Never application/octet-stream, which names
    // nothing, and each once. What "Make it the default" gives the
    // application, all of it.
    [[nodiscard]] static QStringList mime_types_of(const AppConfig& cfg);

    // The types its entry declares: those typed in its settings, always,
    // and of the others only those nothing on this machine handles (see
    // HostMimeHandlers) or that mimeapps.list already names this entry as
    // the default for. A host application installed later removes its types
    // from the entry at the next sync.
    [[nodiscard]] static QStringList declared_mime_types(const AppConfig& cfg,
                                                         const HostMimeHandlers& handlers);

    // The MIME types for ``extensions`` (".xlsx"). An extension the host's
    // database gives several types is settled by the kind of thing the
    // application opens (the top-level type most of its unambiguous
    // extensions have), else the one that is not text, else it is left
    // out; and text types other than the usual few are left out of an
    // application that is not a text one. So Publisher gets .pub as
    // Publisher and not as an SSH key, and Word's autosave .asd is not
    // Lisp.
    [[nodiscard]] static QStringList mime_types_of_extensions(const QStringList& extensions);

    // The application that opens files of ``mime_type`` now, as the desktop
    // says (xdg-mime): its entry's file name, and the name inside it where
    // that can be read. nullopt when none is set or xdg-mime is missing.
    struct DefaultHandler {
        QString entry;
        QString name;
    };
    [[nodiscard]] static std::optional<DefaultHandler> default_handler(const QString& mime_type);

    // Make ``cfg``'s entry the default for every type it declares
    // (``xdg-mime default``). Only ever called for an explicit click: no
    // other path in the manager replaces a default. False, with the reason
    // in ``error``, when it cannot be.
    [[nodiscard]] static bool make_default(const AppConfig& cfg, QString* error);

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
    //
    static void sync(const QList<AppConfig>& live);

    // The manager's own entry, for a manager that was not installed with
    // one (a build-tree run): its icon on the task manager, and the
    // application id the desktop portal registers it under, both come
    // from an entry named after the desktop file name. Written to
    // ``$XDG_DATA_HOME/applications/lsw-manager.desktop`` with its icon
    // beside the guest apps', and removed again once a packaged entry is
    // installed, so that one is the one found.
    static void sync_manager_entry();

private:
    // The writing half of ``sync``, with the pins already in.
    static void write_entries(const QList<AppConfig>& live, const HostMimeHandlers& handlers);
};

}  // namespace lsw::host::apps
