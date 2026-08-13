#pragma once

namespace lsw::host::manager {

// Win32 ``WM_NCHITTEST`` result codes, mirrored so the host does not
// need winuser.h, plus their names for the debug overlay.
class NonClientHit {
public:
    NonClientHit() = delete;

    static constexpr int kClient = 1;
    static constexpr int kCaption = 2;
    static constexpr int kSysMenu = 3;
    static constexpr int kMinButton = 8;
    static constexpr int kMaxButton = 9;
    static constexpr int kLeft = 10;
    static constexpr int kRight = 11;
    static constexpr int kTop = 12;
    static constexpr int kTopLeft = 13;
    static constexpr int kTopRight = 14;
    static constexpr int kBottom = 15;
    static constexpr int kBottomLeft = 16;
    static constexpr int kBottomRight = 17;
    static constexpr int kClose = 20;

    // Human-readable label. Unknown codes render as "?" rather than
    // collapsing to CLIENT, so custom chrome returning something
    // unfamiliar stays visible in the overlay.
    [[nodiscard]] static const char* name(int code);
};

}  // namespace lsw::host::manager
