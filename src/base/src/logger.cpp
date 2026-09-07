#include "lsw/base/logger.hpp"

#include "lsw/base/log.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <string>

namespace lsw::base {

namespace {

[[nodiscard]] spdlog::level::level_enum level_from_env() noexcept {
    const char* env = std::getenv("LSW_LOG");
    if (env == nullptr || *env == '\0') {
        return spdlog::level::info;
    }
    auto lvl = spdlog::level::from_str(env);
    if (lvl == spdlog::level::off && std::strcmp(env, "off") != 0) {
        return spdlog::level::info;  // unrecognized -> default
    }
    return lvl;
}

}  // namespace

void Logger::init(const char* component) noexcept {
    static std::atomic<bool> initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return;
    }
    auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto dist_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();
    dist_sink->add_sink(stderr_sink);
    std::string file_path;
    if (const char* path = std::getenv("LSW_LOG_FILE"); path != nullptr && *path != '\0') {
        file_path = path;
    }
#if defined(_WIN32)
    else {
        // In the guest, default to a file. Almost everything the ICD is
        // loaded into is a GUI-subsystem process with no console at all,
        // so stderr goes nowhere and the only sign of a problem is the
        // loader reporting "cannot find a compatible ICD" — which says
        // nothing about why. A driver that cannot explain itself is a
        // driver that costs a VM round trip per question.
        file_path = "C:\\ProgramData\\LSW\\icd.log";
    }
#endif
    if (!file_path.empty()) {
        try {
            auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
                file_path, /*truncate=*/false);
            dist_sink->add_sink(file_sink);
        } catch (const spdlog::spdlog_ex&) {
            // Couldn't open the file — keep stderr-only.
        }
    }
    auto logger = std::make_shared<spdlog::logger>(component, dist_sink);
    logger->set_level(level_from_env());
    logger->set_pattern("[%H:%M:%S.%e] [%^%-5l%$] [%n] %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
}

}  // namespace lsw::base
