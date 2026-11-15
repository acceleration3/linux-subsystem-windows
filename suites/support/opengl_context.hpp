#pragma once

#include "support/dyn_library.hpp"
#include "support/test_window.hpp"

#include <string>

namespace lsw::suites {
class OpenGlContext {
public:
    OpenGlContext() = default;
    OpenGlContext(const OpenGlContext&) = delete;
    OpenGlContext& operator=(const OpenGlContext&) = delete;
    ~OpenGlContext() { destroy(); }

    [[nodiscard]] bool create(HDC dc, std::string& error);

    void destroy();

    [[nodiscard]] std::string get_string(unsigned name) const;

    [[nodiscard]] std::string vendor() const { return get_string(0x1F00); }

    [[nodiscard]] std::string renderer() const { return get_string(0x1F01); }

    [[nodiscard]] std::string version() const { return get_string(0x1F02); }

    [[nodiscard]] std::string wgl_extensions() const;

    [[nodiscard]] void* proc(const char* name) const;

    [[nodiscard]] void* export_symbol(const char* name) const;

private:
    DynLibrary gl_;
    HGLRC context_ = nullptr;
    HDC dc_ = nullptr;
    const unsigned char* (__stdcall *get_string_)(unsigned) = nullptr;
    HGLRC (__stdcall *delete_context_)(HGLRC) = nullptr;
    BOOL (__stdcall *make_current_)(HDC, HGLRC) = nullptr;
    PROC (__stdcall *get_proc_)(LPCSTR) = nullptr;
};
}  // namespace lsw::suites
