#pragma once

#include <nlohmann/json.hpp>

namespace lsw::agent::apps {

// Lists the installed applications as a JSON object matching the
// manager's parser:
//   { "apps": [ {"id": "...", "name": "...", "exe_path": "...",
//                 "icon_png_base64": "..."}, ... ] }
//
// Source: the machine-wide Add/Remove Programs entries — the
// ``Uninstall`` registry key, in both the 64-bit and 32-bit views — with
// system components, updates and redistributables filtered out. An entry
// is listed only if it resolves to an .exe, through DisplayIcon or the
// first executable in InstallLocation.
class Catalog {
public:
    [[nodiscard]] static nlohmann::json list();
};

}  // namespace lsw::agent::apps
