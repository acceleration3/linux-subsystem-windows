#include "lsw/host/config/log_verbosity.hpp"

#include "lsw/base/log_format.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/config/manager_settings.hpp"

#include <QDateTime>
#include <QStringList>

#include <algorithm>

namespace lsw::host::config {

namespace {

using K = ManagerSettings::Key;

const QStringList& level_names() {
    static const QStringList names = {QStringLiteral("trace"), QStringLiteral("debug"),
                                      QStringLiteral("info"), QStringLiteral("warn"),
                                      QStringLiteral("error")};
    return names;
}

constexpr qint64 kLongestTimerMs = 6LL * 60 * 60 * 1000;

}

LogVerbosity::LogVerbosity(ManagerSettings& settings, QObject* parent)
    : QObject(parent), settings_(settings), expiry_(this) {
    expiry_.setSingleShot(true);
    connect(&expiry_, &QTimer::timeout, this, [this] {
        if (!expire_debug(QDateTime::currentMSecsSinceEpoch())) {
            arm_timer();
        }
    });
    connect(&settings_, &ManagerSettings::changed, this, [this](const QString& key) {
        if (key.startsWith(QStringLiteral("logs/"))) {
            apply();
            Q_EMIT changed();
        }
    });
}

QString LogVerbosity::key(Part part) {
    switch (part) {
    case Part::Manager: return K::kLogManager;
    case Part::Mirrors: return K::kLogMirrors;
    case Part::Agent: return K::kLogAgent;
    case Part::GuestMirrors: return K::kLogGuestMirrors;
    case Part::Gpu: return K::kLogGpu;
    }
    return {};
}

QString LogVerbosity::default_level(Part part) {
    return ManagerSettings::default_value(key(part)).toString();
}

QString LogVerbosity::configured(const ManagerSettings& settings, Part part) {
    const QString level = settings.string_value(key(part)).trimmed().toLower();
    return level_names().contains(level) ? level : default_level(part);
}

QString LogVerbosity::effective(const ManagerSettings& settings, Part part, qint64 now_ms) {
    if (now_ms < settings.value(K::kLogDebugUntil).toLongLong()) {
        return QStringLiteral("debug");
    }
    return configured(settings, part);
}

QString LogVerbosity::effective(const ManagerSettings& settings, Part part) {
    return effective(settings, part, QDateTime::currentMSecsSinceEpoch());
}

QString LogVerbosity::dxvk_level(const QString& level) {
    if (level == QLatin1String("error")) return QStringLiteral("error");
    if (level == QLatin1String("warn")) return QStringLiteral("warn");
    if (level == QLatin1String("info")) return QStringLiteral("info");
    if (level == QLatin1String("trace")) return QStringLiteral("trace");
    return QStringLiteral("debug");
}

QString LogVerbosity::vkd3d_level(const QString& level) {
    if (level == QLatin1String("error")) return QStringLiteral("err");
    if (level == QLatin1String("warn")) return QStringLiteral("warn");
    if (level == QLatin1String("info")) return QStringLiteral("warn");
    return QStringLiteral("trace");
}

qint64 LogVerbosity::debug_until_ms() const {
    return settings_.value(K::kLogDebugUntil).toLongLong();
}

bool LogVerbosity::debug_active(qint64 now_ms) const {
    return now_ms < debug_until_ms();
}

bool LogVerbosity::debug_active() const {
    return debug_active(QDateTime::currentMSecsSinceEpoch());
}

void LogVerbosity::set_level(Part part, const QString& level) {
    settings_.set_value(key(part), level.trimmed().toLower());
}

void LogVerbosity::start_debug(qint64 minutes) {
    settings_.set_value(K::kLogDebugUntil,
                        qlonglong{QDateTime::currentMSecsSinceEpoch() + minutes * 60 * 1000});
}

void LogVerbosity::stop_debug() {
    settings_.set_value(K::kLogDebugUntil, qlonglong{0});
}

bool LogVerbosity::expire_debug(qint64 now_ms) {
    const qint64 until = debug_until_ms();
    if (until == 0 || now_ms < until) {
        return false;
    }
    settings_.set_value(K::kLogDebugUntil, qlonglong{0});
    return true;
}

void LogVerbosity::arm_timer() {
    expiry_.stop();
    const qint64 until = debug_until_ms();
    if (until == 0) {
        return;
    }
    const qint64 left = until - QDateTime::currentMSecsSinceEpoch();
    expiry_.start(static_cast<int>(std::clamp<qint64>(left, 0, kLongestTimerMs)) + 1);
}

void LogVerbosity::apply() {
    const QString level = effective(settings_, Part::Manager);
    const int number = lsw::base::LogFormat::level_from_name(level.toStdString(), 2);
    lsw::base::Logger::set_level(static_cast<spdlog::level::level_enum>(number));
    arm_timer();
}

}
