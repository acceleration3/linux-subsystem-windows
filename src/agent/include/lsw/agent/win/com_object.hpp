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

// IUnknown for a COM object the agent implements itself: QueryInterface
// over ``Interfaces``, and a thread-safe reference count that deletes the
// object when it reaches zero.
//
// The UIA event handlers each carried their own copy of these three
// methods, identical but for the interface name — boilerplate that is
// easy to get subtly wrong (a QueryInterface that forgets IUnknown, a
// non-atomic count on an object called from UIA's thread pool) and has
// no reason to exist more than once.
//
// Objects start with one reference, owned by whoever created them; see
// ``make_com_object``.
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
            // Through the first interface: with several, IUnknown is
            // ambiguous, and COM requires one identity for it.
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

// Construct a ComObject-derived ``T`` and take over its initial
// reference, so nothing has to remember to Release it.
template<class T, class... Args>
[[nodiscard]] ComPtr<T> make_com_object(Args&&... args) {
    ComPtr<T> object;
    object.Attach(new T(std::forward<Args>(args)...));
    return object;
}

}  // namespace lsw::agent::win
