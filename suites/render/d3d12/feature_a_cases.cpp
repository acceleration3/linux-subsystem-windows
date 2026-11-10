#include "render/common/cpu_raster.hpp"
#include "render/d3d12/scene_util.hpp"
#include "support/d3d12_command_list8.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

constexpr DXGI_FORMAT kFeedbackMinMip = static_cast<DXGI_FORMAT>(189);

std::string needs_options6(Kit& kit, D3D12_VARIABLE_SHADING_RATE_TIER tier) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
    const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS6, options);
    return SUCCEEDED(result) && options.VariableShadingRateTier >= tier ? std::string() : std::string("variable rate shading tier 2");
}

std::string needs_samples_4(Kit& kit) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels{};
    levels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    levels.SampleCount = 4;
    const HRESULT result = kit.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof levels);
    return SUCCEEDED(result) && levels.NumQualityLevels > 0 ? std::string() : std::string("4x multisampling");
}

std::string needs_programmable_positions(Kit& kit, const char* what) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
    const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS2, options);
    if (FAILED(result) || options.ProgrammableSamplePositionsTier == D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_NOT_SUPPORTED) {
        return what;
    }
    return {};
}

bool read_raw_image(Kit& kit, ID3D12Resource* texture, D3D12_RESOURCE_STATES state, UINT pixel_multiplier, rr::Image& out, std::string& error) {
    TexelImage texels;
    if (!D3d12Tools::read_texture(kit.rig, texture, state, 0, texels, error)) {
        return false;
    }
    rr::Image image(texels.width * pixel_multiplier, texels.height, rr::PixelKind::Rgba8Uint);
    if (texels.row_bytes != image.stride() || texels.data.size() != image.bytes().size()) {
        return fail_with(error, "raw readback size does not match");
    }
    std::memcpy(image.bytes().data(), texels.data.data(), image.bytes().size());
    out = std::move(image);
    return true;
}

const char* const kVrsPs =
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
    "float noise(uint2 c) { uint x = (c.x * 73856093u) ^ (c.y * 19349663u); x ^= x >> 13; x *= 1274126177u; x ^= x >> 16; return float(x & 255u) / 255.0; }\n"
    "float4 main(VsOut i, uint rate : SV_ShadingRate) : SV_Target {\n"
    "  uint2 b = uint2(1u << ((rate >> 2) & 3u), 1u << (rate & 3u));\n"
    "  float h = noise(uint2(i.pos.xy) / b);\n"
    "  float l = saturate(dot(normalize(i.nrm), normalize(float3(0.5, 0.8, 0.6)))) * 0.6 + 0.4;\n"
    "  return float4(lerp(p0.rgb * l, float3(h, h, h), 0.5), 1); }\n";

const char* const kVrsBandVs =
    "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint id : SV_VertexID; };\n"
    "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; uint rate : SV_ShadingRate; };\n"
    "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.nrm = i.nrm; o.uv = i.uv; uint band = (i.id / 3) / 2; o.rate = band == 0 ? 5 : (band == 1 ? 4 : 0); return o; }\n";

struct VrsBlock {
    unsigned x = 1;
    unsigned y = 1;
};

enum class VrsRows { ImageTiles, CombinerMax, PerPrimitive };

VrsBlock vrs_tile_rate(unsigned tx, unsigned ty, unsigned tiles) {
    if (tx < tiles / 2) {
        return {1, 1};
    }
    if (ty < tiles / 2) {
        return {2, 2};
    }
    return (tx + ty) % 2 == 0 ? VrsBlock{2, 1} : VrsBlock{1, 2};
}

VrsBlock vrs_block_at(VrsRows rows, unsigned tile, unsigned x, unsigned y) {
    const unsigned tiles = (128 + tile - 1) / tile;
    switch (rows) {
        case VrsRows::ImageTiles:
            return vrs_tile_rate(x / tile, y / tile, tiles);
        case VrsRows::CombinerMax:
            return x / tile < tiles / 2 ? VrsBlock{2, 2} : VrsBlock{2, 1};
        case VrsRows::PerPrimitive:
            if (x >= 16 && x < 48) {
                return {2, 2};
            }
            if (x >= 48 && x < 80) {
                return {2, 1};
            }
            return {1, 1};
    }
    return {};
}

std::vector<Vertex> vrs_vertices(VrsRows rows) {
    std::vector<Vertex> out;
    auto corner = [&](float px, float py) {
        const float x = px / 64.0f - 1.0f;
        const float y = 1.0f - py / 64.0f;
        out.push_back({{x, y, 0.5f}, {0.5f * x, 0.5f * y, 1.0f}, {px / 128.0f, py / 128.0f}});
    };
    auto rectangle = [&](float x0, float y0, float x1, float y1) {
        corner(x0, y0);
        corner(x1, y0);
        corner(x0, y1);
        corner(x1, y0);
        corner(x1, y1);
        corner(x0, y1);
    };
    if (rows == VrsRows::PerPrimitive) {
        rectangle(16, 16, 48, 112);
        rectangle(48, 16, 80, 112);
        rectangle(80, 16, 112, 112);
    } else {
        rectangle(0, 0, 128, 128);
    }
    return out;
}

bool draw_vrs_scene(Kit& kit, VrsRows rows, const std::function<void(ID3D12GraphicsCommandList5*, Stage&)>& configure, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
        return false;
    }
    const std::vector<Vertex> vertices = vrs_vertices(rows);
    Geometry geometry;
    if (!geometry.create_vertices(kit, vertices.data(), static_cast<UINT>(vertices.size()), sizeof(Vertex), error)) {
        return false;
    }
    Pso builder(kit, stage.canvas.binder.signature);
    builder.vs(Hlsl12::with_root(rows == VrsRows::PerPrimitive ? kVrsBandVs : Hlsl12::kLitVs)).ps(Hlsl12::with_root(kVrsPs)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
    ID3D12PipelineState* state = builder.build(error);
    if (state == nullptr) {
        return false;
    }
    bool have_list5 = true;
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            ID3D12GraphicsCommandList5* list5 = nullptr;
            if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList5), reinterpret_cast<void**>(&list5)))) {
                have_list5 = false;
                return;
            }
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            list->SetPipelineState(state);
            configure(list5, stage);
            const float color[4] = {0.9f, 0.6f, 0.2f, 1.0f};
            set_cbv_root(list, rr::Mat4(), color);
            geometry.bind(list);
            geometry.draw_vertices(list);
            list5->Release();
        },
        error);
    if (!have_list5) {
        return fail_with(error, "ID3D12GraphicsCommandList5 is not available");
    }
    return ok && stage.finish(out, error);
}

std::uint32_t vrs_noise(std::uint32_t cx, std::uint32_t cy) {
    std::uint32_t x = (cx * 73856093u) ^ (cy * 19349663u);
    x ^= x >> 13;
    x *= 1274126177u;
    x ^= x >> 16;
    return x & 255u;
}

bool vrs_reference(Kit& device, VrsRows rows, rr::CaptureSet& out, std::string&) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
    unsigned tile = 16;
    if (SUCCEEDED(D3d12Tools::feature(device.device(), D3D12_FEATURE_D3D12_OPTIONS6, options)) && options.ShadingRateImageTileSize != 0) {
        tile = options.ShadingRateImageTileSize;
    }
    rr::CpuRaster raster(128, 128, rr::CpuRaster::pack(0.05f, 0.05f, 0.1f));
    const std::vector<Vertex> vertices = vrs_vertices(rows);
    const float color[3] = {0.9f, 0.6f, 0.2f};
    for (std::size_t t = 0; t + 2 < vertices.size(); t += 3) {
        rr::Vec4 clip[3];
        for (std::size_t k = 0; k < 3; ++k) {
            const Vertex& v = vertices[t + k];
            clip[k] = {v.pos[0], v.pos[1], v.pos[2], 1.0f};
        }
        raster.triangle(clip, rr::CpuRaster::Cull::None, true, [&](int x, int y, bool, float, const float*) {
            const VrsBlock block = vrs_block_at(rows, tile, static_cast<unsigned>(x), static_cast<unsigned>(y));
            const float nx = 0.5f * ((static_cast<float>(x) + 0.5f) / 64.0f - 1.0f);
            const float ny = 0.5f * (1.0f - (static_cast<float>(y) + 0.5f) / 64.0f);
            const float length = std::sqrt(nx * nx + ny * ny + 1.0f);
            const float light = std::min(1.0f, std::max(0.0f, (nx * 0.5f + ny * 0.8f + 0.6f) / length / std::sqrt(0.5f * 0.5f + 0.8f * 0.8f + 0.6f * 0.6f))) * 0.6f + 0.4f;
            const float h = static_cast<float>(vrs_noise(static_cast<std::uint32_t>(x) / block.x, static_cast<std::uint32_t>(y) / block.y)) / 255.0f;
            return rr::CpuRaster::pack(color[0] * light + (h - color[0] * light) * 0.5f, color[1] * light + (h - color[1] * light) * 0.5f, color[2] * light + (h - color[2] * light) * 0.5f);
        });
    }
    out.add("", raster.take());
    return true;
}

