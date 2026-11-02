#pragma once

// clang-format off
#include <windows.h>
#include <unknwn.h>
// clang-format on

#include "lsw/agent/win/com_ptr.hpp"

#include <atomic>
#include <tuple>
#include <utility>

namespace lsw::agent::win {
template<class... Interfaces>
class ComObject : public Interfaces... {
public:
    ComObject(const ComObject&) = delete;
    ComObject& operator=(const ComObject&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (iid == __uuidof(IUnknown)) {
            *out = static_cast<IUnknown*>(static_cast<First*>(this));
        } else if (!(... || match<Interfaces>(iid, out))) {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --refs_;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

protected:
    ComObject() noexcept = default;
    virtual ~ComObject() = default;

private:
    using First = std::tuple_element_t<0, std::tuple<Interfaces...>>;

    template<class Interface>
    bool match(REFIID iid, void** out) {
        if (iid != __uuidof(Interface)) {
            return false;
        }
        *out = static_cast<Interface*>(this);
        return true;
    }

    std::atomic<ULONG> refs_{1};
};

template<class T, class... Args>
[[nodiscard]] ComPtr<T> make_com_object(Args&&... args) {
    ComPtr<T> object;
    object.Attach(new T(std::forward<Args>(args)...));
    return object;
}

}
