#pragma once

#include "support/dyn_library.hpp"

#include <mutex>
#include <string>

namespace lsw::suites {
class GlLibrary {
public:
    [[nodiscard]] static GlLibrary& instance();

    [[nodiscard]] bool ready(std::string& error);

    [[nodiscard]] HGLRC create_context(HDC dc) const { return create_(dc); }

    BOOL delete_context(HGLRC context) const { return delete_(context); }

    BOOL make_current(HDC dc, HGLRC context) const { return make_current_(dc, context); }

    [[nodiscard]] HGLRC current_context() const { return current_context_(); }

    [[nodiscard]] HDC current_dc() const { return current_dc_(); }

    [[nodiscard]] BOOL share_lists(HGLRC first, HGLRC second) const { return share_(first, second); }

    [[nodiscard]] void* proc(const char* name) const;

    [[nodiscard]] void* export_symbol(const char* name) const { return lib_.symbol<void*>(name); }

    [[nodiscard]] void* resolve(const char* name) const;

private:
    std::mutex mutex_;
    DynLibrary lib_;
    bool loaded_ = false;
    HGLRC(__stdcall* create_)(HDC) = nullptr;
    BOOL(__stdcall* delete_)(HGLRC) = nullptr;
    BOOL(__stdcall* make_current_)(HDC, HGLRC) = nullptr;
    HGLRC(__stdcall* current_context_)() = nullptr;
    HDC(__stdcall* current_dc_)() = nullptr;
    BOOL(__stdcall* share_)(HGLRC, HGLRC) = nullptr;
    PROC(__stdcall* get_proc_)(LPCSTR) = nullptr;
};
}  // namespace lsw::suites
