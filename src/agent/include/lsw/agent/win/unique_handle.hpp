#pragma once

#include <utility>
#include <windows.h>

namespace lsw::agent::win {

// Move-only owner of one Win32 resource, closed when the owner goes.
//
// The agent used to close every handle by hand, on every exit path, and
// the paths multiply faster than anyone re-reads them: the console leaked
// each session's process handle, and a new early return anywhere is one
// more place to remember. ``Traits`` names the handle type, what "no
// handle" looks like, and how to close one — the only things that differ
// between a file, a find, a desktop and a registry key.
template<class Traits>
class UniqueResource {
public:
    using Handle = typename Traits::Handle;

    UniqueResource() noexcept = default;

    explicit UniqueResource(Handle handle) noexcept : handle_(handle) {}

    ~UniqueResource() { reset(); }

    UniqueResource(UniqueResource&& other) noexcept : handle_(other.release()) {}

    UniqueResource& operator=(UniqueResource&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    UniqueResource(const UniqueResource&) = delete;
    UniqueResource& operator=(const UniqueResource&) = delete;

    [[nodiscard]] Handle get() const noexcept { return handle_; }

    // Give up ownership without closing.
    [[nodiscard]] Handle release() noexcept { return std::exchange(handle_, Traits::empty()); }

    // Close what is held, then hold ``handle``.
    void reset(Handle handle = Traits::empty()) noexcept {
        if (Traits::valid(handle_)) {
            Traits::close(handle_);
        }
        handle_ = handle;
    }

    // For APIs that return the handle through an out-parameter. Closes
    // whatever was held first, so the old value cannot leak.
    [[nodiscard]] Handle* put() noexcept {
        reset();
        return &handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return Traits::valid(handle_); }

private:
    Handle handle_ = Traits::empty();
};

// Kernel objects: processes, threads, files, pipes, jobs, snapshots.
// Both null and INVALID_HANDLE_VALUE count as empty, because which of
// the two means "failed" depends on the function that returned it —
// CreateFileW and CreateToolhelp32Snapshot use one, OpenProcess and
// CreateJobObjectW the other.
struct KernelHandleTraits {
    using Handle = HANDLE;

    static Handle empty() noexcept { return nullptr; }

    static bool valid(Handle h) noexcept { return h != nullptr && h != INVALID_HANDLE_VALUE; }

    static void close(Handle h) noexcept { ::CloseHandle(h); }
};

// FindFirstFile*: a different close function for the same handle type.
struct FindHandleTraits {
    using Handle = HANDLE;

    static Handle empty() noexcept { return INVALID_HANDLE_VALUE; }

    static bool valid(Handle h) noexcept { return h != nullptr && h != INVALID_HANDLE_VALUE; }

    static void close(Handle h) noexcept { ::FindClose(h); }
};

struct DesktopHandleTraits {
    using Handle = HDESK;

    static Handle empty() noexcept { return nullptr; }

    static bool valid(Handle h) noexcept { return h != nullptr; }

    static void close(Handle h) noexcept { ::CloseDesktop(h); }
};

// An opened or created registry key — never one of the predefined
// roots (HKEY_LOCAL_MACHINE and friends), which are not ours to close.
struct RegistryKeyTraits {
    using Handle = HKEY;

    static Handle empty() noexcept { return nullptr; }

    static bool valid(Handle h) noexcept { return h != nullptr; }

    static void close(Handle h) noexcept { ::RegCloseKey(h); }
};

using UniqueHandle = UniqueResource<KernelHandleTraits>;
using UniqueFindHandle = UniqueResource<FindHandleTraits>;
using UniqueDesktop = UniqueResource<DesktopHandleTraits>;
using UniqueRegKey = UniqueResource<RegistryKeyTraits>;

}  // namespace lsw::agent::win
