#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lsw::agent::win {

class RollingFile {
public:
    struct Limits {
        std::uint64_t max_bytes;
        int max_files;
    };
    static constexpr Limits kAgentLimits{10ULL * 1024 * 1024, 5};
    static constexpr Limits kDefaultLimits{2ULL * 1024 * 1024, 3};

    explicit RollingFile(std::wstring path, Limits limits = kDefaultLimits);
    ~RollingFile();
    RollingFile(const RollingFile&) = delete;
    RollingFile& operator=(const RollingFile&) = delete;

    bool append(std::string_view text);

    void roll_now();

    [[nodiscard]] static std::wstring numbered(const std::wstring& path, int index);

private:
    bool open();
    void close();
    void roll();

    std::wstring path_;
    Limits limits_;
    void* handle_;
    std::uint64_t size_ = 0;
};

}
