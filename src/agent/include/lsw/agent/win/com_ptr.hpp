#pragma once

#include <wrl/client.h>

namespace lsw::agent::win {
template<class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

}
