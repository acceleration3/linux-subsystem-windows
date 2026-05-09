#pragma once

#include <QObject>

namespace lsw::manager {

// Single-instance lock via a unique D-Bus name (``org.lsw.Manager``).
// Per-spec: closing the window minimizes; quitting requires explicit
// Quit. A second invocation just raises the existing window.
//
// The DBus method ``raise`` is exposed as a public slot so a second
// invocation can call ``raise_existing()`` to bring the primary
// instance to the foreground.
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

public Q_SLOTS:
    // DBus-callable: a second invocation calls this on the primary.
    void raise();

Q_SIGNALS:
    // Fired in the primary process when another instance asked to raise.
    void raise_requested();

private:
    bool primary_ = false;
};

}  // namespace lsw::manager
