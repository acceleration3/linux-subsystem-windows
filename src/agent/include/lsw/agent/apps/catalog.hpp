#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::apps {

// Lists the applications the session's user has, as a JSON object matching
// the manager's parser:
//   { "apps": [ {"id": "...", "name": "...", "exe_path": "...", "args"?: "...",
//                 "extensions": [".xlsx", ...], "icon_png_base64": "..."}, ... ] }
//
// Source: the Start menu's All apps list (shell:AppsFolder), whose
// shortcuts say exactly which program they start and with which
// arguments: a shortcut to an existing .exe outside Windows' own folder
// that is not an uninstaller is an application, several for one program
// are one (the one with no arguments wins). Beside it, the machine-wide
// Add/Remove Programs entries (the ``Uninstall`` key, in both registry
// views) for what has no shortcut, only when they name their program for
// certain: a DisplayIcon that is an existing .exe, or an .exe in
// InstallLocation named like the entry.
//
// Per user, so it runs in the user's session.
class Catalog {
public:
    [[nodiscard]] static nlohmann::json list();
};

}  // namespace lsw::agent::apps