void register_vrs() {
    const rr::Tolerance tolerance = rr::Tolerance::blend(6).with_outliers(0.01);
    Cases::single(
        rr::CaseSpec("rs", "vrs_tier2_shading_rate_image_tiles").variant(5).tolerance(tolerance).tag("raster").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
            if (FAILED(D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS6, options)) || options.ShadingRateImageTileSize == 0) {
                return fail_with(error, "the shading rate image tile size is not reported");
            }
            const UINT tile = options.ShadingRateImageTileSize;
            const UINT tiles = (128 + tile - 1) / tile;
            TextureSpec spec;
            spec.format = DXGI_FORMAT_R8_UINT;
            spec.width = tiles;
            spec.height = tiles;
            ID3D12Resource* image = TextureFactory::create(kit, spec, false, error);
            if (image == nullptr) {
                return false;
            }
            std::vector<std::uint8_t> rates(static_cast<std::size_t>(tiles) * tiles);
            for (UINT y = 0; y < tiles; ++y) {
                for (UINT x = 0; x < tiles; ++x) {
                    const VrsBlock block = vrs_tile_rate(x, y, tiles);
                    rates[static_cast<std::size_t>(y) * tiles + x] = static_cast<std::uint8_t>(((block.x == 2 ? 1u : 0u) << 2) | (block.y == 2 ? 1u : 0u));
                }
            }
            if (!D3d12Tools::write_texture(kit.rig, image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, rates.data(), error)) {
                return false;
            }
            const bool ignore = kit.broken("vrs");
            return draw_vrs_scene(kit, VrsRows::ImageTiles, [&](ID3D12GraphicsCommandList5* list5, Stage&) {
                ID3D12GraphicsCommandList* base = nullptr;
                list5->QueryInterface(__uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&base));
                D3d12Tools::move(base, image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
                base->Release();
                const D3D12_SHADING_RATE_COMBINER combiners[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, ignore ? D3D12_SHADING_RATE_COMBINER_PASSTHROUGH : D3D12_SHADING_RATE_COMBINER_OVERRIDE};
                list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, combiners);
                list5->RSSetShadingRateImage(image);
            }, out, error);
        },
        [](Kit& kit) { return needs_options6(kit, D3D12_VARIABLE_SHADING_RATE_TIER_2); },
        [](Kit& device, unsigned, rr::CaptureSet& out, std::string& error) { return vrs_reference(device, VrsRows::ImageTiles, out, error); });
    Cases::single(
        rr::CaseSpec("rs", "vrs_tier2_combiner_min_image_and_draw_rate").variant(5).tolerance(tolerance).tag("raster").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS6 options{};
            if (FAILED(D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS6, options)) || options.ShadingRateImageTileSize == 0) {
                return fail_with(error, "the shading rate image tile size is not reported");
            }
            const UINT tile = options.ShadingRateImageTileSize;
            const UINT tiles = (128 + tile - 1) / tile;
            TextureSpec spec;
            spec.format = DXGI_FORMAT_R8_UINT;
            spec.width = tiles;
            spec.height = tiles;
            ID3D12Resource* image = TextureFactory::create(kit, spec, false, error);
            if (image == nullptr) {
                return false;
            }
            std::vector<std::uint8_t> rates(static_cast<std::size_t>(tiles) * tiles);
            for (UINT y = 0; y < tiles; ++y) {
                for (UINT x = 0; x < tiles; ++x) {
                    rates[static_cast<std::size_t>(y) * tiles + x] = x < tiles / 2 ? D3D12_SHADING_RATE_2X2 : D3D12_SHADING_RATE_1X1;
                }
            }
            if (!D3d12Tools::write_texture(kit.rig, image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, 0, rates.data(), error)) {
                return false;
            }
            const bool ignore = kit.broken("vrs");
            return draw_vrs_scene(kit, VrsRows::CombinerMax, [&](ID3D12GraphicsCommandList5* list5, Stage&) {
                ID3D12GraphicsCommandList* base = nullptr;
                list5->QueryInterface(__uuidof(ID3D12GraphicsCommandList), reinterpret_cast<void**>(&base));
                D3d12Tools::move(base, image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_SHADING_RATE_SOURCE);
                base->Release();
                const D3D12_SHADING_RATE_COMBINER combiners[2] = {D3D12_SHADING_RATE_COMBINER_PASSTHROUGH, ignore ? D3D12_SHADING_RATE_COMBINER_PASSTHROUGH : D3D12_SHADING_RATE_COMBINER_MAX};
                list5->RSSetShadingRate(D3D12_SHADING_RATE_2X1, combiners);
                list5->RSSetShadingRateImage(image);
            }, out, error);
        },
        [](Kit& kit) { return needs_options6(kit, D3D12_VARIABLE_SHADING_RATE_TIER_2); },
        [](Kit& device, unsigned, rr::CaptureSet& out, std::string& error) { return vrs_reference(device, VrsRows::CombinerMax, out, error); });
    Cases::single(
        rr::CaseSpec("rs", "vrs_tier2_per_primitive_rate_from_vertex_shader").variant(5).tolerance(tolerance).tag("raster").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            const bool ignore = kit.broken("vrs");
            return draw_vrs_scene(kit, VrsRows::PerPrimitive, [&](ID3D12GraphicsCommandList5* list5, Stage&) {
                const D3D12_SHADING_RATE_COMBINER combiners[2] = {ignore ? D3D12_SHADING_RATE_COMBINER_PASSTHROUGH : D3D12_SHADING_RATE_COMBINER_OVERRIDE, D3D12_SHADING_RATE_COMBINER_PASSTHROUGH};
                list5->RSSetShadingRate(D3D12_SHADING_RATE_1X1, combiners);
            }, out, error);
        },
        [](Kit& kit) { return needs_options6(kit, D3D12_VARIABLE_SHADING_RATE_TIER_2); },
        [](Kit& device, unsigned, rr::CaptureSet& out, std::string& error) { return vrs_reference(device, VrsRows::PerPrimitive, out, error); });
}

