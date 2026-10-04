#pragma once

// clang-format off
#include <windows.h>
#include <oleauto.h>
// clang-format on

#include <string>
#include <utility>

namespace lsw::agent::win {

// An owned COM ``BSTR``, freed on destruction, readable as UTF-8.
//
// UIA hands back BSTRs from a dozen different property getters. Without
// one owner each call site grows its own SysFreeString on every exit path
// and its own conversion — and one of them eventually forgets the length
// and truncates on an embedded NUL.
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

    // A fresh BSTR holding ``utf8``, for passing into COM.
    [[nodiscard]] static Bstr from_utf8(const std::string& utf8);

    // A BSTR we do not own — a VARIANT's ``bstrVal`` — as UTF-8. Null and
    // empty both yield an empty string.
    [[nodiscard]] static std::string to_utf8(BSTR borrowed);

    [[nodiscard]] BSTR get() const noexcept { return bstr_; }

    // For getters that return a BSTR through an out-parameter. Frees
    // whatever was held first.
    [[nodiscard]] BSTR* put() noexcept {
        ::SysFreeString(bstr_);
        bstr_ = nullptr;
        return &bstr_;
    }

    [[nodiscard]] std::string utf8() const { return to_utf8(bstr_); }

private:
    BSTR bstr_ = nullptr;
};

}  // namespace lsw::agent::win
