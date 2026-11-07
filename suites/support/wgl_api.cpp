#include "support/wgl_api.hpp"

#include <cstring>

namespace lsw::suites {

void WglApi::load(const GlLibrary& library) {
    choose_pixel_format = reinterpret_cast<decltype(choose_pixel_format)>(library.proc("wglChoosePixelFormatARB"));
    get_pixel_format_attribs = reinterpret_cast<decltype(get_pixel_format_attribs)>(library.proc("wglGetPixelFormatAttribivARB"));
    create_context_attribs = reinterpret_cast<decltype(create_context_attribs)>(library.proc("wglCreateContextAttribsARB"));
    get_extensions_arb = reinterpret_cast<decltype(get_extensions_arb)>(library.proc("wglGetExtensionsStringARB"));
    get_extensions_ext = reinterpret_cast<decltype(get_extensions_ext)>(library.proc("wglGetExtensionsStringEXT"));
    swap_interval = reinterpret_cast<decltype(swap_interval)>(library.proc("wglSwapIntervalEXT"));
    get_swap_interval = reinterpret_cast<decltype(get_swap_interval)>(library.proc("wglGetSwapIntervalEXT"));
}

std::string WglApi::extensions(HDC dc) const {
    std::string text;
    if (get_extensions_arb != nullptr) {
        const char* value = get_extensions_arb(dc);
        if (value != nullptr) {
            text = value;
        }
    }
    if (get_extensions_ext != nullptr) {
        const char* value = get_extensions_ext();
        if (value != nullptr) {
            text += std::string(" ") + value;
        }
    }
    return text;
}

bool WglApi::contains(const std::string& list, const char* word) {
    const std::size_t length = std::strlen(word);
    std::size_t at = 0;
    while ((at = list.find(word, at)) != std::string::npos) {
        const bool start = at == 0 || list[at - 1] == ' ';
        const bool end = at + length == list.size() || list[at + length] == ' ';
        if (start && end) {
            return true;
        }
        at += length;
    }
    return false;
}

bool WglApi::has(HDC dc, const char* extension) const {
    return contains(extensions(dc), extension);
}

}  // namespace lsw::suites
