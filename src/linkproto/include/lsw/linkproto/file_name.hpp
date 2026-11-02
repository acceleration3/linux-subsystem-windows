#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lsw::linkproto {
class WindowsFileName {
public:
    WindowsFileName() = delete;

    static constexpr std::size_t kMaxBytes = 240;

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

}
