#include "lsw/host/manager/window_class_stamper.hpp"

#include <QByteArray>
#include <QGuiApplication>
#include <QWindow>

#include <cstdint>

#if LSW_HAVE_XCB
// QNativeInterface::QX11Application is the public Qt 6.2+ entry point
// for the X11 connection; no Qt private headers required.
#include <xcb/xcb.h>
#endif

namespace lsw::host::manager {

void WindowClassStamper::apply(QWindow* window, const QString& key) {
    if (window == nullptr) {
        return;
    }
#if LSW_HAVE_XCB
    auto* x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (x11 == nullptr) {
        return;  // Wayland / offscreen
    }
    xcb_connection_t* connection = x11->connection();
    if (connection == nullptr) {
        return;
    }
    const auto win = static_cast<xcb_window_t>(window->winId());
    if (win == 0) {
        return;
    }
    // ICCCM WM_CLASS is ``instance\0class\0`` — two NUL-terminated
    // Latin-1 strings.
    const QByteArray latin = key.toLatin1();
    QByteArray data;
    data.reserve(latin.size() * 2 + 2);
    data.append(latin);
    data.append('\0');
    data.append(latin);
    data.append('\0');
    xcb_change_property(connection, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS,
                        XCB_ATOM_STRING, 8, static_cast<std::uint32_t>(data.size()),
                        data.constData());
    xcb_flush(connection);
#else
    Q_UNUSED(window);
    Q_UNUSED(key);
#endif
}

}  // namespace lsw::host::manager
