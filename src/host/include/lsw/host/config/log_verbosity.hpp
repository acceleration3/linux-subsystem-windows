#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QtGlobal>

namespace lsw::host::config {

class ManagerSettings;

class LogVerbosity : public QObject {
    Q_OBJECT
public:
    enum class Part { Manager, Mirrors, Agent, GuestMirrors, Gpu };

    static constexpr qint64 kDebugMinutes = 30;

    explicit LogVerbosity(ManagerSettings& settings, QObject* parent = nullptr);

    [[nodiscard]] static QString key(Part part);
    [[nodiscard]] static QString default_level(Part part);

    [[nodiscard]] static QString configured(const ManagerSettings& settings, Part part);
    [[nodiscard]] static QString effective(const ManagerSettings& settings, Part part,
                                           qint64 now_ms);
    [[nodiscard]] static QString effective(const ManagerSettings& settings, Part part);

    [[nodiscard]] static QString dxvk_level(const QString& level);
    [[nodiscard]] static QString vkd3d_level(const QString& level);

    [[nodiscard]] qint64 debug_until_ms() const;
    [[nodiscard]] bool debug_active(qint64 now_ms) const;
    [[nodiscard]] bool debug_active() const;

    void set_level(Part part, const QString& level);
    void start_debug(qint64 minutes = kDebugMinutes);
    void stop_debug();

    bool expire_debug(qint64 now_ms);

    void apply();

Q_SIGNALS:
    void changed();

private:
    void arm_timer();

    ManagerSettings& settings_;
    QTimer expiry_;
};

}
