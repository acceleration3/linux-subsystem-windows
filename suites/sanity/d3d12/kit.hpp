#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "core/text.hpp"
#include "support/artifact_writer.hpp"
#include "support/d3d12_frame.hpp"
#include "support/d3d12_oracle.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/target_adapter.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace lsw::suites::sanity::d3d12 {
class Kit {
public:
    using Body = std::function<std::string(D3d12Rig&, Problems&)>;

    [[nodiscard]] static Outcome each_device(Context& context, D3D_FEATURE_LEVEL level, const Body& body);

    [[nodiscard]] static Outcome one_device(Context& context, D3D_FEATURE_LEVEL level, const Body& body);

    [[nodiscard]] static Outcome finish(const std::vector<std::string>& skips, const Problems& problems);

    static bool expect(Problems& problems, const std::string& what, HRESULT got, std::initializer_list<HRESULT> accepted);

    static bool succeeds(Problems& problems, const std::string& what, HRESULT got) {
        return expect(problems, what, got, {S_OK});
    }

    [[nodiscard]] static std::string hr(HRESULT result) { return D3d12Tools::hr(result); }

    [[nodiscard]] static std::string sanitize(std::string text);

    static bool same_bytes(Context& context, Problems& problems, const std::string& what, const void* expected,
                           const void* actual, std::size_t size);

    [[nodiscard]] static std::vector<std::uint8_t> bytes(std::uint64_t seed, std::size_t size);

    [[nodiscard]] static std::vector<std::uint32_t> words(const std::vector<std::uint8_t>& raw);

    [[nodiscard]] static std::uint32_t rgba(int r, int g, int b, int a) {
        return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
               (static_cast<std::uint32_t>(a) << 24);
    }

    [[nodiscard]] static bool uniform(const TexelImage& image, std::uint32_t value, std::string& where);

    [[nodiscard]] static std::uint32_t rgba_pixel(const TexelImage& image, UINT x, UINT y) { return image.pixel(x, y); }

    [[nodiscard]] static std::uint64_t device_local_budget(D3d12Rig& rig);

    [[nodiscard]] static std::uint64_t host_budget(D3d12Rig& rig);

    [[nodiscard]] static bool hazard_allowed();

    [[nodiscard]] static D3D12_RESOURCE_BINDING_TIER binding_tier(D3d12Rig& rig);

    [[nodiscard]] static D3D12_RESOURCE_HEAP_TIER heap_tier(D3d12Rig& rig);

    [[nodiscard]] static D3D12_HEAP_FLAGS buffer_heap_flags(D3d12Rig& rig);

    [[nodiscard]] static D3D12_HEAP_FLAGS texture_heap_flags(D3d12Rig& rig, bool render_target);

    static void compare_words(Context& context, Problems& problems, const std::string& what,
                              const std::vector<std::uint32_t>& expected, const std::vector<std::uint32_t>& actual);

    [[nodiscard]] static std::string vs_source();

    static constexpr const char* kConstantVs =
        "cbuffer C : register(b0) { float z; float r; float g; float b; }\n"
        "float4 main(uint id : SV_VertexID) : SV_Position {\n"
        "  float2 p = float2((id << 1) & 2, id & 2);\n"
        "  return float4(p * float2(2, -2) + float2(-1, 1), z, 1);\n"
        "}\n";

    static constexpr const char* kConstantPs =
        "cbuffer C : register(b0) { float z; float r; float g; float b; }\n"
        "float4 main() : SV_Target { return float4(r, g, b, 1); }\n";

    static constexpr const char* kSolidPs = "float4 main() : SV_Target { return float4(0.2, 0.6, 1.0, 1.0); }";

    [[nodiscard]] static std::uint32_t solid_color() { return rgba(51, 153, 255, 255); }

    [[nodiscard]] static ID3D12PipelineState* pipeline(D3d12Rig& rig, ID3D12RootSignature* signature, const std::string& ps_source,
                                                       DXGI_FORMAT target, std::string& error,
                                                       const std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)>& tweak = {});

    [[nodiscard]] static ID3D12PipelineState* pipeline_with(D3d12Rig& rig, ID3D12RootSignature* signature, const std::string& vs_source,
                                                            const std::string& ps_source, DXGI_FORMAT target, std::string& error,
                                                            const std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)>& tweak = {});

    static void draw_fullscreen(ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature, ID3D12PipelineState* state);

    [[nodiscard]] static std::vector<std::uint32_t> image_words(const TexelImage& image);
};
}  // namespace lsw::suites::sanity::d3d12
