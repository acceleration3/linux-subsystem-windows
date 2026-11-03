#pragma once

#include "lsw/agent/win/environment.hpp"
#include "lsw/agent/win/process.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::agent::win {
class RunAs {
public:
    [[nodiscard]] static RunAs self();

    [[nodiscard]] static RunAs user(std::uint32_t session, bool elevated);

    [[nodiscard]] static RunAs system_in_session(std::uint32_t session);

    [[nodiscard]] bool is_self() const noexcept { return token_ == nullptr; }

    [[nodiscard]] std::uint32_t session() const noexcept { return session_; }

    [[nodiscard]] bool elevated() const noexcept { return elevated_; }

    [[nodiscard]] const std::string& account() const noexcept { return account_; }

    [[nodiscard]] const std::wstring& sid() const noexcept { return sid_; }

    [[nodiscard]] std::wstring expand(const std::wstring& s) const;

    [[nodiscard]] std::vector<wchar_t>
    environment(const std::vector<Environment::Override>& overrides) const;

    void apply(SpawnOptions& options) const;

    [[nodiscard]] std::string describe() const;

private:
    RunAs() = default;

    std::shared_ptr<void> token_;
    std::uint32_t session_ = 0;
    bool elevated_ = false;
    std::string account_;
    std::wstring sid_;
};

}