void register_sampler_feedback() {
    Cases::single(
        rr::CaseSpec("tex", "sampler_feedback_min_mip_decode_regions").variant(6).tolerance(rr::Tolerance::exact()).tag("texture").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            Stage stage;
            if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN)) {
                return false;
            }
            Com<ID3D12Device8> device8 = kit.rig.as<ID3D12Device8>();
            if (!device8) {
                return fail_with(error, "ID3D12Device8 is not available");
            }
            TextureSpec spec;
            spec.width = 64;
            spec.height = 64;
            spec.mips = 7;
            spec.content = 3;
            ID3D12Resource* paired = TextureFactory::create(kit, spec, true, error);
            if (paired == nullptr) {
                return false;
            }
            D3D12_RESOURCE_DESC1 feedback_desc{};
            feedback_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            feedback_desc.Width = 64;
            feedback_desc.Height = 64;
            feedback_desc.DepthOrArraySize = 1;
            feedback_desc.MipLevels = 7;
            feedback_desc.Format = kFeedbackMinMip;
            feedback_desc.SampleDesc.Count = 1;
            feedback_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            feedback_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            feedback_desc.SamplerFeedbackMipRegion = {8, 8, 1};
            const D3D12_HEAP_PROPERTIES heap = D3d12Tools::heap_props(D3D12_HEAP_TYPE_DEFAULT);
            Com<ID3D12Resource> feedback_com;
            HRESULT result = device8->CreateCommittedResource2(&heap, D3D12_HEAP_FLAG_NONE, &feedback_desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, nullptr,
                                                               __uuidof(ID3D12Resource), feedback_com.put_void());
            if (FAILED(result)) {
                return fail_with(error, "creating the sampler feedback texture: " + D3d12Tools::hr(result));
            }
            ID3D12Resource* feedback = kit.rig.hold(std::move(feedback_com));
            if (FAILED(kit.device()->GetDeviceRemovedReason())) {
                return fail_with(error, "creating the sampler feedback texture removed the device: " + D3d12Tools::hr(kit.device()->GetDeviceRemovedReason()));
            }
            Surface decoded;
            if (!decoded.color(kit, 8, 8, DXGI_FORMAT_R8_UINT, error)) {
                return false;
            }
            Binder& binder = stage.canvas.binder;
            binder.srv_texture(0, paired, DXGI_FORMAT_R8G8B8A8_UNORM, 7);
            binder.sampler(0, Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
            device8->CreateSamplerFeedbackUnorderedAccessView(paired, feedback, D3d12Tools::cpu_at(binder.heap, Binder::kSrvSlots, binder.increment));
            if (FAILED(kit.device()->GetDeviceRemovedReason())) {
                return fail_with(error, "creating the sampler feedback UAV removed the device: " + D3d12Tools::hr(kit.device()->GetDeviceRemovedReason()));
            }
            const char* ps =
                "Texture2D<float4> tex : register(t0); SamplerState smp : register(s0); FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> fb : register(u0);\n"
                "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
                "  float lod = floor(uv.x * 4.0) + (uv.y < 0.5 ? 0.0 : 2.0) + p3.x; fb.WriteSamplerFeedbackLevel(tex, smp, uv, lod); return tex.SampleLevel(smp, uv, lod); }\n";
            Pso builder(kit, binder.signature);
            builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(ps)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            const float brk[4] = {kit.broken("sampler_feedback") ? 1.0f : 0.0f, 0, 0, 0};
            bool have_list1 = true;
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    ID3D12GraphicsCommandList1* list1 = nullptr;
                    if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void**>(&list1)))) {
                        have_list1 = false;
                        return;
                    }
                    const float clear[4] = {0, 0, 0, 1};
                    stage.canvas.begin(list, clear);
                    list->SetPipelineState(state);
                    list->SetGraphicsRoot32BitConstants(0, 4, brk, 28);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(3, 1, 0, 0);
                    D3d12Tools::move(list, feedback, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                    decoded.to(list, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                    list1->ResolveSubresourceRegion(decoded.resource, 0, 0, 0, feedback, 0, nullptr, DXGI_FORMAT_R8_UINT, D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK);
                    list1->Release();
                },
                error);
            if (!have_list1) {
                return fail_with(error, "ID3D12GraphicsCommandList1 is not available");
            }
            rr::Image regions;
            if (!ok || !kit.read_image(decoded.resource, decoded.state, 0, DXGI_FORMAT_R8_UINT, regions, error)) {
                return false;
            }
            out.add("min_mip", std::move(regions));
            return stage.finish(out, error, "color");
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS7, options);
            return SUCCEEDED(result) && options.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED ? std::string() : std::string("sampler feedback");
        });
}

void register_work_graphs() {
    Cases::single(
        rr::CaseSpec("cs", "work_graph_broadcasting_node_fills_texture").tolerance(rr::Tolerance::exact()).tag("compute"),
        [](Kit&, rr::CaptureSet&, std::string& error) { return fail_with(error, "work graph state objects are not available in this build"); },
        [](Kit& kit) {
            std::uint32_t data[4] = {0, 0, 0, 0};
            const HRESULT result = kit.device()->CheckFeatureSupport(static_cast<D3D12_FEATURE>(50), data, sizeof data);
            return SUCCEEDED(result) && data[0] != 0 ? std::string() : std::string("work graphs (D3D12_FEATURE_D3D12_OPTIONS21 is not reported by the runtime)");
        });
}

struct ResolveCase {
    const char* name;
    D3D12_RESOLVE_MODE mode;
};

void register_resolve_modes() {
    const ResolveCase cases[] = {{"min", D3D12_RESOLVE_MODE_MIN}, {"max", D3D12_RESOLVE_MODE_MAX}, {"average", D3D12_RESOLVE_MODE_AVERAGE}};
    for (const ResolveCase& c : cases) {
        Cases::single(
            rr::CaseSpec("msaa", std::string("resolve_subresource_region_mode_") + c.name + "_4x").tolerance(rr::Tolerance::blend(2)).tag("msaa"),
            [c](Kit& kit, rr::CaptureSet& out, std::string& error) {
                Stage stage;
                Surface resolved;
                if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT, 128, 4) || !resolved.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
                    return false;
                }
                Pso builder = stage.pso();
                ID3D12PipelineState* state = builder.build(error);
                if (state == nullptr) {
                    return false;
                }
                D3D12_RESOLVE_MODE mode = c.mode;
                if (kit.broken("resolve_mode")) {
                    mode = c.mode == D3D12_RESOLVE_MODE_MIN ? D3D12_RESOLVE_MODE_MAX : (c.mode == D3D12_RESOLVE_MODE_MAX ? D3D12_RESOLVE_MODE_MIN : D3D12_RESOLVE_MODE_MAX);
                }
                bool have_list1 = true;
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        ID3D12GraphicsCommandList1* list1 = nullptr;
                        if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void**>(&list1)))) {
                            have_list1 = false;
                            return;
                        }
                        const float clear[4] = {0.05f, 0.05f, 0.1f, 1};
                        stage.canvas.begin(list, clear);
                        stage.backdrop(list);
                        list->SetPipelineState(state);
                        stage.mesh(list, stage.torus, rr::Mat4::rotate_x(0.9f), 0.9f, 0.6f, 0.2f);
                        stage.mesh(list, stage.sphere, rr::Mat4::translate(0.3f, -0.2f, 0.2f) * rr::Mat4::scale(0.55f, 0.55f, 0.55f), 0.3f, 0.7f, 0.95f);
                        stage.canvas.color.to(list, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                        resolved.to(list, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                        list1->ResolveSubresourceRegion(resolved.resource, 0, 0, 0, stage.canvas.color.resource, 0, nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, mode);
                        list1->Release();
                    },
                    error);
                if (!have_list1) {
                    return fail_with(error, "ID3D12GraphicsCommandList1 is not available");
                }
                rr::Image image;
                if (!ok || !kit.read_image(resolved.resource, resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                    return false;
                }
                out.add("", std::move(image));
                return true;
            },
            [](Kit& kit) {
                const std::string reason = needs_programmable_positions(kit, "ResolveSubresourceRegion (programmable sample positions)");
                return reason.empty() ? needs_samples_4(kit) : reason;
            });
    }
}

void register_sample_positions() {
    for (int grid = 0; grid < 2; ++grid) {
        Cases::single(
            rr::CaseSpec("msaa", grid != 0 ? "programmable_sample_positions_2x2_pixel_grid_4x" : "programmable_sample_positions_single_pattern_4x").tolerance(rr::Tolerance::blend(2)).tag("msaa"),
            [grid](Kit& kit, rr::CaptureSet& out, std::string& error) {
                Stage stage;
                Surface resolved;
                if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN, 128, 4) || !resolved.color(kit, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, error)) {
                    return false;
                }
                std::vector<Vertex> bars;
                for (int n = 0; n < 15; ++n) {
                    const float x0 = (4.0f + 8.0f * static_cast<float>(n) + 0.25f * static_cast<float>(n % 4) + 0.2f) / 64.0f - 1.0f;
                    const float x1 = x0 + 2.6f / 64.0f;
                    const Vertex quad[6] = {{{x0, -0.9f, 0}, {0, 0, 1}, {0, 0}}, {{x1, -0.9f, 0}, {0, 0, 1}, {0, 0}}, {{x1, 0.9f, 0}, {0, 0, 1}, {0, 0}},
                                            {{x0, -0.9f, 0}, {0, 0, 1}, {0, 0}}, {{x1, 0.9f, 0}, {0, 0, 1}, {0, 0}}, {{x0, 0.9f, 0}, {0, 0, 1}, {0, 0}}};
                    bars.insert(bars.end(), quad, quad + 6);
                }
                Geometry geometry;
                if (!geometry.create_vertices(kit, bars.data(), static_cast<UINT>(bars.size()), sizeof(Vertex), error)) {
                    return false;
                }
                Pso builder(kit, stage.canvas.binder.signature);
                builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(Hlsl12::kSolidPs)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).samples(4);
                ID3D12PipelineState* state = builder.build(error);
                if (state == nullptr) {
                    return false;
                }
                std::vector<D3D12_SAMPLE_POSITION> positions;
                if (grid == 0) {
                    positions = {{7, -6}, {5, 6}, {6, -1}, {7, 3}};
                } else {
                    positions = {{7, -6}, {5, 6}, {6, -1}, {7, 3}, {-7, -6}, {-5, 6}, {-6, -1}, {-7, 3}, {-3, 7}, {3, 6}, {0, 7}, {-1, 5}, {3, -7}, {-3, -6}, {0, -7}, {1, -5}};
                }
                const bool custom = !kit.broken("sample_positions");
                bool have_list1 = true;
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        ID3D12GraphicsCommandList1* list1 = nullptr;
                        if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void**>(&list1)))) {
                            have_list1 = false;
                            return;
                        }
                        const float clear[4] = {0.0f, 0.0f, 0.0f, 1};
                        stage.canvas.begin(list, clear);
                        if (custom) {
                            list1->SetSamplePositions(4, grid != 0 ? 4 : 1, positions.data());
                        }
                        list->SetPipelineState(state);
                        geometry.bind(list);
                        for (UINT n = 0; n < 15; ++n) {
                            const float color[4] = {1.0f, (n & 1) != 0 ? 0.4f : 1.0f, 1.0f, 1.0f};
                            set_cbv_root(list, rr::Mat4::identity(), color);
                            list->DrawInstanced(6, 1, n * 6, 0);
                        }
                        if (custom) {
                            list1->SetSamplePositions(0, 0, nullptr);
                        }
                        stage.canvas.color.to(list, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
                        resolved.to(list, D3D12_RESOURCE_STATE_RESOLVE_DEST);
                        list->ResolveSubresource(resolved.resource, 0, stage.canvas.color.resource, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
                        list1->Release();
                    },
                    error);
                if (!have_list1) {
                    return fail_with(error, "ID3D12GraphicsCommandList1 is not available");
                }
                rr::Image image;
                if (!ok || !kit.read_image(resolved.resource, resolved.state, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                    return false;
                }
                out.add("", std::move(image));
                return true;
            },
            [grid](Kit& kit) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
                const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS2, options);
                const D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER need = grid != 0 ? D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_2 : D3D12_PROGRAMMABLE_SAMPLE_POSITIONS_TIER_1;
                if (FAILED(result) || options.ProgrammableSamplePositionsTier < need) {
                    return std::string(grid != 0 ? "programmable sample positions tier 2" : "programmable sample positions");
                }
                return needs_samples_4(kit);
            });
    }
}

