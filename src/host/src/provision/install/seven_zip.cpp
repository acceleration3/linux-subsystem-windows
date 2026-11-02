#include "lsw/host/provision/install/seven_zip.hpp"

#include "lsw/base/log.hpp"

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace lsw::host::provision::install {

namespace {
const QStringList& candidate_names() {
    static const QStringList names = {QStringLiteral("7z"), QStringLiteral("7zz"),
                                      QStringLiteral("7za")};
    return names;
}

QString last_line(const QByteArray& output) {
    const QString text = QString::fromLocal8Bit(output).trimmed();
    if (text.isEmpty()) {
        return {};
    }
    return text.section(QLatin1Char('\n'), -1).trimmed();
}

bool run(const QStringList& args, int timeout_ms, bool merged, QByteArray* stdout_out,
         QString* error_out) {
    const QString program = find_seven_zip();
    if (program.isEmpty()) {
        if (error_out != nullptr) {
            *error_out = seven_zip_missing_message();
        }
        return false;
    }

    QProcess proc;
    if (merged) {
        proc.setProcessChannelMode(QProcess::MergedChannels);
    }
    proc.start(program, args);
    if (!proc.waitForStarted(5000)) {
        if (error_out != nullptr) {
            *error_out = QStringLiteral("%1 would not start: %2")
                             .arg(program, proc.errorString());
        }
        return false;
    }
    if (!proc.waitForFinished(timeout_ms)) {
        proc.kill();
        proc.waitForFinished(5000);
        if (error_out != nullptr) {
            *error_out = QStringLiteral("%1 did not finish within %2 minutes and was stopped")
                             .arg(program)
                             .arg(timeout_ms / 60000);
        }
        return false;
    }
    const QByteArray output = proc.readAllStandardOutput();
    if (stdout_out != nullptr) {
        *stdout_out = output;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString detail = last_line(merged ? output : proc.readAllStandardError());
        LSW_WARN("7z: {} exited {} ({})", program.toStdString(), proc.exitCode(),
                 detail.toStdString());
        if (error_out != nullptr) {
            *error_out = detail.isEmpty()
                             ? QStringLiteral("%1 failed (exit %2)")
                                   .arg(program)
                                   .arg(proc.exitCode())
                             : QStringLiteral("%1 failed (exit %2): %3")
                                   .arg(program)
                                   .arg(proc.exitCode())
                                   .arg(detail);
        }
        return false;
    }
    return true;
}

}

QString find_seven_zip() {
    const QString from_env = qEnvironmentVariable("LSW_SEVEN_ZIP").trimmed();
    if (!from_env.isEmpty()) {
        const QFileInfo info{from_env};
        if (info.isAbsolute()) {
            return info.isExecutable() ? from_env : QString{};
        }
        return QStandardPaths::findExecutable(from_env);
    }
    for (const QString& name : candidate_names()) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) {
            return path;
        }
    }
    return {};
}

QString seven_zip_missing_message() {
    return QStringLiteral(
        "7-Zip is not installed, and reading a Windows ISO needs it: the media is UDF, "
        "which the ISO libraries here do not read. Install it (Arch: 7zip, Debian/Ubuntu: "
        "p7zip-full, Fedora: p7zip-plugins) and try again.");
}

bool run_seven_zip(const QStringList& args, int timeout_ms, QString* error_out) {
    return run(args, timeout_ms, true, nullptr, error_out);
}

QStringList seven_zip_entries(const QString& archive_path, QString* error_out) {
    QByteArray output;
    if (!run({QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-ba"), archive_path},
             300000, false, &output, error_out)) {
        return {};
    }

    QStringList paths;
    QString pending;
    const QStringList lines = QString::fromLocal8Bit(output).split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("Path = "))) {
            pending = trimmed.mid(7);
        } else if (trimmed.startsWith(QLatin1String("Folder = "))) {
            if (!pending.isEmpty() && trimmed.mid(9) == QLatin1String("-")) {
                paths.append(pending);
            }
            pending.clear();
        }
    }
    return paths;
}

}
