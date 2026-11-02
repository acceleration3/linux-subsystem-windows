#pragma once

#include "lsw/host/services/guest_bundle.hpp"

#include <QString>

#include <optional>
#include <string>

namespace lsw::host::services {
class UpdateBatch {
public:
    UpdateBatch() = delete;

    static constexpr const char* kGuestLog = "C:\\ProgramData\\LSW\\logs\\update.log";

    static constexpr int kDownloadFailed = 8;
    static constexpr int kTaskFailed = 9;
    static constexpr int kStillRunning = 10;
    static constexpr int kNoResult = 20;
    static constexpr int kResultFailed = 22;
    static constexpr int kResultOk = 23;

    [[nodiscard]] static std::string send(unsigned short port, const std::string& release,
                                          const GuestBundle::Contents& contents, bool force);

    [[nodiscard]] static std::string read_result(const std::string& finished_before);

    [[nodiscard]] static std::optional<QString> send_failure(bool ok, int exit_code, const std::string& message);

    [[nodiscard]] static QString unreported(std::optional<int> result_exit_code);

    [[nodiscard]] static QString with_log(const QString& why);
};

}