std::string needs_forced_count(Kit& kit, UINT count) {
    Stage stage;
    std::string error;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN)) {
        return {};
    }
    const char* ps = "void main(float4 pos : SV_Position) {}\n";
    auto builds = [&](UINT forced) {
        Pso builder(kit, stage.canvas.binder.signature);
        builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(ps)).targets({});
        builder.desc.NumRenderTargets = 0;
        builder.desc.RasterizerState.ForcedSampleCount = forced;
        std::string ignored;
        return builder.build(ignored) != nullptr;
    };
    if (!builds(1)) {
        return {};
    }
    return builds(count) ? std::string() : "forced sample count " + std::to_string(count) + " without render targets";
}

void register_forced_sample_count() {
    for (UINT count : {4u, 8u, 16u}) {
        Cases::single(
            rr::CaseSpec("rs", "forced_sample_count_" + std::to_string(count) + "_uav_coverage_popcount").tolerance(rr::Tolerance::coverage_edges()).tag("raster"),
            [count](Kit& kit, rr::CaptureSet& out, std::string& error) {
                Stage stage;
                if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN)) {
                    return false;
                }
                TextureSpec spec;
                spec.format = DXGI_FORMAT_R32_UINT;
                spec.width = 128;
                spec.height = 128;
                spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                ID3D12Resource* coverage = TextureFactory::create(kit, spec, false, error);
                if (coverage == nullptr) {
                    return false;
                }
                stage.canvas.binder.uav_texture(0, coverage, DXGI_FORMAT_R32_UINT);
                const char* ps =
                    "RWTexture2D<uint> cov : register(u0);\n"
                    "void main(float4 pos : SV_Position, uint c : SV_Coverage) { cov[uint2(pos.xy)] = countbits(c); }\n";
                Pso builder(kit, stage.canvas.binder.signature);
                builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root(ps)).targets({});
                builder.desc.NumRenderTargets = 0;
                builder.desc.RasterizerState.ForcedSampleCount = kit.broken("forced_sample_count") ? 1 : count;
                ID3D12PipelineState* state = builder.build(error);
                if (state == nullptr) {
                    return false;
                }
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, coverage, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        stage.canvas.binder.bind_graphics(list);
                        list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
                        stage.canvas.set_viewport(list);
                        list->SetPipelineState(state);
                        const float color[4] = {1, 1, 1, 1};
                        set_cbv_root(list, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -2.6f) * rr::Mat4::rotate_x(0.9f), color);
                        stage.torus.bind(list);
                        stage.torus.draw(list);
                        set_cbv_root(list, rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0.3f, -0.2f, -3.0f) * rr::Mat4::scale(0.6f, 0.6f, 0.6f), color);
                        stage.sphere.bind(list);
                        stage.sphere.draw(list);
                    },
                    error);
                rr::Image image;
                if (!ok || !read_raw_image(kit, coverage, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1, image, error)) {
                    return false;
                }
                out.add("", std::move(image));
                return true;
            },
            [count](Kit& kit) { return needs_forced_count(kit, count); });
    }
}

struct SoRig {
    Stage stage;
    std::vector<Vertex> expanded;
    Geometry source;
    ID3D12PipelineState* state = nullptr;
    ID3D12Resource* buffer = nullptr;
    ID3D12Resource* filled = nullptr;
    UINT64 bytes = 0;

    [[nodiscard]] bool open(Kit& kit, bool front_only, UINT64 buffer_bytes, std::string& error) {
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        const MeshData mesh = Shapes::torus(18, 10, 0.85f, 0.3f);
        for (std::uint32_t index : mesh.indices) {
            expanded.push_back(mesh.vertices[index]);
        }
        if (!source.create_vertices(kit, expanded.data(), static_cast<UINT>(expanded.size()), sizeof(Vertex), error)) {
            return false;
        }
        const char* vs =
            "struct VsIn { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "VsOut main(VsIn i) { VsOut o; o.pos = mul(mvp, float4(i.pos, 1)); o.nrm = i.nrm; o.uv = i.uv; return o; }\n";
        const char* gs =
            "struct In { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
            "[maxvertexcount(3)] void main(triangle In v[3], inout TriangleStream<In> s) {\n"
            "  float2 a = v[1].pos.xy / v[1].pos.w - v[0].pos.xy / v[0].pos.w; float2 b = v[2].pos.xy / v[2].pos.w - v[0].pos.xy / v[0].pos.w;\n"
            "  if (a.x * b.y - a.y * b.x > 0) { for (int k = 0; k < 3; ++k) s.Append(v[k]); } }\n";
        static const D3D12_SO_DECLARATION_ENTRY entries[3] = {{0, "SV_Position", 0, 0, 4, 0}, {0, "NORMAL", 0, 0, 3, 0}, {0, "TEXCOORD", 0, 0, 2, 0}};
        static const UINT strides[1] = {36};
        Pso builder(kit, stage.canvas.binder.signature);
        builder.vs(Hlsl12::with_root(vs));
        if (front_only) {
            builder.stage(gs, "main", "gs");
        }
        builder.targets({});
        builder.desc.StreamOutput.pSODeclaration = entries;
        builder.desc.StreamOutput.NumEntries = 3;
        builder.desc.StreamOutput.pBufferStrides = strides;
        builder.desc.StreamOutput.NumStrides = 1;
        builder.desc.StreamOutput.RasterizedStream = D3D12_SO_NO_RASTERIZED_STREAM;
        builder.desc.NumRenderTargets = 0;
        builder.desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        builder.desc.DepthStencilState.DepthEnable = FALSE;
        builder.desc.PS = {};
        state = builder.build(error);
        if (state == nullptr) {
            return false;
        }
        bytes = buffer_bytes != 0 ? buffer_bytes : static_cast<UINT64>(expanded.size()) * 36;
        HRESULT result = S_OK;
        buffer = D3d12Tools::device_buffer(kit.rig, static_cast<UINT64>(expanded.size()) * 36, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_FLAG_NONE, &result);
        filled = D3d12Tools::device_buffer(kit.rig, 4096, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_FLAG_NONE, &result);
        if (buffer == nullptr || filled == nullptr) {
            return fail_with(error, "stream output buffers: " + D3d12Tools::hr(result));
        }
        const std::vector<std::uint32_t> zero_words(1024, 0);
        return D3d12Tools::write_buffer(kit.rig, filled, D3D12_RESOURCE_STATE_STREAM_OUT, 0, zero_words.data(), 4096, error);
    }

