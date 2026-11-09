#pragma once

#include "render/d3d/frame_swap.hpp"
#include "render/d3d/warp_check.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/dxc_compiler.hpp"

#include <vector>

namespace lsw::suites::render::d3d12 {
struct Target {
    ID3D12Resource* texture = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE view{};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
};

struct Depth {
    ID3D12Resource* texture = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE view{};
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    UINT width = 0;
    UINT height = 0;
};

class Kit {
public:
    using Bytes = D3d12Tools::Bytes;

    [[nodiscard]] OpenStatus open(bool use_warp, const rr::CaseSpec& spec, std::string& error);

    [[nodiscard]] ID3D12Device* device() { return rig.device(); }

    [[nodiscard]] bool compile(const std::string& source, char stage, Bytes& code, std::string& error);

    [[nodiscard]] bool make_target(Target& out, UINT width, UINT height, DXGI_FORMAT format, std::string& error);
    [[nodiscard]] bool make_depth(Depth& out, UINT width, UINT height, std::string& error);
    [[nodiscard]] ID3D12Resource* make_upload(const void* data, UINT64 size, std::string& error);

    [[nodiscard]] bool end_frame(std::string& error);

    [[nodiscard]] bool run(const D3d12Queue::Recorder& record, std::string& error);

    [[nodiscard]] bool read_color(Target& target, rr::Image& out, std::string& error);
    [[nodiscard]] bool read_depth(Depth& depth, rr::Image& out, std::string& error);

    [[nodiscard]] static D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc(ID3D12RootSignature* signature, const Bytes& vs, const Bytes& ps,
                                                                          const std::vector<DXGI_FORMAT>& targets, DXGI_FORMAT depth);

    bool warp = false;
    bool dxc = false;
    std::string name;
    D3d12Rig rig;
    DxcCompiler compiler;
    bool compiler_ready = false;
    FrameSwap swap;
    std::uint64_t ring[3] = {0, 0, 0};
    unsigned ring_index = 0;
};
}  // namespace lsw::suites::render::d3d12
