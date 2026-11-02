#include "lsw/base/log_tail.hpp"

#include <algorithm>
#include <cstring>
#include <unistd.h>

namespace lsw::base {

LogTail& LogTail::instance() {
    static LogTail tail;
    return tail;
}

void LogTail::append(std::string_view line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.remove_suffix(1);
    }
    const std::lock_guard lock{mutex_};
    const std::size_t slot = next_.load(std::memory_order_relaxed) % kLines;
    const std::size_t n = std::min(line.size(), kLineBytes);
    std::memcpy(lines_[slot], line.data(), n);
    lengths_[slot] = static_cast<unsigned short>(n);
    next_.fetch_add(1, std::memory_order_release);
}

void LogTail::clear() {
    const std::lock_guard lock{mutex_};
    for (auto& length : lengths_) {
        length = 0;
    }
    next_.store(0, std::memory_order_release);
}

void LogTail::dump(int fd) const noexcept {
    const std::size_t total = next_.load(std::memory_order_acquire);
    const std::size_t first = total > kLines ? total - kLines : 0;
    for (std::size_t i = first; i < total; ++i) {
        const std::size_t slot = i % kLines;
        const char* data = lines_[slot];
        std::size_t left = lengths_[slot];
        while (left > 0) {
            const ssize_t n = ::write(fd, data, left);
            if (n <= 0) {
                break;
            }
            data += n;
            left -= static_cast<std::size_t>(n);
        }
        (void)!::write(fd, "\n", 1);
    }
}

}
