#include "lsw/agent/util/text_encoding.hpp"

#include <windows.h>

namespace lsw::agent::util {

std::wstring TextEncoding::to_wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                        static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                          out.data(), n);
    return out;
}

std::string TextEncoding::to_utf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                        static_cast<int>(wide.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

}  // namespace lsw::agent::util
