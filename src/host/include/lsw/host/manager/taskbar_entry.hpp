#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace lsw::host::manager {

// Generates a per-window XDG desktop entry so the Linux taskbar (KDE
// Plasma Wayland, GNOME Shell, etc.) can render the guest window's
// real title + icon. Each mirror window stamps a unique
// ``app_id = lsw-window-<hwnd>`` via Qt's
// ``QGuiApplication::desktopFileName`` (or X11 WM_CLASS); compositors
// then locate the matching ``lsw-window-<hwnd>.desktop`` in
// ``$XDG_DATA_HOME/applications`` and read its ``Icon=`` + ``Name=``.
// Without this, Wayland clients have NO public protocol to push a
// window icon to the compositor — ``QWindow::setIcon`` is silently
// dropped on Wayland and Plasma's Task Manager falls back to a
// generic placeholder.
//
// PNG payload is written to ``$XDG_CACHE_HOME/lsw-manager/icons/<hwnd>.png``
// and referenced by absolute path from the .desktop.
//
// Lifecycle: ``register_window`` is called once when the icon PNG
// arrives from the guest agent. ``unregister_window`` is called on
// ``WindowView::guest_closed`` (which also fires from the dtor for
// the host-driven close case). ``cleanup_stale_entries`` runs once at
// App startup to scrub anything left from a previous crashed run.
class TaskbarEntry {
public:
    // The identity a mirror with no application behind it maps under,
    // and the basename of the entry ``register_window`` writes for it.
    [[nodiscard]] static QString entry_id(std::uint64_t hwnd);

    // Writes both files. If ``icon_png`` is empty, the .desktop is
    // still emitted (with no ``Icon=`` line) so the title appears.
    static void register_window(std::uint64_t hwnd, const QString& title,
                                 const QByteArray& icon_png);

    // Removes the .desktop + PNG for ``hwnd``. Idempotent.
    static void unregister_window(std::uint64_t hwnd);

    // Removes every ``lsw-window-*.desktop`` from the user's
    // applications dir and the matching PNGs from the cache dir.
    // Called once at App startup so a previous manager crash doesn't
    // leave stale launcher entries lingering in the user's menus.
    static void cleanup_stale_entries();
};

}  // namespace lsw::host::manager
