#pragma once

#include <windows.h>

namespace lsw::suites {
class DynLibrary {
public:
    DynLibrary() = default;
    DynLibrary(const DynLibrary&) = delete;
    DynLibrary& operator=(const DynLibrary&) = delete;
    ~DynLibrary() { unload(); }

    [[nodiscard]] bool load(const char* name) {
        unload();
        module_ = ::LoadLibraryA(name);
        return module_ != nullptr;
    }

    void unload() {
        if (module_ != nullptr) {
            ::FreeLibrary(module_);
            module_ = nullptr;
        }
    }

    [[nodiscard]] bool loaded() const { return module_ != nullptr; }

    template <typename Function>
    [[nodiscard]] Function symbol(const char* name) const {
        if (module_ == nullptr) {
            return nullptr;
        }
        void* address = reinterpret_cast<void*>(::GetProcAddress(module_, name));
        return reinterpret_cast<Function>(address);
    }

private:
    HMODULE module_ = nullptr;
};
}  // namespace lsw::suites
