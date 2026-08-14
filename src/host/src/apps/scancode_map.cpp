#include "lsw/host/apps/scancode_map.hpp"

namespace lsw::host::apps {

namespace {

// SDL's scancodes are USB HID usage codes, and these are the ones from
// the HID keyboard page. Spelled as numbers rather than pulled from
// SDL's headers so that nothing outside the render layer has to include
// SDL to talk about a key.
enum : unsigned {
    kA = 4,
    kZ = 29,
    k1 = 30,
    k9 = 38,
    k0 = 39,
    kEnter = 40,
    kEscape = 41,
    kBackspace = 42,
    kTab = 43,
    kSpace = 44,
    kMinus = 45,
    kEquals = 46,
    kLeftBracket = 47,
    kRightBracket = 48,
    kBackslash = 49,
    kSemicolon = 51,
    kApostrophe = 52,
    kGrave = 53,
    kComma = 54,
    kPeriod = 55,
    kSlash = 56,
    kCapsLock = 57,
    kF1 = 58,
    kF12 = 69,
    kPrintScreen = 70,
    kScrollLock = 71,
    kPause = 72,
    kInsert = 73,
    kHome = 74,
    kPageUp = 75,
    kDelete = 76,
    kEnd = 77,
    kPageDown = 78,
    kRight = 79,
    kLeft = 80,
    kDown = 81,
    kUp = 82,
    kNumLock = 83,
    kKpDivide = 84,
    kKpMultiply = 85,
    kKpMinus = 86,
    kKpPlus = 87,
    kKpEnter = 88,
    kKp1 = 89,
    kKp9 = 97,
    kKp0 = 98,
    kKpPeriod = 99,
    kLeftCtrl = 224,
    kLeftShift = 225,
    kLeftAlt = 226,
    kLeftGui = 227,
    kRightCtrl = 228,
    kRightShift = 229,
    kRightAlt = 230,
    kRightGui = 231,
};

// Win32 virtual-key codes. Also written out rather than included:
// windows.h is not available on the host, and these values are part of
// a wire contract that cannot change anyway.
enum : unsigned {
    kVkBack = 0x08,
    kVkTab = 0x09,
    kVkReturn = 0x0D,
    kVkShift = 0x10,
    kVkControl = 0x11,
    kVkMenu = 0x12,
    kVkPause = 0x13,
    kVkCapital = 0x14,
    kVkEscape = 0x1B,
    kVkSpace = 0x20,
    kVkPrior = 0x21,
    kVkNext = 0x22,
    kVkEnd = 0x23,
    kVkHome = 0x24,
    kVkLeft = 0x25,
    kVkUp = 0x26,
    kVkRight = 0x27,
    kVkDown = 0x28,
    kVkSnapshot = 0x2C,
    kVkInsert = 0x2D,
    kVkDelete = 0x2E,
    kVk0 = 0x30,
    kVkA = 0x41,
    kVkLwin = 0x5B,
    kVkRwin = 0x5C,
    kVkNumpad0 = 0x60,
    kVkMultiply = 0x6A,
    kVkAdd = 0x6B,
    kVkSubtract = 0x6D,
    kVkDecimal = 0x6E,
    kVkDivide = 0x6F,
    kVkF1 = 0x70,
    kVkNumlock = 0x90,
    kVkScroll = 0x91,
    kVkOem1 = 0xBA,       // ;:
    kVkOemPlus = 0xBB,
    kVkOemComma = 0xBC,
    kVkOemMinus = 0xBD,
    kVkOemPeriod = 0xBE,
    kVkOem2 = 0xBF,       // /?
    kVkOem3 = 0xC0,       // `~
    kVkOem4 = 0xDB,       // [{
    kVkOem5 = 0xDC,       // \|
    kVkOem6 = 0xDD,       // ]}
    kVkOem7 = 0xDE,       // '"
};

}  // namespace

unsigned ScancodeMap::virtual_key_for(unsigned hid_usage) {
    // Runs of contiguous keys first: the HID page orders letters,
    // digits, function keys and the keypad the same way Windows does,
    // so the whole run is one line each.
    if (hid_usage >= kA && hid_usage <= kZ) {
        return kVkA + (hid_usage - kA);
    }
    if (hid_usage >= k1 && hid_usage <= k9) {
        return kVk0 + 1 + (hid_usage - k1);
    }
    if (hid_usage >= kF1 && hid_usage <= kF12) {
        return kVkF1 + (hid_usage - kF1);
    }
    if (hid_usage >= kKp1 && hid_usage <= kKp9) {
        return kVkNumpad0 + 1 + (hid_usage - kKp1);
    }

    switch (hid_usage) {
    case k0:            return kVk0;
    case kKp0:          return kVkNumpad0;
    case kEnter:        return kVkReturn;
    case kKpEnter:      return kVkReturn;
    case kEscape:       return kVkEscape;
    case kBackspace:    return kVkBack;
    case kTab:          return kVkTab;
    case kSpace:        return kVkSpace;
    case kMinus:        return kVkOemMinus;
    case kEquals:       return kVkOemPlus;
    case kLeftBracket:  return kVkOem4;
    case kRightBracket: return kVkOem6;
    case kBackslash:    return kVkOem5;
    case kSemicolon:    return kVkOem1;
    case kApostrophe:   return kVkOem7;
    case kGrave:        return kVkOem3;
    case kComma:        return kVkOemComma;
    case kPeriod:       return kVkOemPeriod;
    case kSlash:        return kVkOem2;
    case kCapsLock:     return kVkCapital;
    case kPrintScreen:  return kVkSnapshot;
    case kScrollLock:   return kVkScroll;
    case kPause:        return kVkPause;
    case kInsert:       return kVkInsert;
    case kHome:         return kVkHome;
    case kPageUp:       return kVkPrior;
    case kDelete:       return kVkDelete;
    case kEnd:          return kVkEnd;
    case kPageDown:     return kVkNext;
    case kRight:        return kVkRight;
    case kLeft:         return kVkLeft;
    case kDown:         return kVkDown;
    case kUp:           return kVkUp;
    case kNumLock:      return kVkNumlock;
    case kKpDivide:     return kVkDivide;
    case kKpMultiply:   return kVkMultiply;
    case kKpMinus:      return kVkSubtract;
    case kKpPlus:       return kVkAdd;
    case kKpPeriod:     return kVkDecimal;
    // The guest injector takes the unsided modifiers; which physical
    // shift was pressed is not something an application reads through
    // this path.
    case kLeftCtrl:
    case kRightCtrl:    return kVkControl;
    case kLeftShift:
    case kRightShift:   return kVkShift;
    case kLeftAlt:
    case kRightAlt:     return kVkMenu;
    case kLeftGui:      return kVkLwin;
    case kRightGui:     return kVkRwin;
    default:            return 0;
    }
}

}  // namespace lsw::host::apps
