#pragma once

#include <wrl/client.h>

namespace lsw::agent::win {

// Owning COM interface pointer: Release on destruction, AddRef on copy.
//
// WRL's, under a shorter name. Every UIA call hands back a reference the
// caller must Release, and the code that did that by hand had a Release
// on every exit path of every query — the kind of bookkeeping one missed
// early return turns into a leak. Where the ORDER of releases matters
// (anything torn down relative to the COM apartment), the owner still
// calls ``Reset()`` explicitly rather than relying on destruction order.
template<class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

}  // namespace lsw::agent::win
