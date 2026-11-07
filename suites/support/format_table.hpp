#pragma once

#include <dxgiformat.h>

#include <vector>

namespace lsw::suites {
struct FormatEntry {
    DXGI_FORMAT dxgi;
    const char* name;
    int vulkan;
    unsigned gl_internal;
    unsigned gl_format;
    unsigned gl_type;
    int d3d9;
    bool packed;
};

class FormatTable {
public:
    [[nodiscard]] static const std::vector<FormatEntry>& all();

    [[nodiscard]] static const FormatEntry* find(DXGI_FORMAT format);

    [[nodiscard]] static const char* name(DXGI_FORMAT format);

    [[nodiscard]] static std::vector<DXGI_FORMAT> uncompressed_color();
};
}  // namespace lsw::suites
