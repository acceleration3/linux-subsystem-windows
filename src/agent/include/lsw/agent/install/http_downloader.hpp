#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace lsw::agent::install {

// Downloads a URL to a file over WinHTTP.
//
// Reports progress through a callback rather than posting notifications
// itself, so it knows nothing about jobs or the manager — which is what
// makes it reusable by every job kind that starts with a download.
class HttpDownloader {
public:
    HttpDownloader() = delete;

    // ``total`` is 0 when the server used chunked encoding and never
    // told us the length. Called at most ~10x/second.
    using ProgressCallback = std::function<void(std::uint64_t received, std::uint64_t total)>;

    // Blocking. Deletes a partial file on failure so a retry cannot pick
    // up a truncated download. Non-2xx status is a failure.
    [[nodiscard]] static bool fetch(const std::wstring& url, const std::wstring& dest_path,
                                    const ProgressCallback& on_progress,
                                    std::string& error_out);
};

}  // namespace lsw::agent::install
