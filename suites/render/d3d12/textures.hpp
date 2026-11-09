#pragma once

#include "render/common/pattern.hpp"
#include "render/d3d12/gfx.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace lsw::suites::render::d3d12 {

class Texels {
public:
    using Bytes = std::vector<std::uint8_t>;

    [[nodiscard]] static Bytes encode(DXGI_FORMAT format, const Bytes& rgba, unsigned width, unsigned height);
    [[nodiscard]] static Bytes block_compressed(DXGI_FORMAT format, unsigned width, unsigned height, std::uint32_t seed);
    [[nodiscard]] static bool is_block(DXGI_FORMAT format);
    [[nodiscard]] static Bytes content(unsigned kind, unsigned width, unsigned height);
    [[nodiscard]] static Bytes base_content(unsigned kind, unsigned width, unsigned height);
};

struct TextureSpec {
    D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT width = 64;
    UINT height = 64;
    UINT depth_or_array = 1;
    UINT mips = 1;
    UINT samples = 1;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    unsigned content = 0;
};

class TextureFactory {
public:
    [[nodiscard]] static ID3D12Resource* create(Kit& kit, const TextureSpec& spec, bool fill, std::string& error);
};

[[nodiscard]] bool sample_scene(Kit& kit, ID3D12Resource* texture, const D3D12_SHADER_RESOURCE_VIEW_DESC& srv, const D3D12_SAMPLER_DESC* sampler,
                                const std::string& pixel_source, const float p0[4], const float p1[4], float tilt, rr::CaptureSet& out, std::string& error,
                                const D3D12_SAMPLER_DESC* second_sampler = nullptr, bool on_sphere = false);
class SampleRig {
public:
    [[nodiscard]] bool open(Kit& kit, ID3D12Resource* texture, const D3D12_SHADER_RESOURCE_VIEW_DESC& srv, const D3D12_SAMPLER_DESC& sampler, const std::string& pixel_source, std::string& error);
    [[nodiscard]] bool frame(Kit& kit, const float p0[4], const float p1[4], float tilt, bool clear, std::string& error);
    [[nodiscard]] bool capture(Kit& kit, rr::CaptureSet& out, std::string& error) { return stage.finish(out, error); }

    Stage stage;
    ID3D12PipelineState* state = nullptr;
};

[[nodiscard]] D3D12_SHADER_RESOURCE_VIEW_DESC srv_2d(DXGI_FORMAT format, UINT mips, UINT most_detailed = 0);
[[nodiscard]] std::string needs_format(Kit& kit, DXGI_FORMAT format, D3D12_FORMAT_SUPPORT1 support, const char* what);
extern const char* const kSamplePrefix;
constexpr float kFlatTilt = 1000.0f;
[[nodiscard]] rr::Tolerance sampler_tolerance();
[[nodiscard]] rr::Tolerance anisotropic_tolerance();
[[nodiscard]] rr::Tolerance loose_sampler_tolerance();
[[nodiscard]] rr::Tolerance format_tolerance();
[[nodiscard]] rr::Tolerance exact_op_tolerance();
[[nodiscard]] rr::Tolerance texel_edge_tolerance();
}  // namespace lsw::suites::render::d3d12
