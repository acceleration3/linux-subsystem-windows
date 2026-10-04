#include "lsw/host/platform/tool_process.hpp"

#include <QFileInfo>
#include <QProcess>

namespace lsw::host::platform {

namespace {

// How long a tool gets to exec. Separate from the run timeout: a binary
// that is missing or not executable fails here at once, and that is a
// different sentence from one that ran too long.
constexpr int kStartTimeoutMs = 5000;

// The tail of what the tool printed, for the end of an error sentence.
// The last line is where tools put the reason ("No space left", "Could
// not open"), and it is worth more than the exit code it comes with.
QString last_line(const QByteArray& output) {
    const QString text = QString::fromLocal8Bit(output).trimmed();
    if (text.isEmpty()) {
        return {};
    }
    return text.section(QLatin1Char('\n'), -1).trimmed();
}

void set_error(QString* error_out, const QString& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
}

// Seconds below two minutes, minutes above, so "did not finish within
// 30 seconds" and "within 30 minutes" both read naturally.
QString duration(int timeout_ms) {
    if (timeout_ms < 120000) {
        return QStringLiteral("%1 seconds").arg(timeout_ms / 1000);
    }
    return QStringLiteral("%1 minutes").arg(timeout_ms / 60000);
}

}  // namespace

bool run_tool(const QString& program, const QStringList& args, int timeout_ms,
              QByteArray* stdout_out, QString* error_out) {
    const QString name = QFileInfo(program).fileName();
    QProcess proc;
    proc.start(program, args);
    if (!proc.waitForStarted(kStartTimeoutMs)) {
        set_error(error_out,
                  QStringLiteral("%1 would not start: %2").arg(name, proc.errorString()));
        return false;
    }
    if (!proc.waitForFinished(timeout_ms)) {
        proc.kill();
        (void)proc.waitForFinished(kStartTimeoutMs);
        set_error(error_out, QStringLiteral("%1 did not finish within %2 and was stopped")
                                 .arg(name, duration(timeout_ms)));
        return false;
    }
    if (stdout_out != nullptr) {
        *stdout_out = proc.readAllStandardOutput();
    }
    if (proc.exitStatus() != QProcess::NormalExit) {
        set_error(error_out, QStringLiteral("%1 crashed").arg(name));
        return false;
    }
    if (proc.exitCode() != 0) {
        const QString detail = last_line(proc.readAllStandardError());
        set_error(error_out,
                  detail.isEmpty()
                      ? QStringLiteral("%1 failed (exit %2)").arg(name).arg(proc.exitCode())
                      : QStringLiteral("%1 failed (exit %2): %3")
                            .arg(name)
                            .arg(proc.exitCode())
                            .arg(detail));
        return false;
    }
    return true;
}

}  // namespace lsw::host::platform
