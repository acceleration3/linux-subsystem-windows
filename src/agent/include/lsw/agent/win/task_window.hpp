#pragma once

#include <cstdint>
#include <string_view>

namespace lsw::agent::win {
struct TaskWindow {
    enum class Front { None, Mine, Another };

    static constexpr std::uint32_t kToolWindow = 0x00000080;
    static constexpr std::uint32_t kAppWindow = 0x00040000;
    static constexpr std::uint32_t kNoActivate = 0x08000000;

    bool visible = false;
    bool cloaked = false;
    bool top_level = false;
    bool shell_window = false;
    std::uint32_t extended_style = 0;
    std::wstring_view class_name;
    bool owned = false;
    bool owner_shown = false;
    Front front = Front::None;

    [[nodiscard]] static bool styled_as_task(std::uint32_t extended_style) noexcept;
    [[nodiscard]] static bool system_surface(std::wstring_view class_name) noexcept;

    [[nodiscard]] bool qualifies() const noexcept;
};

}  // namespace lsw::agent::win
