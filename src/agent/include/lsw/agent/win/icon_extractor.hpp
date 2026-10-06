#pragma once

#include <string>

struct HBITMAP__;
using HBITMAP = HBITMAP__*;

namespace lsw::agent::win {

// Pulls an application icon out of the guest and hands it back as a
// base64 PNG, ready to drop into an RPC reply.
class IconExtractor {
public:
    IconExtractor() = delete;

    // Largest icon resource in ``path``, as base64 PNG. Empty on failure.
    [[nodiscard]] static std::string from_executable(const std::wstring& path);
    // A 32-bit bitmap with premultiplied alpha, as the shell's image
    // factory hands one out, as base64 PNG. Empty on failure. The bitmap
    // stays the caller's.
    [[nodiscard]] static std::string from_premultiplied_bitmap(HBITMAP bitmap);
};

}  // namespace lsw::agent::win
