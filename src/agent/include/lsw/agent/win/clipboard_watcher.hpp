#pragma once

#include "lsw/agent/rpc/event_source.hpp"

#include <cstdint>
#include <future>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace lsw::agent::win {
class ClipboardWatcher final : public rpc::EventSource {
public:
    struct Content {
        std::optional<std::string> text;
        std::optional<std::vector<std::uint8_t>> png;
        std::optional<std::vector<std::uint8_t>> dib;
        bool secret = false;
        bool clear = false;
    };

    ClipboardWatcher();
    ~ClipboardWatcher() override;

    ClipboardWatcher(const ClipboardWatcher&) = delete;
    ClipboardWatcher& operator=(const ClipboardWatcher&) = delete;

    [[nodiscard]] nlohmann::json read();
    void write(const Content& content);

    [[nodiscard]] std::vector<rpc::Event> collect_pending() override;

private:
    static constexpr std::size_t kMaxTextBytes = 4U * 1024U * 1024U;
    static constexpr std::size_t kMaxImageBytes = 8U * 1024U * 1024U;
    static constexpr std::size_t kMaxMessageChars = 15U * 1024U * 1024U;

    static long long __stdcall window_proc(void* hwnd, unsigned message, std::uintptr_t wparam,
                                           std::intptr_t lparam);

    void run(std::promise<bool> ready);
    void on_update();
    [[nodiscard]] bool is_secret() const;
    bool write_on_thread(const Content& content);

    void* hwnd_ = nullptr;
    std::thread thread_;
    std::mutex mu_;
    std::optional<nlohmann::json> pending_;
};

}
