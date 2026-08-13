#pragma once

class QKeyEvent;

namespace lsw::host::manager {

// Maps Qt keys to Win32 virtual-key codes, and decides which key events
// are text rather than commands.
//
// That second job is subtler than it looks: sending both a WM_KEYDOWN
// and a WM_CHAR for a printable key makes the guest's own
// TranslateMessage synthesise a second WM_CHAR, and every character
// arrives twice. Text goes down the WM_CHAR path only; everything else
// goes down the virtual-key path so shortcuts and navigation work.
class VirtualKeyMap {
public:
    VirtualKeyMap() = delete;

    // Win32 VK_* code for a Qt::Key, or 0 when unmapped — the WM_CHAR
    // path still delivers those as typed text.
    [[nodiscard]] static unsigned virtual_key_for(int qt_key);

    // True when the event is typed text: non-empty, not a control
    // character, and no Ctrl/Alt/Meta held.
    [[nodiscard]] static bool is_text_input(const QKeyEvent* event);
};

}  // namespace lsw::host::manager
