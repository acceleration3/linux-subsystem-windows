#include "lsw/host/platform/process_command_runner.hpp"

#include <QProcess>
#include <QStandardPaths>

namespace lsw::host::platform {

namespace {
constexpr int kWaitMs = 5000;
}

bool ProcessCommandRunner::available(const QString& program) const {
    return !QStandardPaths::findExecutable(program).isEmpty();
}

CommandRunner::Output ProcessCommandRunner::run(const QString& program,
                                                const QStringList& arguments) {
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForFinished(kWaitMs)) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    return {ok, QString::fromUtf8(process.readAllStandardOutput())};
}

}  // namespace lsw::host::platform
