#pragma once

#include <windows.h>
#include <unknwn.h>

#include <utility>

namespace lsw::suites {
template <typename T>
class Com {
public:
    Com() = default;
    explicit Com(T* pointer) : pointer_(pointer) {}
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    Com(Com&& other) noexcept : pointer_(other.pointer_) { other.pointer_ = nullptr; }
    Com& operator=(Com&& other) noexcept {
        if (this != &other) {
            reset();
            pointer_ = other.pointer_;
            other.pointer_ = nullptr;
        }
        return *this;
    }
    ~Com() { reset(); }

    [[nodiscard]] T* get() const { return pointer_; }
    T* operator->() const { return pointer_; }
    explicit operator bool() const { return pointer_ != nullptr; }

    [[nodiscard]] T** put() {
        reset();
        return &pointer_;
    }

    [[nodiscard]] void** put_void() { return reinterpret_cast<void**>(put()); }

    void reset(T* pointer = nullptr) {
        if (pointer_ != nullptr) {
            pointer_->Release();
        }
        pointer_ = pointer;
    }

    [[nodiscard]] T* detach() {
        T* result = pointer_;
        pointer_ = nullptr;
        return result;
    }

    template <typename U>
    [[nodiscard]] Com<U> as() const {
        U* other = nullptr;
        if (pointer_ != nullptr) {
            pointer_->QueryInterface(__uuidof(U), reinterpret_cast<void**>(&other));
        }
        return Com<U>(other);
    }

private:
    T* pointer_ = nullptr;
};
}  // namespace lsw::suites
