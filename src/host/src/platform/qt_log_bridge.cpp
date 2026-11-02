#include "lsw/host/platform/qt_log_bridge.hpp"

#include "lsw/base/log.hpp"
#include "lsw/base/logger.hpp"

#include <QtLogging>
#include <QByteArray>
#include <QString>

namespace lsw::host::platform {

namespace {

void forward(QtMsgType type, const QMessageLogContext&, const QString& message) {
    spdlog::level::level_enum level = spdlog::level::info;
    switch (type) {
    case QtDebugMsg: level = spdlog::level::debug; break;
    case QtInfoMsg: level = spdlog::level::info; break;
    case QtWarningMsg: level = spdlog::level::warn; break;
    case QtCriticalMsg:
    case QtFatalMsg: level = spdlog::level::err; break;
    }
    if (!::lsw::base::level_enabled(level)) {
        return;
    }
    const QByteArray utf8 = message.toUtf8();
    ::lsw::base::Logger::write(level, "qt", std::string_view{utf8.constData(), static_cast<std::size_t>(utf8.size())});
}

}

void QtLogBridge::install() {
    qInstallMessageHandler(forward);
}

}
