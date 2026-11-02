#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lsw::agent::core {
class Paths {
public:
    Paths() = delete;

    [[nodiscard]] static std::wstring root();

    [[nodiscard]] static std::wstring logs_dir();
    [[nodiscard]] static std::wstring mirror_logs_dir();

    [[nodiscard]] static std::wstring driver_logs_dir();

    [[nodiscard]] static std::wstring agent_log();

    [[nodiscard]] static std::wstring session_helper_log(std::uint32_t session);

    [[nodiscard]] static std::wstring desktop_ready_flag();

    [[nodiscard]] static std::wstring release_file();
    [[nodiscard]] static std::wstring update_result_file();

    [[nodiscard]] static std::wstring work_dir();

    [[nodiscard]] static std::wstring remotewin_guest_exe();
    [[nodiscard]] static std::wstring remotewin_guest_log(std::string_view name);
    [[nodiscard]] static std::wstring remotewin_desktop_log();
};

}
