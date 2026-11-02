#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace lsw::base {
class RollingFile {
public:
    struct Limits {
        std::uintmax_t max_bytes;
        int max_files;
    };
    static constexpr Limits kManagerLimits{10u * 1024 * 1024, 5};
    static constexpr Limits kDefaultLimits{2u * 1024 * 1024, 3};

    explicit RollingFile(std::filesystem::path path, Limits limits = kDefaultLimits);
    ~RollingFile();
    RollingFile(const RollingFile&) = delete;
    RollingFile& operator=(const RollingFile&) = delete;

    bool append(std::string_view text);

    void roll_now();

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] static std::filesystem::path numbered(const std::filesystem::path& path,
                                                        int index);

private:
    bool open_locked();
    void close_locked();
    void roll_locked();

    std::filesystem::path path_;
    Limits limits_;
    std::mutex mutex_;
    int fd_ = -1;
    std::uintmax_t size_ = 0;
};

}
