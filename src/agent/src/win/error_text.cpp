#include "lsw/agent/win/error_text.hpp"

#include "lsw/agent/util/text_encoding.hpp"

#include <cstdio>
#include <cwctype>
#include <windows.h>

namespace lsw::agent::win {

std::string ErrorText::hresult(long hr) {
    char buf[16] = {};
    std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string ErrorText::win32(unsigned long code) {
    std::string out = std::to_string(code);
    wchar_t* text = nullptr;
    const DWORD n = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    if (n == 0 || text == nullptr) {
        return out;
    }
    // System messages end in "\r\n"; a log line must not.
    std::wstring message(text, n);
    ::LocalFree(text);
    while (!message.empty() && std::iswspace(message.back()) != 0) {
        message.pop_back();
    }
    if (!message.empty()) {
        out += " (" + util::TextEncoding::to_utf8(message) + ")";
    }
    return out;
}

std::string ErrorText::last_error() {
    return win32(::GetLastError());
}

}  // namespace lsw::agent::win
