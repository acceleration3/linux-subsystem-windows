#include "lsw/base/logger.hpp"

#include "lsw/base/log.hpp"
#include "lsw/base/log_format.hpp"
#include "lsw/base/repeat_collapser.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>
#include <string>
#include <vector>

namespace lsw::base {

namespace {

constexpr std::size_t kBacklogLimit = 512;

std::atomic<bool> g_overridden{false};

struct Output {
    struct Early {
        std::int64_t ts_ms;
        int level;
        std::string source;
        std::string text;
    };

    std::mutex mutex;
    std::unique_ptr<RollingFile> file;
    Logger::Observer observer;
    RepeatCollapser collapser;
    std::string line;
    std::vector<Early> early;

    static Output& instance() {
        static Output output;
        return output;
    }

    void emit_locked(std::int64_t ts_ms, int level, std::string_view source, std::string_view text) {
        if (!file && !observer && early.size() < kBacklogLimit) {
            early.push_back({ts_ms, level, std::string{source}, std::string{text}});
        }
        deliver_locked(ts_ms, level, source, text);
    }

    void deliver_locked(std::int64_t ts_ms, int level, std::string_view source,
                        std::string_view text) {
        line.clear();
        LogFormat::append_line(line, ts_ms, level, source, text);
        std::fwrite(line.data(), 1, line.size(), stderr);
        if (file) {
            file->append(line);
        }
        if (observer) {
            observer(level, source, text, ts_ms);
        }
    }

    void write_note_locked(const RepeatCollapser::Note& note, std::int64_t ts_ms) {
        const std::string text = "last message repeated " + std::to_string(note.count) +
                                 (note.count == 1 ? " time" : " times");
        emit_locked(ts_ms, note.level, note.source, text);
    }

    void record(std::int64_t ts_ms, int level, std::string_view source, std::string_view text) {
        const std::lock_guard lock{mutex};
        const auto verdict = collapser.feed(level, source, text, mono_ms());
        if (verdict.note) {
            write_note_locked(*verdict.note, ts_ms);
        }
        if (verdict.write) {
            emit_locked(ts_ms, level, source, text);
        }
    }

    [[nodiscard]] static std::int64_t mono_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
};

class ManagerSink final : public spdlog::sinks::base_sink<spdlog::details::null_mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               msg.time.time_since_epoch())
                               .count();
        const std::string_view source{msg.logger_name.data(), msg.logger_name.size()};
        const std::string_view text{msg.payload.data(), msg.payload.size()};
        Output::instance().record(ts_ms, static_cast<int>(msg.level), source, text);
    }
    void flush_() override { std::fflush(stderr); }
};

}

void Logger::init(const char* component) noexcept {
    static std::atomic<bool> initialized{false};
    bool expected = false;
    if (!initialized.compare_exchange_strong(expected, true)) {
        return;
    }
    auto logger = std::make_shared<spdlog::logger>(component, std::make_shared<ManagerSink>());
    logger->set_level(spdlog::level::trace);
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);

    const char* env = std::getenv("LSW_LOG");
    if (env != nullptr && *env != '\0') {
        constexpr int kUnknown = -1;
        const int named = LogFormat::level_from_name(env, kUnknown);
        if (named != kUnknown) {
            g_overridden.store(true);
            g_log_threshold.store(named);
        } else if (std::string_view{env} == "off") {
            g_overridden.store(true);
            g_log_threshold.store(spdlog::level::off);
        }
    }
}

void Logger::attach(const std::filesystem::path& file, RollingFile::Limits limits,
                    Observer observer) {
    auto& out = Output::instance();
    const std::lock_guard lock{out.mutex};
    out.file = std::make_unique<RollingFile>(file, limits);
    out.observer = std::move(observer);
    std::vector<Output::Early> early;
    early.swap(out.early);
    for (const auto& e : early) {
        std::string line;
        LogFormat::append_line(line, e.ts_ms, e.level, e.source, e.text);
        out.file->append(line);
        if (out.observer) {
            out.observer(e.level, e.source, e.text, e.ts_ms);
        }
    }
}

void Logger::detach() noexcept {
    auto& out = Output::instance();
    const std::lock_guard lock{out.mutex};
    out.observer = nullptr;
    out.file.reset();
}

void Logger::set_level(spdlog::level::level_enum level) noexcept {
    if (!g_overridden.load()) {
        g_log_threshold.store(static_cast<int>(level));
    }
}

spdlog::level::level_enum Logger::level() noexcept {
    return static_cast<spdlog::level::level_enum>(g_log_threshold.load());
}

bool Logger::level_overridden() noexcept {
    return g_overridden.load();
}

void Logger::write(spdlog::level::level_enum level, std::string_view source,
                   std::string_view text) {
    if (!level_enabled(level)) {
        return;
    }
    const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    Output::instance().record(ts_ms, static_cast<int>(level), source, text);
}

void Logger::flush_repeats() {
    auto& out = Output::instance();
    const std::lock_guard lock{out.mutex};
    if (const auto note = out.collapser.due(Output::mono_ms())) {
        const auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        out.write_note_locked(*note, ts_ms);
    }
}

}
