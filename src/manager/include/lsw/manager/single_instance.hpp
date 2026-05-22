#pragma once

#include <QObject>
#include <QString>

namespace lsw::manager {

// Single-instance lock via a unique D-Bus name (``org.lsw.Manager``).
// Per-spec: closing the window minimizes; quitting requires explicit
// Quit. A second invocation just raises the existing window. The bus
// also forwards launch-app requests from external clicks on the XDG
// launcher entries we drop into ``~/.local/share/applications`` —
// each guest-app .desktop ``Exec=lsw-manager --launch <id>``, the
// second process forwards that id to the running manager and exits.
//
// The DBus methods ``raise`` and ``launch_app`` are exposed as public
// slots so a second invocation can call them on the primary.
class SingleInstance : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.lsw.Manager.Lifecycle")
public:
    explicit SingleInstance(QObject* parent = nullptr);

    // Try to acquire the unique name. ``true`` -> we're the primary.
    [[nodiscard]] bool acquire();

    // Tell the existing instance to come to the foreground. Returns
    // true if the call was delivered.
    [[nodiscard]] bool raise_existing();

    // Tell the existing instance to launch ``app_id`` in the guest.
    // Returns true if the message was delivered. Used by the second
    // invocation triggered from an XDG launcher click.
    [[nodiscard]] bool launch_app_existing(const QString& app_id);

public Q_SLOTS:
    // DBus-callable: a second invocation calls these on the primary.
    void raise();
    void launch_app(const QString& app_id);

Q_SIGNALS:
    // Fired in the primary process when another instance asked to raise.
    void raise_requested();
    // Fired in the primary when an external launcher click forwarded a
    // request to launch ``app_id`` inside the guest.
    void launch_app_requested(const QString& app_id);

private:
    bool primary_ = false;
};

}  // namespace lsw::manager
