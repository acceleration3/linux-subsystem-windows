#pragma once

#include "lsw/agent/win/unique_handle.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <mutex>

namespace lsw::agent::win {

// One end of a named pipe, driven with overlapped I/O so nothing on it
// can block forever.
//
// Overlapped rather than plain blocking calls for two reasons. A handle
// opened for synchronous I/O serialises every call on it, so a reader
// thread parked in ReadFile would stall every write from another thread
// behind it. And a blocking write to a peer that has stopped reading
// waits as long as the peer does; here a write gives up after its
// timeout and the pipe is declared dead instead.
//
// One thread reads, any number write (serialised internally).
class OverlappedPipe {
public:
    // Takes ownership of ``pipe``, which must have been opened or created
    // with FILE_FLAG_OVERLAPPED.
    explicit OverlappedPipe(HANDLE pipe);

    OverlappedPipe(const OverlappedPipe&) = delete;
    OverlappedPipe& operator=(const OverlappedPipe&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return pipe_.get(); }

    enum class Wait { Done, TimedOut, Interrupted, Failed };

    // Server side: wait for a client to connect, up to ``timeout``, or
    // until either interrupt (an event or a process handle; either may
    // be null) is signalled.
    [[nodiscard]] Wait connect(std::chrono::milliseconds timeout, HANDLE interrupt,
                               HANDLE other_interrupt);

    enum class Read { Data, Closed, Interrupted };

    // Read whatever is available, up to ``capacity`` bytes, into
    // ``buffer``. ``Closed`` once the other end has gone (or the pipe
    // failed); ``Interrupted`` if ``interrupt`` was signalled first, with
    // the read cancelled.
    [[nodiscard]] Read read(void* buffer, std::size_t capacity, std::size_t* got, HANDLE interrupt);

    // Write all of ``data``. False when it could not be written within
    // ``timeout`` or the pipe failed; the pipe is then ``broken`` and no
    // later write is attempted.
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

}  // namespace lsw::agent::win
