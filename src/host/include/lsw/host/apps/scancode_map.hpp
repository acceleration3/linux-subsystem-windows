#pragma once

namespace lsw::host::apps {

// USB HID usage code -> Win32 virtual-key code.
//
// The replay's window reports keys by scancode, which names the
// physical key and says nothing about layout — the right thing to send
// to a guest that has its own keyboard layout and will map the key
// itself. Windows wants a virtual-key code, so the two numbering
// schemes have to be bridged somewhere, and this is the only place
// either is mentioned.
//
// Only keys a remote application plausibly reads are listed. An
// unmapped scancode returns 0, and the caller drops it: sending
// VK_UNKNOWN would be indistinguishable, at the far end, from a key
// that really was pressed.
class ScancodeMap {
public:
    ScancodeMap() = delete;

    [[nodiscard]] static unsigned virtual_key_for(unsigned hid_usage);
};

}  // namespace lsw::host::apps
