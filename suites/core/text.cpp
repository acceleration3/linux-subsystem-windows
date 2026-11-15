#include "core/text.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>

namespace lsw::suites {

std::wstring Text::wide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length =
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                          length);
    return out;
}

std::string Text::utf8(const std::wstring& wide) {
    if (wide.empty()) {
        return {};
    }
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(),
                          length, nullptr, nullptr);
    return out;
}

std::string Text::upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

std::wstring Text::upper(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
    return text;
}

std::string Text::escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        default:
            out += c;
        }
    }
    return out;
}

std::string Text::unescape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const char next = text[++i];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

std::string Text::hex(unsigned long long value, int digits) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out(static_cast<std::size_t>(digits), '0');
    for (int i = digits - 1; i >= 0 && value != 0; --i) {
        out[static_cast<std::size_t>(i)] = kDigits[value & 0xf];
        value >>= 4;
    }
    return out;
}

}  // namespace lsw::suites