    void record(ID3D12GraphicsCommandList* list, const rr::Mat4& matrix) {
        stage.canvas.binder.bind_graphics(list);
        list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
        stage.canvas.set_viewport(list);
        list->SetPipelineState(state);
        const float color[4] = {0.9f, 0.6f, 0.2f, 1.0f};
        set_cbv_root(list, matrix, color);
        const D3D12_STREAM_OUTPUT_BUFFER_VIEW view{buffer->GetGPUVirtualAddress(), bytes, filled->GetGPUVirtualAddress()};
        list->SOSetTargets(0, 1, &view);
        source.bind(list);
        source.draw_vertices(list);
        list->SOSetTargets(0, 1, nullptr);
    }
};

void register_stream_output_statistics() {
    for (int overflow = 0; overflow < 2; ++overflow) {
        Cases::single(
            rr::CaseSpec("query", overflow != 0 ? "so_statistics_buffer_overflow_counts_needed_primitives" : "so_statistics_primitives_written_equal_needed").tolerance(rr::Tolerance::exact()).tag("query").tag("streamout"),
            [overflow](Kit& kit, rr::CaptureSet& out, std::string& error) {
                SoRig rig;
                UINT64 capacity = overflow != 0 ? 100ull * 3 * 36 : 0;
                if (kit.broken("so_statistics")) {
                    capacity = 50ull * 3 * 36;
                }
                if (!rig.open(kit, false, capacity, error)) {
                    return false;
                }
                const D3D12_QUERY_HEAP_DESC heap_desc{D3D12_QUERY_HEAP_TYPE_SO_STATISTICS, 1, 0};
                Com<ID3D12QueryHeap> heap_com;
                const HRESULT created = kit.device()->CreateQueryHeap(&heap_desc, __uuidof(ID3D12QueryHeap), heap_com.put_void());
                if (FAILED(created)) {
                    return fail_with(error, "CreateQueryHeap: " + D3d12Tools::hr(created));
                }
                ID3D12QueryHeap* heap = kit.rig.hold(std::move(heap_com));
                HRESULT result = S_OK;
                ID3D12Resource* results = D3d12Tools::device_buffer(kit.rig, 256, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_FLAG_NONE, &result);
                if (results == nullptr) {
                    return fail_with(error, "query result buffer: " + D3d12Tools::hr(result));
                }
                const rr::Mat4 matrix = rig.stage.view_projection() * rr::Mat4::rotate_x(0.9f);
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        list->BeginQuery(heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);
                        rig.record(list, matrix);
                        list->EndQuery(heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0);
                        list->ResolveQueryData(heap, D3D12_QUERY_TYPE_SO_STATISTICS_STREAM0, 0, 1, results, 0);
                    },
                    error);
                std::vector<std::uint32_t> words;
                if (!ok || !D3d12Tools::read_words(kit.rig, results, D3D12_RESOURCE_STATE_COPY_DEST, 4, words, error)) {
                    return false;
                }
                const std::uint32_t values[12] = {words[0], words[1], words[2], words[3], words[2], words[3], words[0], words[1], 1080u, 36u, 3u, 7u};
                rr::Image image(3, 1, rr::PixelKind::Rgba32Uint);
                std::memcpy(image.bytes().data(), values, 48);
                out.add("", std::move(image));
                return true;
            });
    }
    Cases::single(
        rr::CaseSpec("ia", "stream_output_filled_size_drives_indirect_indexed_draw").tolerance(rr::Tolerance::blend(2)).tag("streamout").tag("indirect"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            SoRig rig;
            if (!rig.open(kit, true, 0, error)) {
                return false;
            }
            Cs sizer;
            if (!sizer.open(kit,
                            "ByteAddressBuffer filled : register(t0); RWByteAddressBuffer args : register(u0);\n"
                            "[numthreads(1, 1, 1)] void main() { uint n = filled.Load(0) / 36; if (p3.x > 0.5) n /= 2; args.Store(0, n); args.Store(4, 1); args.Store(8, 0); args.Store(12, 0); args.Store(16, 0); }\n",
                            error)) {
                return false;
            }
            HRESULT result = S_OK;
            ID3D12Resource* args = D3d12Tools::device_buffer(kit.rig, 256, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
            if (args == nullptr) {
                return fail_with(error, "argument buffer: " + D3d12Tools::hr(result));
            }
            sizer.binder.srv_buffer(0, rig.filled, DXGI_FORMAT_UNKNOWN, 0, 1024, 0, true);
            sizer.binder.uav_buffer(0, args, DXGI_FORMAT_UNKNOWN, 0, 64, 0, true);
            std::vector<std::uint32_t> sequence(rig.expanded.size());
            for (std::size_t i = 0; i < sequence.size(); ++i) {
                sequence[i] = static_cast<std::uint32_t>(i);
            }
            ID3D12Resource* index_buffer = kit.make_upload(sequence.data(), sequence.size() * 4, error);
            if (index_buffer == nullptr) {
                return false;
            }
            D3D12_INDIRECT_ARGUMENT_DESC argument{};
            argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
            const D3D12_COMMAND_SIGNATURE_DESC signature_desc{sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), 1, &argument, 0};
            Com<ID3D12CommandSignature> signature_com;
            const HRESULT created = kit.device()->CreateCommandSignature(&signature_desc, nullptr, __uuidof(ID3D12CommandSignature), signature_com.put_void());
            if (FAILED(created)) {
                return fail_with(error, "CreateCommandSignature: " + D3d12Tools::hr(created));
            }
            ID3D12CommandSignature* signature = kit.rig.hold(std::move(signature_com));
            const char* draw_vs =
                "struct VsIn { float4 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "struct VsOut { float4 pos : SV_Position; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };\n"
                "VsOut main(VsIn i) { VsOut o; o.pos = i.pos; o.nrm = i.nrm; o.uv = i.uv; return o; }\n";
            std::vector<D3D12_INPUT_ELEMENT_DESC> layout = Shapes::layout();
            layout[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
            layout[1].AlignedByteOffset = 16;
            layout[2].AlignedByteOffset = 28;
            Pso draw(kit, rig.stage.canvas.binder.signature);
            draw.vs(Hlsl12::with_root(draw_vs)).ps(Hlsl12::with_root(Hlsl12::kLitPs)).layout(layout).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D32_FLOAT);
            ID3D12PipelineState* draw_state = draw.build(error);
            if (draw_state == nullptr) {
                return false;
            }
            const float brk[4] = {kit.broken("so_draw_size") ? 1.0f : 0.0f, 0, 0, 0};
            const rr::Mat4 matrix = rig.stage.view_projection() * rr::Mat4::rotate_x(0.9f);
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    Canvas& canvas = rig.stage.canvas;
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                    canvas.begin(list, clear);
                    list->OMSetRenderTargets(0, nullptr, FALSE, nullptr);
                    rig.record(list, matrix);
                    D3d12Tools::move(list, rig.filled, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    sizer.bind(list);
                    list->SetComputeRoot32BitConstants(0, 4, brk, 28);
                    list->Dispatch(1, 1, 1);
                    D3d12Tools::move(list, args, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
                    D3d12Tools::move(list, rig.buffer, D3D12_RESOURCE_STATE_STREAM_OUT, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
                    canvas.color.bind_target(list, &canvas.depth);
                    canvas.binder.bind_graphics(list);
                    list->SetPipelineState(draw_state);
                    const D3D12_VERTEX_BUFFER_VIEW vbv{rig.buffer->GetGPUVirtualAddress(), static_cast<UINT>(rig.expanded.size() * 36), 36};
                    const D3D12_INDEX_BUFFER_VIEW ibv{index_buffer->GetGPUVirtualAddress(), static_cast<UINT>(sequence.size() * 4), DXGI_FORMAT_R32_UINT};
                    list->IASetVertexBuffers(0, 1, &vbv);
                    list->IASetIndexBuffer(&ibv);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->ExecuteIndirect(signature, 1, args, 0, nullptr, 0);
                },
                error);
            return ok && rig.stage.finish(out, error);
        });
}

