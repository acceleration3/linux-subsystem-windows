#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace lsw::agent::install {
class HttpDownloader {
public:
    HttpDownloader() = delete;

    using ProgressCallback = std::function<void(std::uint64_t received, std::uint64_t total)>;

    [[nodiscard]] static bool fetch(const std::wstring& url, const std::wstring& dest_path,
                                    const ProgressCallback& on_progress,
                                    std::string& error_out);
};

}
