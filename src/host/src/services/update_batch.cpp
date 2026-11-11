#include "lsw/host/services/update_batch.hpp"

#include "lsw/base/log.hpp"

namespace lsw::host::services {

namespace {

constexpr const char* kGuestResult = "C:\\ProgramData\\LSW\\update-result.json";

constexpr const char* kStageRoot = "C:\\ProgramData\\LSW\\update\\";

}

std::string UpdateBatch::send(unsigned short port, const std::string& release,
                                const GuestBundle::Contents& contents, bool force) {
    LSW_DEBUG("update batch: {} files, port {}{}", contents.files.size(), port, force ? ", forced" : "");
    const std::string stage = kStageRoot + release.substr(0, 16) + "-" + std::to_string(port);
    const std::string url = "http://10.0.2.2:" + std::to_string(port) + "/";
    std::string text = "@echo off\r\nsetlocal\r\n";
    text += "set STAGE=" + stage + "\r\n";
    text += "set LOG=" + std::string{kGuestLog} + "\r\n";
    text += "if not exist \"%LOG%\\..\" md \"%LOG%\\..\"\r\n";
    text += "schtasks /Query /TN lsw-update /FO CSV /NH 2>nul | findstr /i /c:Running >nul && exit /b "
            + std::to_string(kStillRunning) + "\r\n";
    text += "if exist \"%STAGE%\" rd /s /q \"%STAGE%\"\r\n";
    text += "md \"%STAGE%\"\r\n";
    const auto fetch = [&](const std::string& name, const std::string& to) {
        text += "curl.exe --fail -s -S -L --create-dirs -o \"%STAGE%\\" + to + "\" " + url + name
                + " >> \"%LOG%\" 2>&1\r\n";
        text += "if errorlevel 1 (echo [manager] download of " + name + " failed>> \"%LOG%\" & exit /b "
                + std::to_string(kDownloadFailed) + ")\r\n";
    };
    for (std::size_t i = 0; i < static_cast<std::size_t>(contents.files.size()); ++i) {
        fetch("f" + std::to_string(i), GuestBundle::staged_path(contents.files[static_cast<qsizetype>(i)].guest_path));
    }
    fetch("manifest.txt", "manifest.txt");
    fetch("shares.txt", "shares.txt");

    const std::string run = "powershell -NoProfile -ExecutionPolicy Bypass -File \"%STAGE%\\C\\ProgramData\\LSW\\"
                            "lsw-guest-update.ps1\" -Stage \"%STAGE%\" -Release " + release
                            + " -Config " + contents.config
                            + " -ProgressUrl http://10.0.2.2:" + std::to_string(port)
                            + " -Version " + contents.version + (force ? " -Force" : "");
    text += ">  \"%STAGE%\\run.cmd\" echo @echo off\r\n";
    text += ">> \"%STAGE%\\run.cmd\" echo " + run + "\r\n";
    text += "schtasks /Delete /TN lsw-update /F >nul 2>&1\r\n";
    text += "schtasks /Create /TN lsw-update /SC ONCE /ST 00:00 /RU SYSTEM /RL HIGHEST /F /TR "
            "\"%STAGE%\\run.cmd\" >> \"%LOG%\" 2>&1\r\n";
    text += "if errorlevel 1 (echo [manager] creating the task failed>> \"%LOG%\" & exit /b "
            + std::to_string(kTaskFailed) + ")\r\n";
    text += "schtasks /Run /TN lsw-update >> \"%LOG%\" 2>&1\r\n";
    text += "if errorlevel 1 (echo [manager] starting the task failed>> \"%LOG%\" & exit /b "
            + std::to_string(kTaskFailed) + ")\r\n";
    text += "exit /b 0\r\n";
    return text;
}

std::string UpdateBatch::read_result(const std::string& finished_before) {
    std::string text = "@echo off\r\n";
    text += std::string{"if not exist \""} + kGuestResult + "\" exit /b " + std::to_string(kNoResult) + "\r\n";
    if (!finished_before.empty()) {
        text += "findstr /c:\"" + finished_before + "\" \"" + kGuestResult + "\" >nul && exit /b "
                + std::to_string(kNoResult) + "\r\n";
    }
    text += std::string{"findstr /c:\"ok\\\":false\" \""} + kGuestResult + "\" >nul && exit /b "
            + std::to_string(kResultFailed) + "\r\n";
    text += "exit /b " + std::to_string(kResultOk) + "\r\n";
    return text;
}


std::optional<QString> UpdateBatch::send_failure(bool ok, int exit_code, const std::string& message) {
    if (exit_code != 0 || !ok) {
        LSW_WARN("update: the send script ended ok={} exit={}: {}", ok, exit_code, message);
    }
    if (exit_code == kDownloadFailed) {
        return with_log(QStringLiteral("LSW's files could not be copied into Windows."));
    }
    if (exit_code == kTaskFailed) {
        return with_log(QStringLiteral("Windows would not start the update."));
    }
    if (exit_code == kStillRunning) {
        return QStringLiteral("An update is still running in Windows.");
    }
    if (exit_code != 0 || !ok) {
        return with_log(QStringLiteral("Starting the update failed: %1").arg(QString::fromStdString(message)));
    }
    return std::nullopt;
}

QString UpdateBatch::unreported(std::optional<int> result_exit_code) {
    if (result_exit_code == kResultFailed) {
        return with_log(QStringLiteral("Windows could not be updated."));
    }
    if (result_exit_code == kResultOk) {
        return with_log(QStringLiteral("Windows finished the update, but its agent does not report "
                                       "the new release."));
    }
    return with_log(QStringLiteral("Windows did not report the new release in 15 minutes, and a "
                                   "previous LSW agent may have been put back."));
}

QString UpdateBatch::with_log(const QString& why) {
    return QStringLiteral("%1 %2 in Windows says more.").arg(why, QLatin1StringView(kGuestLog));
}

}
