#pragma once

#include <nlohmann/json_fwd.hpp>
#include <optional>

namespace lsw::agent::system {
class DisplayRequest {
public:
    struct Resolution {
        int width = 0;
        int height = 0;

        [[nodiscard]] bool operator==(const Resolution&) const noexcept = default;
    };

    explicit DisplayRequest(const nlohmann::json& params);

    [[nodiscard]] int scaling_percent() const noexcept { return scaling_percent_; }

    [[nodiscard]] const std::optional<Resolution>& resolution() const noexcept {
        return resolution_;
    }

    static void answer(nlohmann::json& reply, const std::optional<Resolution>& now);

private:
    int scaling_percent_ = 0;
    std::optional<Resolution> resolution_;
};

}  // namespace lsw::agent::system
