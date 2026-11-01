#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace lsw::agent::apps {

// The packaged (Microsoft Store, MSIX) applications the session's user
// has, as the Start menu's All apps list knows them (the shell's
// AppsFolder), and starting one.
//
// A packaged application is named by its Application User Model ID,
// ``<package family>!<application>``; it is never started from its
// executable, which the user cannot run directly, but activated by that
// ID. Both run in the user's session helper: packages are installed per
// user, and SYSTEM in session 0 sees none of them.
class StoreApps {
public:
    StoreApps() = delete;

    // ``{"apps": [{"aumid", "name", "family", "extensions", "icon_png_base64"}]}``,
    // ``extensions`` being the file types the application is registered for,
    // sorted by name, icons ``icon_size`` pixels square. ``only``
    // non-empty answers for that one ID.
    [[nodiscard]] static nlohmann::json list(int icon_size, const std::wstring& only);

    // The started (or, for a single-instance application already
    // running, the existing) process. Throws std::runtime_error with
    // Windows' reason.
    //
    // ``documents`` (Windows paths) are opened with the application
    // instead: a packaged application takes no file on a command line,
    // only an activation for files, and ``args`` cannot go along.
    [[nodiscard]] static unsigned long activate(const std::wstring& aumid, const std::wstring& args,
                                                const std::vector<std::wstring>& documents = {});
};

}  // namespace lsw::agent::apps
