#pragma once

#include <string>
#include <windows.h>

namespace lsw::agent::win {
class SecurityAttributes {
public:
    explicit SecurityAttributes(const std::wstring& sddl);
    ~SecurityAttributes();

    SecurityAttributes(const SecurityAttributes&) = delete;
    SecurityAttributes& operator=(const SecurityAttributes&) = delete;

    [[nodiscard]] bool ok() const noexcept { return descriptor_ != nullptr; }

    [[nodiscard]] SECURITY_ATTRIBUTES* get() noexcept { return &attributes_; }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

}
