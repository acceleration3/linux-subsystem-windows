#include "lsw/agent/win/com.hpp"

#include "lsw/agent/log.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on

namespace lsw::agent::win {

Apartment::Apartment() {
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        initialised_ = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // Some other library on this thread initialised STA before us
        // (e.g. a stray dependency reaching ``CoInitialize`` first).
        // Live with whatever apartment exists; do NOT call
        // CoUninitialize, because we didn't actually take the init
        // refcount. UIA event handlers will route into the COM
        // global pool regardless.
        LSW_AGENT_WARN("win::Apartment: thread already in STA mode; "
                       "leaving apartment alone");
    } else {
        LSW_AGENT_ERROR(std::string{"win::Apartment: CoInitializeEx failed hr=0x"}
                        + std::to_string(static_cast<unsigned long>(hr)));
    }
}

Apartment::~Apartment() {
    if (initialised_) {
        ::CoUninitialize();
    }
}

}  // namespace lsw::agent::win
