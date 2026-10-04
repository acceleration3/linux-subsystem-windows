#pragma once

#include <string>
#include <windows.h>

namespace lsw::agent::win {

// SECURITY_ATTRIBUTES built from an SDDL string, owning the descriptor.
//
// For the kernel objects the agent creates on purpose for someone else
// to open — a session helper's pipe, a launch's job object — the default
// DACL (the creator's) is the wrong answer in both directions: a service
// creates them as SYSTEM, which leaves the user who must open them out,
// and widening that by hand-built ACL calls is where mistakes hide. SDDL
// states the whole policy in one line.
//
// Handles created through these attributes are never inheritable.
class SecurityAttributes {
public:
    explicit SecurityAttributes(const std::wstring& sddl);
    ~SecurityAttributes();

    SecurityAttributes(const SecurityAttributes&) = delete;
    SecurityAttributes& operator=(const SecurityAttributes&) = delete;

    // False when the SDDL did not parse; ``get`` must not be used then.
    [[nodiscard]] bool ok() const noexcept { return descriptor_ != nullptr; }

    [[nodiscard]] SECURITY_ATTRIBUTES* get() noexcept { return &attributes_; }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    SECURITY_ATTRIBUTES attributes_{};
};

}  // namespace lsw::agent::win
