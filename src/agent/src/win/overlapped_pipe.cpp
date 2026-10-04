#include "lsw/agent/win/overlapped_pipe.hpp"

#include <algorithm>
#include <limits>

namespace lsw::agent::win {

namespace {

// Wait for an overlapped operation's event, racing up to two interrupt
// handles (null ones are skipped). WAIT_OBJECT_0 means the operation
// completed; anything else is a timeout or an interrupt.
DWORD wait_for(HANDLE op_event, HANDLE interrupt, HANDLE other_interrupt, DWORD timeout_ms) {
    HANDLE handles[3] = {op_event, nullptr, nullptr};
    DWORD count = 1;
    for (HANDLE h : {interrupt, other_interrupt}) {
        if (h != nullptr) {
            handles[count++] = h;
        }
    }
    return ::WaitForMultipleObjects(count, handles, FALSE, timeout_ms);
}

// Cancel ``ov`` and wait for the cancellation to land. The OVERLAPPED and
// the buffer belong to the caller's stack frame; returning while the
// kernel may still write to them is a use-after-free.
void cancel_and_drain(HANDLE pipe, OVERLAPPED& ov) {
    ::CancelIoEx(pipe, &ov);
    DWORD ignored = 0;
    ::GetOverlappedResult(pipe, &ov, &ignored, TRUE);
}

DWORD clamp_ms(std::chrono::milliseconds d) {
    const auto ms = std::max<std::chrono::milliseconds::rep>(0, d.count());
    return static_cast<DWORD>(
        std::min<std::chrono::milliseconds::rep>(ms, std::numeric_limits<DWORD>::max() - 1));
}

}  // namespace

OverlappedPipe::OverlappedPipe(HANDLE pipe)
    : pipe_(pipe), read_event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      write_event_(::CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

OverlappedPipe::Wait OverlappedPipe::connect(std::chrono::milliseconds timeout, HANDLE interrupt,
                                             HANDLE other_interrupt) {
    UniqueHandle event{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!pipe_ || !event) {
        return Wait::Failed;
    }
    OVERLAPPED ov{};
    ov.hEvent = event.get();
    if (::ConnectNamedPipe(pipe_.get(), &ov) != FALSE) {
        return Wait::Done;
    }
    switch (::GetLastError()) {
    case ERROR_PIPE_CONNECTED:  // the client got in between create and connect
        return Wait::Done;
    case ERROR_IO_PENDING:
        break;
    default:
        return Wait::Failed;
    }
    const DWORD w = wait_for(ov.hEvent, interrupt, other_interrupt, clamp_ms(timeout));
    if (w == WAIT_OBJECT_0) {
        DWORD ignored = 0;
        return ::GetOverlappedResult(pipe_.get(), &ov, &ignored, FALSE) != FALSE ? Wait::Done
                                                                                 : Wait::Failed;
    }
    cancel_and_drain(pipe_.get(), ov);
    return w == WAIT_TIMEOUT ? Wait::TimedOut : Wait::Interrupted;
}

OverlappedPipe::Read OverlappedPipe::read(void* buffer, std::size_t capacity, std::size_t* got,
                                          HANDLE interrupt) {
    *got = 0;
    if (!pipe_ || !read_event_) {
        return Read::Closed;
    }
    OVERLAPPED ov{};
    ov.hEvent = read_event_.get();
    ::ResetEvent(ov.hEvent);
    const auto want =
        static_cast<DWORD>(std::min<std::size_t>(capacity, std::numeric_limits<DWORD>::max()));
    DWORD n = 0;
    if (::ReadFile(pipe_.get(), buffer, want, nullptr, &ov) == FALSE &&
        ::GetLastError() != ERROR_IO_PENDING) {
        return Read::Closed;  // ERROR_BROKEN_PIPE, or the pipe is unusable
    }
    const DWORD w = wait_for(ov.hEvent, interrupt, nullptr, INFINITE);
    if (w != WAIT_OBJECT_0) {
        cancel_and_drain(pipe_.get(), ov);
        return Read::Interrupted;
    }
    if (::GetOverlappedResult(pipe_.get(), &ov, &n, FALSE) == FALSE) {
        return Read::Closed;
    }
    *got = n;
    return Read::Data;
}

bool OverlappedPipe::write_all(const void* data, std::size_t len,
                               std::chrono::milliseconds timeout) {
    const std::lock_guard lock(write_mu_);
    if (broken_ || !pipe_ || !write_event_) {
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    const auto* p = static_cast<const unsigned char*>(data);
    while (len > 0) {
        OVERLAPPED ov{};
        ov.hEvent = write_event_.get();
        ::ResetEvent(ov.hEvent);
        const auto chunk =
            static_cast<DWORD>(std::min<std::size_t>(len, std::numeric_limits<DWORD>::max()));
        DWORD wrote = 0;
        if (::WriteFile(pipe_.get(), p, chunk, nullptr, &ov) == FALSE &&
            ::GetLastError() != ERROR_IO_PENDING) {
            broken_ = true;
            return false;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (wait_for(ov.hEvent, nullptr, nullptr, clamp_ms(left)) != WAIT_OBJECT_0) {
            cancel_and_drain(pipe_.get(), ov);
            broken_ = true;
            return false;
        }
        if (::GetOverlappedResult(pipe_.get(), &ov, &wrote, FALSE) == FALSE || wrote == 0) {
            broken_ = true;
            return false;
        }
        p += wrote;
        len -= wrote;
    }
    return true;
}

}  // namespace lsw::agent::win
