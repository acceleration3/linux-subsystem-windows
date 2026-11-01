#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lsw::linkproto {

// What Windows will take as the name of one file or folder, once, for both
// ends: the manager decides whether a host file can be reached by its name
// through a shared folder and what to call a copy of one it cannot, and the
// agent refuses a name it cannot create. Two copies of the rule disagreed
// (the agent also refused ``%`` and ``!``, which are legal), so there is
// one.
//
// UTF-8 in, UTF-8 out. Not a path: separators are illegal here.
class WindowsFileName {
public:
    WindowsFileName() = delete;

    // The longest name accepted, in bytes: well under NTFS's 255 UTF-16
    // units for any text, leaving room for the folder around it.
    static constexpr std::size_t kMaxBytes = 240;

    // A name Windows can create: not empty, not too long, none of
    // ``<>:"|?*`` or a separator or a control character, not ending in a dot
    // or a space, and not a device's name (CON, NUL, COM1 ... LPT9, CONIN$,
    // CONOUT$) with or without an extension.
    [[nodiscard]] static bool legal(std::string_view name) {
        if (name.empty() || name.size() > kMaxBytes || name.back() == '.' || name.back() == ' ') {
            return false;
        }
        for (const char raw : name) {
            const auto c = static_cast<unsigned char>(raw);
            if (c < 32 || c == 127 || std::string_view{"<>:\"|?*/\\"}.find(raw) != std::string_view::npos) {
                return false;
            }
        }
        return !reserved(name);
    }

    // The nearest name Windows can create: illegal characters become ``_``,
    // a trailing dot or space goes, a device's name gets a ``_`` in front,
    // and one too long is cut (its extension kept). Never empty. A name that
    // is already legal comes back as it is.
    [[nodiscard]] static std::string sanitised(std::string_view name) {
        std::string out;
        for (const char raw : name) {
            const auto c = static_cast<unsigned char>(raw);
            const bool bad = c < 32 || c == 127 || std::string_view{"<>:\"|?*/\\"}.find(raw) != std::string_view::npos;
            out += bad ? '_' : raw;
        }
        while (!out.empty() && (out.back() == '.' || out.back() == ' ')) {
            out.pop_back();
        }
        if (out.empty()) {
            out = "file";
        }
        if (reserved(out)) {
            out.insert(out.begin(), '_');
        }
        if (out.size() > kMaxBytes) {
            const std::size_t dot = out.rfind('.');
            const std::string extension = dot != std::string::npos && out.size() - dot <= 20 ? out.substr(dot) : "";
            std::size_t keep = kMaxBytes - extension.size();
            // Not in the middle of a UTF-8 character.
            while (keep > 0 && (static_cast<unsigned char>(out[keep]) & 0xC0) == 0x80) {
                --keep;
            }
            out = out.substr(0, keep) + extension;
        }
        return out;
    }

private:
    static bool reserved(std::string_view name) {
        std::string stem{name.substr(0, name.find('.'))};
        while (!stem.empty() && stem.back() == ' ') {
            stem.pop_back();
        }
        for (char& c : stem) {
            if (c >= 'a' && c <= 'z') {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        return stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" ||
               stem == "CONOUT$" ||
               (stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) &&
                stem[3] >= '1' && stem[3] <= '9');
    }
};

}  // namespace lsw::linkproto
