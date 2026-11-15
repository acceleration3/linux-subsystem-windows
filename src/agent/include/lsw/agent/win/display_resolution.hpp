#pragma once

#include "lsw/agent/system/display_request.hpp"

#include <optional>

namespace lsw::agent::win {
class DisplayResolution {
public:
    using Resolution = system::DisplayRequest::Resolution;

    DisplayResolution() = delete;

    [[nodiscard]] static std::optional<Resolution> current();

    [[nodiscard]] static bool apply(const Resolution& wanted);

private:
    [[nodiscard]] static bool offered(const Resolution& wanted);
};

}  // namespace lsw::agent::win
