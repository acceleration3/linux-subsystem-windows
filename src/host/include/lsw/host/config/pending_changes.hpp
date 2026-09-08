#pragma once

#include "lsw/host/config/manager_settings.hpp"

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

namespace lsw::host::config {

// Settings that have been edited but not yet applied, because applying
// them means restarting the guest.
//
// This exists so that nothing reboots a machine as a side effect of a
// slider moving. An edit to a restart key lands here, the settings page
// shows what is staged, and the guest is only stopped and started when
// somebody presses the button that says so.
//
// A value staged back to what is already stored is not a change, so
// dragging a slider away and back leaves nothing behind.
class PendingChanges : public QObject {
    Q_OBJECT
public:
    explicit PendingChanges(ManagerSettings& settings, QObject* parent = nullptr);

    // Stage an edit. Returns true if anything is now different from
    // what is stored.
    bool stage(const QString& key, const QVariant& value);

    [[nodiscard]] bool has(const QString& key) const;
    [[nodiscard]] QVariant staged_or_current(const QString& key) const;
    [[nodiscard]] int count() const noexcept { return static_cast<int>(staged_.size()); }
    [[nodiscard]] bool empty() const noexcept { return staged_.isEmpty(); }

    // One line per change, in the form the apply bar shows: the key's
    // label, what it is, and what it would become.
    [[nodiscard]] QStringList summary() const;

    // Write every staged value through to the settings store. The
    // caller restarts the guest; this only makes the values current.
    void commit();

    // Throw the staged values away.
    void revert();

Q_SIGNALS:
    void changed();

private:
    ManagerSettings& settings_;
    QMap<QString, QVariant> staged_;
};

}  // namespace lsw::host::config
