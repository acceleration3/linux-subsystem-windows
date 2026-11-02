#pragma once

// clang-format off
#include <windows.h>
#include <oleauto.h>
// clang-format on

#include <string>
#include <utility>

namespace lsw::agent::win {
class Bstr {
public:
    Bstr() noexcept = default;

    ~Bstr() { ::SysFreeString(bstr_); }

    Bstr(Bstr&& other) noexcept : bstr_(std::exchange(other.bstr_, nullptr)) {}

    Bstr& operator=(Bstr&& other) noexcept {
        if (this != &other) {
            ::SysFreeString(bstr_);
            bstr_ = std::exchange(other.bstr_, nullptr);
        }
        return *this;
    }

    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    [[nodiscard]] static Bstr from_utf8(const std::string& utf8);

    [[nodiscard]] static std::string to_utf8(BSTR borrowed);

    [[nodiscard]] BSTR get() const noexcept { return bstr_; }

    [[nodiscard]] BSTR* put() noexcept {
        ::SysFreeString(bstr_);
        bstr_ = nullptr;
        return &bstr_;
    }

    [[nodiscard]] std::string utf8() const { return to_utf8(bstr_); }

private:
    BSTR bstr_ = nullptr;
};

}
