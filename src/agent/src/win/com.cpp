#include "lsw/agent/win/com.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/error_text.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on

namespace lsw::agent::win {

Apartment::Apartment(Model model) {
    const HRESULT hr = ::CoInitializeEx(
        nullptr, model == Model::Multithreaded ? COINIT_MULTITHREADED : COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr)) {
        // S_FALSE too: COM was already up on this thread in this model,
        // and the call still took a reference that must be released.
        initialised_ = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
        // Something on this thread initialised the other model before us
        // (e.g. a stray dependency reaching ``CoInitialize`` first).
        // Live with whatever apartment exists; do NOT call
        // CoUninitialize, because we didn't actually take the init
        // refcount. UIA event handlers will route into the COM
        // global pool regardless.
        LSW_AGENT_WARN("win::Apartment: thread already in the other apartment model; "
                       "leaving apartment alone");
    } else {
        LSW_AGENT_ERROR("win::Apartment: CoInitializeEx failed hr={}", ErrorText::hresult(hr));
    }
}

Apartment::~Apartment() {
    if (initialised_) {
        ::CoUninitialize();
    }
}

}  // namespace lsw::agent::win
