#pragma once

#include <QString>

namespace lsw::host::platform {

// The same "Windows" entry Dolphin gets, in the file GTK file managers
// read.
//
// GNOME Files, Nautilus forks and Thunar all read
// ``$XDG_CONFIG_HOME/gtk-3.0/bookmarks``: one line per bookmark, a URI
// and an optional label. GVfs serves ``smb://`` in userspace exactly as
// KIO does, so this is the same arrangement as the KDE side and not a
// mount either.
//
// A KDE user and a GNOME user are not exclusive, and neither are these:
// both can be on at once, and the settings page treats them as two
// switches rather than a choice.
class GtkBookmarks {
public:
    // Adds (or refreshes) the bookmark. Idempotent: an existing LSW
    // line is rewritten rather than duplicated, matched on the host and
    // port rather than the whole URI so a changed password does not
    // leave two of them.
    [[nodiscard]] static bool register_windows_bookmark(const QString& username,
                                                        const QString& password,
                                                        const QString& share);

    // Removes it. Used when the setting is turned off, because a
    // bookmark that outlives the thing it points at is worse than never
    // having written one.
    static void remove_windows_bookmark();
};

}  // namespace lsw::host::platform
