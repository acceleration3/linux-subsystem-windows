#include "lsw/host/services/host_logging.hpp"

#include "lsw/base/crash_handler.hpp"
#include "lsw/base/log.hpp"
#include "lsw/base/log_format.hpp"
#include "lsw/base/logger.hpp"
#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/config/manager_settings.hpp"
#include "lsw/host/config/settings.hpp"
#include "lsw/host/mirror/mirror_log.hpp"
#include "lsw/host/platform/qt_log_bridge.hpp"
#include "lsw/host/services/startup_header.hpp"

namespace lsw::host::services {

HostLogging::HostLogging(config::Settings& settings, config::ManagerSettings& manager_settings,
                         apps::LogStore& store, QObject* parent)
    : QObject(parent), settings_(settings), manager_settings_(manager_settings), store_(store),
      verbosity_(manager_settings, this), repeats_(this) {
    mirror_level_from_environment_ = qEnvironmentVariableIsSet("RWIN_LOG");
    connect(&verbosity_, &config::LogVerbosity::changed, this, &HostLogging::apply_mirror_level);
    repeats_.setInterval(1000);
    connect(&repeats_, &QTimer::timeout, this, [] { lsw::base::Logger::flush_repeats(); });
}

HostLogging::~HostLogging() {
    lsw::base::Logger::detach();
    mirror::MirrorLog::set_directory({});
}

void HostLogging::start() {
    if (started_) {
        return;
    }
    started_ = true;
    mirror::MirrorLog::set_directory(settings_.mirror_logs_dir());
    platform::QtLogBridge::install();

    apps::LogStore* store = &store_;
    lsw::base::Logger::attach(
        settings_.manager_log_path().toStdString(), lsw::base::RollingFile::kManagerLimits,
        [store](int level, std::string_view source, std::string_view text, std::int64_t ts_ms) {
            apps::LogRecord record;
            record.ts_ms = static_cast<quint64>(ts_ms);
            record.level = QString::fromLatin1(lsw::base::LogFormat::level_name(level)).toUpper();
            record.source = QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()));
            record.msg = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
            store->post(std::move(record));
        });

    verbosity_.apply();
    apply_mirror_level();
    StartupHeader::write();
    lsw::base::CrashHandler::install(settings_.logs_dir().toStdString(),
                                     StartupHeader::version().toStdString());
    repeats_.start();
}

void HostLogging::apply_mirror_level() {
    if (mirror_level_from_environment_) {
        return;
    }
    const QString level = config::LogVerbosity::effective(
        manager_settings_, config::LogVerbosity::Part::Mirrors);
    qputenv("RWIN_LOG", level.toUtf8());
}

}
