#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string_view>

namespace lsw::base {
class LogTail {
public:
    static constexpr std::size_t kLines = 300;
    static constexpr std::size_t kLineBytes = 400;

    [[nodiscard]] static LogTail& instance();

    void append(std::string_view line);

    void clear();

    void dump(int fd) const noexcept;

private:
    char lines_[kLines][kLineBytes] = {};
    unsigned short lengths_[kLines] = {};
    std::atomic<std::size_t> next_{0};
    std::mutex mutex_;
};

}