void register_atomics64() {
    struct Op {
        const char* name;
        const char* body;
    };
    const Op ops[] = {{"interlocked_max_hash", "InterlockedMax(img[id.xy & 15], v);"}, {"interlocked_add_wrapping_sum", "InterlockedAdd(img[id.xy & 15], v);"},
                      {"interlocked_xor_fold", "InterlockedXor(img[id.xy & 15], v);"}};
    for (const Op& op : ops) {
        Cases::single(
            rr::CaseSpec("tex", std::string("atomic64_texture_") + op.name).variant(7).tolerance(rr::Tolerance::exact()).tag("texture").tag("x64-only"),
            [op](Kit& kit, rr::CaptureSet& out, std::string& error) {
                const std::string body =
                    std::string("RWTexture2D<uint64_t> img : register(u0);\n"
                                "uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }\n"
                                "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
                                "  uint h = hash(id.x * 9781u + id.y * 6271u + (uint)p3.x); uint h2 = hash(h + 17u); uint64_t v = ((uint64_t)h2 << 32) | h;\n  ") +
                    op.body + "\n}\n";
                Cs cs;
                if (!cs.open(kit, body, error)) {
                    return false;
                }
                TextureSpec spec;
                spec.format = DXGI_FORMAT_R32G32_UINT;
                spec.width = 16;
                spec.height = 16;
                spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                ID3D12Resource* image = TextureFactory::create(kit, spec, false, error);
                if (image == nullptr) {
                    return false;
                }
                cs.binder.uav_texture(0, image, DXGI_FORMAT_R32G32_UINT);
                const float brk[4] = {kit.broken("atomic64") ? 5.0f : 0.0f, 0, 0, 0};
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, image, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        cs.bind(list);
                        list->SetComputeRoot32BitConstants(0, 4, brk, 28);
                        list->Dispatch(16, 16, 1);
                    },
                    error);
                rr::Image result;
                if (!ok || !read_raw_image(kit, image, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 2, result, error)) {
                    return false;
                }
                out.add("", std::move(result));
                return true;
            },
            [](Kit& kit) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS9 options{};
                const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS9, options);
                return SUCCEEDED(result) && options.AtomicInt64OnTypedResourceSupported ? std::string() : std::string("64-bit atomics on typed resources");
            });
    }
}

void register_gpu_upload_heap() {
    Cases::frames(
        rr::CaseSpec("heap", "gpu_upload_heap_vertex_buffer_rewritten_per_frame").frames(4, {0, 3}).tolerance(rr::Tolerance::blend(2)).tag("heap"),
        [](Cases::Steps& steps) {
            struct State {
                Stage stage;
                ID3D12Resource* vertices = nullptr;
                Vertex* mapped = nullptr;
                std::vector<Vertex> expanded;
                ID3D12PipelineState* state = nullptr;
            };
            auto s = std::make_shared<State>();
            steps.on_setup = [s](Kit& kit, std::string& error) {
                if (!s->stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                    return false;
                }
                const MeshData mesh = Shapes::torus(24, 12, 0.85f, 0.3f);
                for (std::uint32_t index : mesh.indices) {
                    s->expanded.push_back(mesh.vertices[index]);
                }
                D3D12_HEAP_PROPERTIES props{};
                props.Type = static_cast<D3D12_HEAP_TYPE>(5);
                const D3D12_RESOURCE_DESC desc = D3d12Tools::buffer_desc(s->expanded.size() * sizeof(Vertex));
                Com<ID3D12Resource> buffer;
                const HRESULT result = kit.device()->CreateCommittedResource(&props, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), buffer.put_void());
                if (FAILED(result)) {
                    return fail_with(error, "creating the GPU upload heap buffer: " + D3d12Tools::hr(result));
                }
                s->vertices = kit.rig.hold(std::move(buffer));
                const D3D12_RANGE none{0, 0};
                if (FAILED(s->vertices->Map(0, &none, reinterpret_cast<void**>(&s->mapped)))) {
                    return fail_with(error, "mapping the GPU upload heap buffer");
                }
                Pso builder = s->stage.pso();
                s->state = builder.build(error);
                return s->state != nullptr;
            };
            steps.on_frame = [s](Kit& kit, unsigned frame, std::string& error) {
                if (frame == 0 || !kit.broken("gpu_upload")) {
                    const rr::Mat4 turn = rr::Mat4::rotate_y(0.5f * static_cast<float>(frame)) * rr::Mat4::rotate_x(0.9f);
                    for (std::size_t i = 0; i < s->expanded.size(); ++i) {
                        Vertex v = s->expanded[i];
                        const float* m = turn.m;
                        v.pos[0] = m[0] * s->expanded[i].pos[0] + m[4] * s->expanded[i].pos[1] + m[8] * s->expanded[i].pos[2];
                        v.pos[1] = m[1] * s->expanded[i].pos[0] + m[5] * s->expanded[i].pos[1] + m[9] * s->expanded[i].pos[2];
                        v.pos[2] = m[2] * s->expanded[i].pos[0] + m[6] * s->expanded[i].pos[1] + m[10] * s->expanded[i].pos[2];
                        v.normal[0] = m[0] * s->expanded[i].normal[0] + m[4] * s->expanded[i].normal[1] + m[8] * s->expanded[i].normal[2];
                        v.normal[1] = m[1] * s->expanded[i].normal[0] + m[5] * s->expanded[i].normal[1] + m[9] * s->expanded[i].normal[2];
                        v.normal[2] = m[2] * s->expanded[i].normal[0] + m[6] * s->expanded[i].normal[1] + m[10] * s->expanded[i].normal[2];
                        s->mapped[i] = v;
                    }
                }
                return kit.submit([&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                    s->stage.canvas.begin(list, clear);
                    list->SetPipelineState(s->state);
                    const float color[4] = {0.9f, 0.6f, 0.2f, 1};
                    set_cbv_root(list, s->stage.view_projection(), color);
                    const D3D12_VERTEX_BUFFER_VIEW vbv{s->vertices->GetGPUVirtualAddress(), static_cast<UINT>(s->expanded.size() * sizeof(Vertex)), sizeof(Vertex)};
                    list->IASetVertexBuffers(0, 1, &vbv);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->DrawInstanced(static_cast<UINT>(s->expanded.size()), 1, 0, 0);
                }, error);
            };
            steps.on_capture = [s](Kit&, unsigned, rr::CaptureSet& out, std::string& error) { return s->stage.finish(out, error); };
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS16 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS16, options);
            return SUCCEEDED(result) && options.GPUUploadHeapSupported ? std::string() : std::string("GPU upload heaps");
        });
}

