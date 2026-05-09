#pragma once

#include <string>

namespace lsw::agent {

// Discovers Start menu apps and serializes them as a JSON object
// matching the manager's parser:
//   { "apps": [ {"id": "...", "name": "...", "exe_path": "...",
//                 "icon_png_base64": "..."}, ... ] }
//
// Sources:
//   * PowerShell ``Get-StartApps`` for AUMID-style modern apps.
//   * ``%ProgramData%\Microsoft\Windows\Start Menu\Programs`` and
//     the per-user equivalent for traditional .lnk installs.
class AppCatalog {
public:
    [[nodiscard]] static std::string list_json();
};

}  // namespace lsw::agent
