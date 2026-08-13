#include "lsw/host/manager/virtual_key_map.hpp"

#include <QKeyEvent>
#include <QString>

namespace lsw::host::manager {

unsigned VirtualKeyMap::virtual_key_for(int qt_key) {
    // VK_* constants are inlined: they are Win32 values and the host is
    // Linux, so there is no header to take them from.
    if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) {
        return static_cast<unsigned>('A' + (qt_key - Qt::Key_A));
    }
    if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) {
        return static_cast<unsigned>('0' + (qt_key - Qt::Key_0));
    }
    if (qt_key >= Qt::Key_F1 && qt_key <= Qt::Key_F24) {
        return 0x70u + static_cast<unsigned>(qt_key - Qt::Key_F1);  // VK_F1..
    }
    switch (qt_key) {
    case Qt::Key_Backspace:    return 0x08;  // VK_BACK
    case Qt::Key_Tab:          return 0x09;  // VK_TAB
    case Qt::Key_Backtab:      return 0x09;
    case Qt::Key_Return:
    case Qt::Key_Enter:        return 0x0D;  // VK_RETURN
    case Qt::Key_Escape:       return 0x1B;  // VK_ESCAPE
    case Qt::Key_Space:        return 0x20;  // VK_SPACE
    case Qt::Key_PageUp:       return 0x21;  // VK_PRIOR
    case Qt::Key_PageDown:     return 0x22;  // VK_NEXT
    case Qt::Key_End:          return 0x23;  // VK_END
    case Qt::Key_Home:         return 0x24;  // VK_HOME
    case Qt::Key_Left:         return 0x25;  // VK_LEFT
    case Qt::Key_Up:           return 0x26;  // VK_UP
    case Qt::Key_Right:        return 0x27;  // VK_RIGHT
    case Qt::Key_Down:         return 0x28;  // VK_DOWN
    case Qt::Key_Insert:       return 0x2D;  // VK_INSERT
    case Qt::Key_Delete:       return 0x2E;  // VK_DELETE
    case Qt::Key_Shift:        return 0x10;  // VK_SHIFT
    case Qt::Key_Control:      return 0x11;  // VK_CONTROL
    case Qt::Key_Alt:          return 0x12;  // VK_MENU
    case Qt::Key_CapsLock:     return 0x14;  // VK_CAPITAL
    case Qt::Key_Meta:         return 0x5B;  // VK_LWIN
    case Qt::Key_Semicolon:    return 0xBA;  // VK_OEM_1
    case Qt::Key_Equal:        return 0xBB;  // VK_OEM_PLUS
    case Qt::Key_Comma:        return 0xBC;  // VK_OEM_COMMA
    case Qt::Key_Minus:        return 0xBD;  // VK_OEM_MINUS
    case Qt::Key_Period:       return 0xBE;  // VK_OEM_PERIOD
    case Qt::Key_Slash:        return 0xBF;  // VK_OEM_2
    case Qt::Key_QuoteLeft:    return 0xC0;  // VK_OEM_3
    case Qt::Key_BracketLeft:  return 0xDB;  // VK_OEM_4
    case Qt::Key_Backslash:    return 0xDC;  // VK_OEM_5
    case Qt::Key_BracketRight: return 0xDD;  // VK_OEM_6
    case Qt::Key_Apostrophe:   return 0xDE;  // VK_OEM_7
    default:                   return 0;
    }
}

bool VirtualKeyMap::is_text_input(const QKeyEvent* event) {
    if (event == nullptr) {
        return false;
    }
    const QString text = event->text();
    if (text.isEmpty() || text[0].unicode() < 0x20) {
        return false;  // control characters are commands, not text
    }
    return (event->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == 0;
}

}  // namespace lsw::host::manager