void register_indirect_views() {
    Cases::single(
        rr::CaseSpec("ia", "execute_indirect_vertex_index_views_and_root_constants").tolerance(rr::Tolerance::blend(2)).tag("indirect"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            Stage stage;
            if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
                return false;
            }
            Pso builder = stage.pso();
            ID3D12PipelineState* state = builder.build(error);
            if (state == nullptr) {
                return false;
            }
            struct Command {
                D3D12_VERTEX_BUFFER_VIEW vertex;
                D3D12_INDEX_BUFFER_VIEW index;
                float mvp[16];
                float color[4];
                D3D12_DRAW_INDEXED_ARGUMENTS draw;
            };
            D3D12_INDIRECT_ARGUMENT_DESC arguments[5] = {};
            arguments[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
            arguments[0].VertexBuffer.Slot = 0;
            arguments[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
            arguments[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            arguments[2].Constant = {0, 0, 16};
            arguments[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
            arguments[3].Constant = {0, 16, 4};
            arguments[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
            const D3D12_COMMAND_SIGNATURE_DESC signature_desc{sizeof(Command), 5, arguments, 0};
            Com<ID3D12CommandSignature> signature_com;
            const HRESULT created = kit.device()->CreateCommandSignature(&signature_desc, stage.canvas.binder.signature, __uuidof(ID3D12CommandSignature), signature_com.put_void());
            if (FAILED(created)) {
                return fail_with(error, "CreateCommandSignature: " + D3d12Tools::hr(created));
            }
            ID3D12CommandSignature* signature = kit.rig.hold(std::move(signature_com));
            struct Item {
                const Geometry* geometry;
                rr::Mat4 model;
                float color[4];
            };
            const Item items[4] = {{&stage.sphere, rr::Mat4::translate(-0.7f, 0.5f, 0), {0.9f, 0.4f, 0.3f, 1}},
                                   {&stage.cube, rr::Mat4::translate(0.7f, 0.5f, 0) * rr::Mat4::rotate_y(0.7f) * rr::Mat4::rotate_x(0.5f), {0.3f, 0.8f, 0.4f, 1}},
                                   {&stage.torus, rr::Mat4::translate(-0.5f, -0.6f, 0) * rr::Mat4::rotate_x(1.0f) * rr::Mat4::scale(0.8f, 0.8f, 0.8f), {0.4f, 0.5f, 0.95f, 1}},
                                   {&stage.sphere, rr::Mat4::translate(0.8f, -0.6f, 0.2f) * rr::Mat4::scale(0.7f, 0.7f, 0.7f), {0.95f, 0.85f, 0.3f, 1}}};
            Command commands[4];
            for (int k = 0; k < 4; ++k) {
                Command& c = commands[k];
                c = {};
                c.vertex = items[k].geometry->vbv;
                c.index = items[k].geometry->ibv;
                if (k == 1 && kit.broken("indirect_views")) {
                    c.index.Format = DXGI_FORMAT_R32_UINT;
                }
                std::memcpy(c.mvp, (stage.view_projection() * items[k].model).m, 64);
                std::memcpy(c.color, items[k].color, 16);
                c.draw.IndexCountPerInstance = items[k].geometry->index_count;
                c.draw.InstanceCount = 1;
            }
            ID3D12Resource* args = kit.make_upload(commands, sizeof commands, error);
            if (args == nullptr) {
                return false;
            }
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                    stage.canvas.begin(list, clear);
                    list->SetPipelineState(state);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    list->ExecuteIndirect(signature, 4, args, 0, nullptr, 0);
                },
                error);
            return ok && stage.finish(out, error);
        });
}

struct Rasterizer2 {
    D3D12_FILL_MODE fill;
    D3D12_CULL_MODE cull;
    BOOL front_counter_clockwise;
    FLOAT depth_bias;
    FLOAT depth_bias_clamp;
    FLOAT slope_scaled_depth_bias;
    BOOL depth_clip;
    UINT line_mode;
    UINT forced_sample_count;
    D3D12_CONSERVATIVE_RASTERIZATION_MODE conservative;
};

constexpr D3D12_PIPELINE_STATE_SUBOBJECT_TYPE kRasterizer2 = static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(28);

struct LineStream {
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> layout;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC> blend;
    Sub<kRasterizer2, Rasterizer2> rasterizer;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> targets;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
};

std::string needs_options19(Kit& kit, std::size_t word, const char* what) {
    std::uint32_t data[16] = {};
    const HRESULT result = kit.device()->CheckFeatureSupport(static_cast<D3D12_FEATURE>(48), data, 40);
    return SUCCEEDED(result) && data[word] != 0 ? std::string() : std::string(what);
}

bool narrow_line_reference(Kit&, unsigned, rr::CaptureSet& out, std::string&) {
    rr::CpuRaster raster(128, 128, rr::CpuRaster::pack(0.05f, 0.05f, 0.1f));
    std::vector<std::pair<float, float>> points;
    for (int k = 0; k < 24; ++k) {
        const float a = static_cast<float>(k) * 0.2618f;
        const float r = (k & 1) != 0 ? 0.9f : 0.35f;
        points.emplace_back((r * std::cos(a) * 0.5f + 0.5f) * 128.0f, (0.5f - r * std::sin(a) * 0.5f) * 128.0f);
    }
    auto clip = [](float x, float y) { return rr::Vec4{x / 64.0f - 1.0f, 1.0f - y / 64.0f, 0.5f, 1.0f}; };
    for (std::size_t k = 0; k + 1 < points.size(); k += 2) {
        const float dx = points[k + 1].first - points[k].first;
        const float dy = points[k + 1].second - points[k].second;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / length * 0.5f;
        const float ny = dx / length * 0.5f;
        const rr::Vec4 corners[4] = {clip(points[k].first + nx, points[k].second + ny), clip(points[k].first - nx, points[k].second - ny),
                                     clip(points[k + 1].first - nx, points[k + 1].second - ny), clip(points[k + 1].first + nx, points[k + 1].second + ny)};
        const rr::Vec4 first[3] = {corners[0], corners[1], corners[2]};
        const rr::Vec4 second[3] = {corners[0], corners[2], corners[3]};
        auto shade = [](int, int, bool, float, const float*) { return rr::CpuRaster::pack(0.9f, 0.9f, 0.3f); };
        raster.triangle(first, rr::CpuRaster::Cull::None, false, shade);
        raster.triangle(second, rr::CpuRaster::Cull::None, false, shade);
    }
    out.add("", raster.take());
    return true;
}

void register_line_modes() {
    struct Mode {
        const char* name;
        UINT value;
        bool blend;
        std::size_t cap_word;
        const char* cap_text;
    };
    const Mode modes[] = {{"aliased", 0, false, 3, "rasterizer desc 2 (line rasterization modes)"}, {"alpha_antialiased", 1, true, 3, "rasterizer desc 2 (line rasterization modes)"},
                          {"quadrilateral_wide", 2, false, 3, "rasterizer desc 2 (line rasterization modes)"}, {"quadrilateral_narrow", 3, false, 4, "narrow quadrilateral lines"}};
    for (const Mode& mode : modes) {
        rr::CaseSpec spec("rs", std::string("lines_desc2_") + mode.name);
        spec.tolerance(mode.value == 1 ? rr::Tolerance::psnr(8) : (mode.value == 2 ? rr::Tolerance::blend(4).with_outliers(0.003) : (mode.value == 3 ? rr::Tolerance::blend(4).with_outliers(0.004) : rr::Tolerance::raster(4, 1)))).tag("raster");
        if (mode.value == 0) {
            spec.same_as({"rs.lines_aliased_list"}, "the same star of line segments through the rasterizer desc 2 stream instead of the legacy rasterizer desc; the aliased mode must give the legacy image");
        } else if (mode.value == 1) {
            spec.same_as({"rs.lines_antialiased_list"}, "the same star of line segments through the rasterizer desc 2 stream instead of the legacy AntialiasedLineEnable; the alpha antialiased mode must give the legacy image");
        }
        Cases::single(spec,
            [mode](Kit& kit, rr::CaptureSet& out, std::string& error) {
                Stage stage;
                if (!stage.open(kit, error)) {
                    return false;
                }
                std::vector<Vertex> points;
                for (int k = 0; k < 24; ++k) {
                    const float a = static_cast<float>(k) * 0.2618f;
                    const float r = (k & 1) != 0 ? 0.9f : 0.35f;
                    points.push_back({{r * std::cos(a), r * std::sin(a), 0.0f}, {0, 0, 1}, {0, 0}});
                }
                Geometry geometry;
                if (!geometry.create_vertices(kit, points.data(), static_cast<UINT>(points.size()), sizeof(Vertex), error)) {
                    return false;
                }
                Pso builder(kit, stage.canvas.binder.signature);
                builder.vs(Hlsl12::with_root(Hlsl12::kFlatVs)).ps(Hlsl12::with_root("float4 main(float4 pos : SV_Position) : SV_Target { return p0; }\n")).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                if (!builder.failure.empty()) {
                    error = builder.failure;
                    return false;
                }
                Com<ID3D12Device2> device2 = kit.rig.as<ID3D12Device2>();
                if (!device2) {
                    return fail_with(error, "ID3D12Device2 is not available");
                }
                LineStream stream;
                stream.root = stage.canvas.binder.signature;
                stream.vs = D3d12Tools::bytecode(builder.vs_code);
                stream.ps = D3d12Tools::bytecode(builder.ps_code);
                stream.layout = D3D12_INPUT_LAYOUT_DESC{builder.elements.data(), static_cast<UINT>(builder.elements.size())};
                D3D12_BLEND_DESC blend = builder.desc.BlendState;
                if (mode.blend) {
                    blend.RenderTarget[0].BlendEnable = TRUE;
                    blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
                    blend.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
                }
                stream.blend = blend;
                Rasterizer2 raster{};
                raster.fill = D3D12_FILL_MODE_SOLID;
                raster.cull = D3D12_CULL_MODE_NONE;
                raster.depth_clip = TRUE;
                raster.line_mode = kit.broken("line_mode") ? (mode.value == 0 || mode.value == 3 ? 2 : 0) : mode.value;
                stream.rasterizer = raster;
                D3D12_RT_FORMAT_ARRAY formats{};
                formats.NumRenderTargets = 1;
                formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                stream.targets = formats;
                stream.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
                D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof stream, &stream};
                Com<ID3D12PipelineState> state_com;
                const HRESULT created = device2->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), state_com.put_void());
                if (FAILED(created)) {
                    return fail_with(error, "CreatePipelineState (rasterizer desc 2): " + D3d12Tools::hr(created));
                }
                ID3D12PipelineState* state = kit.rig.hold(std::move(state_com));
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                        stage.canvas.begin(list, clear);
                        list->SetPipelineState(state);
                        const float color[4] = {0.9f, 0.9f, 0.3f, 1.0f};
                        set_cbv_root(list, rr::Mat4::identity(), color);
                        geometry.bind(list, D3D_PRIMITIVE_TOPOLOGY_LINELIST);
                        geometry.draw_vertices(list);
                    },
                    error);
                if (!ok || !stage.finish(out, error)) {
                    return false;
                }
                if (mode.value == 1) {
                    const std::string judgement = LineJudge::antialiased(out.items().back().second, points);
                    if (!judgement.empty()) {
                        return fail_with(error, judgement);
                    }
                }
                return true;
            },
            [mode](Kit& kit) { return needs_options19(kit, mode.cap_word, mode.cap_text); },
            mode.value == 3 ? Cases::RefFn(narrow_line_reference) : Cases::RefFn());
    }
}

