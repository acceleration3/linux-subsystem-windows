#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>

namespace lsw::agent::win {
class OverlappedPipe {
public:
    explicit OverlappedPipe(HANDLE pipe);

    OverlappedPipe(const OverlappedPipe&) = delete;
    OverlappedPipe& operator=(const OverlappedPipe&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return pipe_.get(); }

    enum class Wait { Done, TimedOut, Interrupted, Failed };

    [[nodiscard]] Wait connect(std::chrono::milliseconds timeout, HANDLE interrupt,
                               HANDLE other_interrupt);

    enum class Read { Data, Closed, Interrupted };

    [[nodiscard]] Read read(void* buffer, std::size_t capacity, std::size_t* got, HANDLE interrupt);

    [[nodiscard]] bool write_all(const void* data, std::size_t len,
                                 std::chrono::milliseconds timeout);

    [[nodiscard]] bool broken() const noexcept { return broken_.load(); }

private:
    UniqueHandle pipe_;
    UniqueHandle read_event_;
    UniqueHandle write_event_;
    std::mutex write_mu_;
    std::atomic<bool> broken_{false};
};

}
