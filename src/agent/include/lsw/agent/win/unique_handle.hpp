#pragma once

#include <utility>
#include <windows.h>

namespace lsw::agent::win {
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

    [[nodiscard]] Handle release() noexcept { return std::exchange(handle_, Traits::empty()); }

    void reset(Handle handle = Traits::empty()) noexcept {
        if (Traits::valid(handle_)) {
            Traits::close(handle_);
        }
        handle_ = handle;
    }

    [[nodiscard]] Handle* put() noexcept {
        reset();
        return &handle_;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return Traits::valid(handle_); }

private:
    Handle handle_ = Traits::empty();
};

struct KernelHandleTraits {
    using Handle = HANDLE;

    static Handle empty() noexcept { return nullptr; }

    static bool valid(Handle h) noexcept { return h != nullptr && h != INVALID_HANDLE_VALUE; }

    static void close(Handle h) noexcept { ::CloseHandle(h); }
};

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

}