struct DepthStencilOp2 {
    D3D12_STENCIL_OP fail;
    D3D12_STENCIL_OP depth_fail;
    D3D12_STENCIL_OP pass;
    D3D12_COMPARISON_FUNC func;
    UINT8 read_mask;
    UINT8 write_mask;
};

struct DepthStencil2 {
    BOOL depth_enable;
    D3D12_DEPTH_WRITE_MASK depth_write;
    D3D12_COMPARISON_FUNC depth_func;
    BOOL stencil_enable;
    DepthStencilOp2 front;
    DepthStencilOp2 back;
    BOOL depth_bounds;
};

constexpr D3D12_PIPELINE_STATE_SUBOBJECT_TYPE kDepthStencil2 = static_cast<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE>(26);

struct StencilStream {
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> layout;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
    Sub<kDepthStencil2, DepthStencil2> depth_stencil;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> targets;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depth_format;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
};

MeshData bowl_shell() {
    MeshData full = Shapes::sphere(20, 28, 0.9f);
    MeshData shell;
    shell.vertices = full.vertices;
    for (std::size_t i = 0; i + 2 < full.indices.size(); i += 3) {
        const float y = (full.vertices[full.indices[i]].pos[1] + full.vertices[full.indices[i + 1]].pos[1] + full.vertices[full.indices[i + 2]].pos[1]) / 3.0f;
        if (y < 0.25f) {
            shell.indices.insert(shell.indices.end(), full.indices.begin() + static_cast<std::ptrdiff_t>(i), full.indices.begin() + static_cast<std::ptrdiff_t>(i) + 3);
        }
    }
    return shell;
}

bool stencil_reference(Kit&, unsigned, rr::CaptureSet& out, std::string&) {
    const MeshData shell = bowl_shell();
    const rr::Mat4 matrix = rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f) * rr::Mat4::rotate_x(0.9f);
    rr::CpuRaster raster(128, 128, rr::CpuRaster::pack(0.05f, 0.05f, 0.1f));
    for (std::size_t i = 0; i + 2 < shell.indices.size(); i += 3) {
        rr::Vec4 clip[3];
        for (int k = 0; k < 3; ++k) {
            const Vertex& v = shell.vertices[shell.indices[i + static_cast<std::size_t>(k)]];
            clip[k] = rr::CpuRaster::transform(matrix, v.pos[0], v.pos[1], v.pos[2]);
        }
        raster.triangle(clip, rr::CpuRaster::Cull::None, true, [](int, int, bool front, float, const float*) {
            return front ? rr::CpuRaster::pack(0.2f, 0.9f, 0.3f) : rr::CpuRaster::pack(0.95f, 0.2f, 0.2f);
        });
    }
    out.add("", raster.take());
    return true;
}

void register_front_back_stencil_ref() {
    Cases::single(
        rr::CaseSpec("om", "stencil_front_and_back_ref_and_masks_independent").tolerance(rr::Tolerance::blend(2).with_edge(1)).tag("stencil"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            Stage stage;
            if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D24_UNORM_S8_UINT)) {
                return false;
            }
            const MeshData shell = bowl_shell();
            Geometry bowl;
            if (!bowl.create(kit, shell, true, error)) {
                return false;
            }
            Pso writer(kit, stage.canvas.binder.signature);
            writer.vs(Hlsl12::with_root(Hlsl12::kLitVs)).ps(Hlsl12::with_root(Hlsl12::kLitPs)).targets({DXGI_FORMAT_R8G8B8A8_UNORM}).depth(DXGI_FORMAT_D24_UNORM_S8_UINT);
            if (!writer.failure.empty()) {
                error = writer.failure;
                return false;
            }
            Com<ID3D12Device2> device2 = kit.rig.as<ID3D12Device2>();
            if (!device2) {
                return fail_with(error, "ID3D12Device2 is not available");
            }
            StencilStream stream;
            stream.root = stage.canvas.binder.signature;
            stream.vs = D3d12Tools::bytecode(writer.vs_code);
            stream.ps = D3d12Tools::bytecode(writer.ps_code);
            stream.layout = D3D12_INPUT_LAYOUT_DESC{writer.elements.data(), static_cast<UINT>(writer.elements.size())};
            stream.rasterizer = writer.desc.RasterizerState;
            DepthStencil2 ds{};
            ds.depth_enable = TRUE;
            ds.depth_write = D3D12_DEPTH_WRITE_MASK_ALL;
            ds.depth_func = D3D12_COMPARISON_FUNC_LESS;
            ds.stencil_enable = TRUE;
            ds.front = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_REPLACE, D3D12_COMPARISON_FUNC_ALWAYS, 0xff, 0xf0};
            ds.back = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_REPLACE, D3D12_COMPARISON_FUNC_ALWAYS, 0xff, 0x0f};
            stream.depth_stencil = ds;
            D3D12_RT_FORMAT_ARRAY formats{};
            formats.NumRenderTargets = 1;
            formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            stream.targets = formats;
            stream.depth_format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            stream.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof stream, &stream};
            Com<ID3D12PipelineState> writer_com;
            const HRESULT created = device2->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), writer_com.put_void());
            if (FAILED(created)) {
                return fail_with(error, "CreatePipelineState (depth stencil 2): " + D3d12Tools::hr(created));
            }
            ID3D12PipelineState* writer_state = kit.rig.hold(std::move(writer_com));
            auto visual = [&](UINT8 unused) -> ID3D12PipelineState* {
                (void)unused;
                Pso builder(kit, stage.canvas.binder.signature);
                builder.vs(Hlsl12::kFullVs).ps(Hlsl12::with_root(Hlsl12::kSolidPs)).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                builder.desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
                builder.desc.DepthStencilState.DepthEnable = FALSE;
                builder.desc.DepthStencilState.StencilEnable = TRUE;
                builder.desc.DepthStencilState.StencilReadMask = 0xff;
                builder.desc.DepthStencilState.StencilWriteMask = 0;
                builder.desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_EQUAL};
                builder.desc.DepthStencilState.BackFace = builder.desc.DepthStencilState.FrontFace;
                return builder.build(error);
            };
            ID3D12PipelineState* visual_state = visual(0);
            if (visual_state == nullptr) {
                return false;
            }
            const bool independent = !kit.broken("stencil_ref");
            bool have_list8 = true;
            const bool ok = kit.run(
                [&](ID3D12GraphicsCommandList* list) {
                    ID3D12GraphicsCommandList8* list8 = nullptr;
                    if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList8), reinterpret_cast<void**>(&list8)))) {
                        have_list8 = false;
                        return;
                    }
                    const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                    stage.canvas.begin(list, clear);
                    list->SetPipelineState(writer_state);
                    if (independent) {
                        list8->OMSetFrontAndBackStencilRef(0x11, 0x22);
                    } else {
                        list->OMSetStencilRef(0x11);
                    }
                    stage.mesh(list, bowl, rr::Mat4::rotate_x(0.9f), 0.5f, 0.5f, 0.55f);
                    list->SetPipelineState(visual_state);
                    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    const float green[4] = {0.2f, 0.9f, 0.3f, 1};
                    const float red[4] = {0.95f, 0.2f, 0.2f, 1};
                    list->SetGraphicsRoot32BitConstants(0, 4, green, 16);
                    list->OMSetStencilRef(0x10);
                    list->DrawInstanced(3, 1, 0, 0);
                    list->SetGraphicsRoot32BitConstants(0, 4, red, 16);
                    list->OMSetStencilRef(0x02);
                    list->DrawInstanced(3, 1, 0, 0);
                    list8->Release();
                },
                error);
            if (!have_list8) {
                return fail_with(error, "ID3D12GraphicsCommandList8 is not available");
            }
            return ok && stage.finish(out, error);
        },
        [](Kit& kit) {
            if (kit.warp) {
                return std::string("OMSetFrontAndBackStencilRef (the WARP device faults with 0xc0000005 on the call)");
            }
            D3D12_FEATURE_DATA_D3D12_OPTIONS14 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS14, options);
            return SUCCEEDED(result) && options.IndependentFrontAndBackStencilRefMaskSupported ? std::string() : std::string("independent front and back stencil reference and masks");
        },
        stencil_reference);
}

const struct Registrar {
    Registrar() {
        register_vrs();
        register_sampler_feedback();
        register_work_graphs();
        register_resolve_modes();
        register_sample_positions();
        register_forced_sample_count();
        register_stream_output_statistics();
        register_atomics64();
        register_gpu_upload_heap();
        register_indirect_views();
        register_line_modes();
        register_front_back_stencil_ref();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
