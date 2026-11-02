#pragma once

#include <string>

struct HBITMAP__;
using HBITMAP = HBITMAP__*;

namespace lsw::agent::win {
class IconExtractor {
public:
    IconExtractor() = delete;

    [[nodiscard]] static std::string from_executable(const std::wstring& path);
    [[nodiscard]] static std::string from_premultiplied_bitmap(HBITMAP bitmap);
};

}
