#pragma once

#include "support/com_ptr.hpp"
#include "support/d3d12_rig.hpp"

#include <cstdint>
#include <string>

namespace lsw::suites {
class SharedFence {
public:
    [[nodiscard]] static bool create(D3d12Rig& rig, const std::string& name, Com<ID3D12Fence>& fence, HANDLE& handle,
                                     std::string& error);

    [[nodiscard]] static bool open(D3d12Rig& rig, const std::string& name, Com<ID3D12Fence>& fence, std::string& error);

    [[nodiscard]] static bool reaches(ID3D12Fence* fence, std::uint64_t value, DWORD timeout_ms);
};
}  // namespace lsw::suites
