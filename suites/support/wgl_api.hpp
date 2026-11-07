#pragma once

#include "support/gl_library.hpp"

#include <string>

namespace lsw::suites {
class WglApi {
public:
    void load(const GlLibrary& library);

    [[nodiscard]] std::string extensions(HDC dc) const;

    [[nodiscard]] bool has(HDC dc, const char* extension) const;

    [[nodiscard]] static bool contains(const std::string& list, const char* word);

    BOOL(__stdcall* choose_pixel_format)(HDC, const int*, const float*, UINT, int*, UINT*) = nullptr;
    BOOL(__stdcall* get_pixel_format_attribs)(HDC, int, int, UINT, const int*, int*) = nullptr;
    HGLRC(__stdcall* create_context_attribs)(HDC, HGLRC, const int*) = nullptr;
    const char*(__stdcall* get_extensions_arb)(HDC) = nullptr;
    const char*(__stdcall* get_extensions_ext)() = nullptr;
    BOOL(__stdcall* swap_interval)(int) = nullptr;
    int(__stdcall* get_swap_interval)() = nullptr;
};
}  // namespace lsw::suites
