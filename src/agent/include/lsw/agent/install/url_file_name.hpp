#pragma once

#include <string>

namespace lsw::agent::install {

// Extracts the file name from a URL.
//
// Worth naming because the extension is load-bearing: ShellExecute
// decides how to run a downloaded file from it, so a download saved
// without its real extension silently stops being installable.
class UrlFileName {
public:
    UrlFileName() = delete;

    // Basename of the URL path, query and fragment stripped. Falls back
    // to ``download.bin`` when the URL ends in a slash.
    [[nodiscard]] static std::wstring of(const std::wstring& url);
};

}  // namespace lsw::agent::install
