#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "sanity/iso/fleet.hpp"
#include "support/problems.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace lsw::suites::sanity::life {
class Kit {
public:
    [[nodiscard]] static std::vector<DWORD> process_ids(const std::wstring& exe);

    [[nodiscard]] static bool run(const std::wstring& command, DWORD& exit_code, int timeout_ms = 60000);

    [[nodiscard]] static bool restart_dwm(int timeout_ms, double& ms, std::string& error);

    [[nodiscard]] static Outcome app_role(Context& context, const std::string& role);

    [[nodiscard]] static std::vector<std::string> app_apis();

    static void judge_apps(Problems& problems, const iso::Fleet& fleet, const std::string& what);

    [[nodiscard]] static std::vector<DEVMODEW> modes();

    [[nodiscard]] static bool current_mode(DEVMODEW& mode);

    [[nodiscard]] static LONG set_mode(const DEVMODEW& mode);

    static void restore_mode();

    [[nodiscard]] static bool desktop_shows(const RECT& area, COLORREF color, int timeout_ms, double& ms);
};
}  // namespace lsw::suites::sanity::life
