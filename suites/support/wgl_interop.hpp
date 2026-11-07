#pragma once

#include "support/gl_library.hpp"

#include <windows.h>

#include <string>

namespace lsw::suites {
class WglInterop {
public:
    static constexpr unsigned kAccessRead = 0x0000;
    static constexpr unsigned kAccessReadWrite = 0x0001;
    static constexpr unsigned kAccessWriteDiscard = 0x0002;

    void load(const GlLibrary& library);

    [[nodiscard]] bool complete() const;

    [[nodiscard]] std::string missing() const;

    HANDLE(WINAPI* open_device)(void*) = nullptr;
    BOOL(WINAPI* close_device)(HANDLE) = nullptr;
    HANDLE(WINAPI* register_object)(HANDLE, void*, unsigned, unsigned, unsigned) = nullptr;
    BOOL(WINAPI* unregister_object)(HANDLE, HANDLE) = nullptr;
    BOOL(WINAPI* object_access)(HANDLE, unsigned) = nullptr;
    BOOL(WINAPI* lock_objects)(HANDLE, int, HANDLE*) = nullptr;
    BOOL(WINAPI* unlock_objects)(HANDLE, int, HANDLE*) = nullptr;
    BOOL(WINAPI* set_share_handle)(void*, HANDLE) = nullptr;
};
}  // namespace lsw::suites
