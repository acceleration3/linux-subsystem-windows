#pragma once

#include "lsw/host/services/guest_bundle.hpp"

#include <QString>

#include <optional>
#include <string>

namespace lsw::host::services {

// The two batch files GuestUpdate sends to the agent as run_script, and what
// their exit codes mean (docs/guest-update.md, "The run", steps 2 and 3).
// Text in and text out, so that what Windows would run is checked without it.
class UpdateBatch {
public:
    UpdateBatch() = delete;

    // Where the guest's script keeps its log.
    static constexpr const char* kGuestLog = "C:\\ProgramData\\LSW\\update.log";

    // The send script's exit codes.
    static constexpr int kDownloadFailed = 8;
    static constexpr int kTaskFailed = 9;
    // A task of an earlier run is still going; its stage is not to be removed.
    static constexpr int kStillRunning = 10;
    // The result-reading script's: no result of this run, a failed one, an ok one.
    static constexpr int kNoResult = 20;
    static constexpr int kResultFailed = 22;
    static constexpr int kResultOk = 23;

    // The script that downloads every file of ``contents`` from the file
    // server on ``port`` into the stage of ``release`` and starts the task
    // that runs the guest's update script there.
    [[nodiscard]] static std::string send(unsigned short port, const std::string& release,
                                          const GuestBundle::Contents& contents, bool force);

    // The script that tells, by its exit code, what update-result.json holds
    // (a run_script's output does not come back, only how it ended).
    // ``finished_before`` is the stamp from before the run.
    [[nodiscard]] static std::string read_result(const std::string& finished_before);

    // Why the send script's end is a failure, or nothing when it is not.
    [[nodiscard]] static std::optional<QString> send_failure(bool ok, int exit_code, const std::string& message);

    // What the host says when the run window ended with nothing from the
    // agent, given the read-result script's exit code (none when it did not
    // end).
    [[nodiscard]] static QString unreported(std::optional<int> result_exit_code);

    // ``why`` and where the guest's log is.
    [[nodiscard]] static QString with_log(const QString& why);
};

}  // namespace lsw::host::services
