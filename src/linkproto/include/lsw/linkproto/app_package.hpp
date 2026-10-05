#pragma once

#include <string_view>

namespace lsw::linkproto {

// File types opened by Windows App Installer, rather than CreateProcess.
[[nodiscard]] constexpr bool is_app_package(std::string_view path) noexcept {
    constexpr std::string_view suffixes[] = {".msix", ".msixbundle", ".appx", ".appxbundle"};
    for (const auto suffix : suffixes) {
        if (path.size() < suffix.size())
            continue;
        const auto ending = path.substr(path.size() - suffix.size());
        bool matches = true;
        for (std::size_t i = 0; i < ending.size(); ++i) {
            char c = ending[i];
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
            if (c != suffix[i]) {
                matches = false;
                break;
            }
        }
        if (matches)
            return true;
    }
    return false;
}

}  // namespace lsw::linkproto
