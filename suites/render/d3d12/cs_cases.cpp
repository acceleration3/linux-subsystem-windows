#include "render/d3d12/compute.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

namespace {

rr::Image words_image(const std::vector<std::uint32_t>& words, unsigned width) {
    const unsigned pixels = static_cast<unsigned>((words.size() + 3) / 4);
    rr::Image image(width, (pixels + width - 1) / width, rr::PixelKind::Rgba32Uint);
    std::memcpy(image.bytes().data(), words.data(), words.size() * 4);
    return image;
}

ID3D12Resource* uav_buffer(Kit& kit, UINT words, std::string& error, const std::uint32_t* initial = nullptr) {
    HRESULT result = S_OK;
    ID3D12Resource* buffer = D3d12Tools::device_buffer(kit.rig, static_cast<UINT64>(words) * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, &result);
    if (buffer == nullptr) {
        error = "UAV buffer creation: " + D3d12Tools::hr(result);
        return nullptr;
    }
    std::vector<std::uint32_t> zeros(words, 0);
    if (!D3d12Tools::write_buffer(kit.rig, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, initial != nullptr ? initial : zeros.data(), static_cast<UINT64>(words) * 4, error)) {
        return nullptr;
    }
    return buffer;
}

ID3D12Resource* srv_words(Kit& kit, const std::vector<std::uint32_t>& data, std::string& error) {
    return kit.make_upload(data.data(), data.size() * 4, error);
}

std::vector<std::uint32_t> noise_words(UINT count, std::uint32_t seed, std::uint32_t modulo) {
    std::vector<std::uint32_t> out(count);
    std::uint32_t state = seed;
    for (auto& w : out) {
        w = static_cast<std::uint32_t>(rr::Pattern::unit_random(state) * static_cast<float>(modulo));
    }
    return out;
}

struct UavFormat {
    const char* name;
    DXGI_FORMAT format;
    const char* type;
    const char* store;
};

void register_uav_formats() {
    const UavFormat formats[] = {
        {"r8g8b8a8_unorm", DXGI_FORMAT_R8G8B8A8_UNORM, "float4", "float4(p.x, p.y, 0.5 + 0.5 * sin(p.x * 20.0), 1.0)"},
        {"r16g16b16a16_float", DXGI_FORMAT_R16G16B16A16_FLOAT, "float4", "float4(p.x, p.y * 2.0, 0.5 + 0.5 * sin(p.x * 20.0), 0.75)"},
        {"r32g32b32a32_float", DXGI_FORMAT_R32G32B32A32_FLOAT, "float4", "float4(p.x, p.y * 3.0, sin(p.x * 20.0), 0.25)"},
        {"r32_float", DXGI_FORMAT_R32_FLOAT, "float4", "float4(p.x * p.y + 0.1 * sin(p.y * 30.0), 0, 0, 0)"},
        {"r8_unorm", DXGI_FORMAT_R8_UNORM, "float4", "float4(p.x * 0.5 + p.y * 0.5, 0, 0, 0)"},
        {"r8g8b8a8_uint", DXGI_FORMAT_R8G8B8A8_UINT, "uint4", "uint4(p.x * 255.0, p.y * 255.0, (uint)(p.x * 40) * 6, 255)"},
        {"r16g16b16a16_uint", DXGI_FORMAT_R16G16B16A16_UINT, "uint4", "uint4(p.x * 4000.0, p.y * 4000.0, (uint)(p.y * 40) * 99, 7)"},
        {"r32g32b32a32_uint", DXGI_FORMAT_R32G32B32A32_UINT, "uint4", "uint4(p.x * 100000.0, p.y * 70000.0, (uint)(p.x * 64) * (uint)(p.y * 64), 1)"},
    };
    for (const UavFormat& item : formats) {
        const bool integer = std::string(item.type) == "uint4";
        Cases::single(
            rr::CaseSpec("cs", std::string("uav_store_") + item.name).tolerance(integer ? rr::Tolerance::exact() : rr::Tolerance::floats(2e-3f, 2e-3f)).tag("compute"),
            [item](Kit& kit, rr::CaptureSet& out, std::string& error) {
                TextureSpec spec;
                spec.format = item.format;
                spec.width = 64;
                spec.height = 64;
                spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
                if (texture == nullptr) {
                    return false;
                }
                const std::string source = std::string("RWTexture2D<") + item.type + "> dst : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n  float2 p = (id.xy + 0.5) / 64.0;\n  dst[id.xy] = " +
                                           item.store + ";\n}\n";
                Cs cs;
                if (!cs.open(kit, source, error)) {
                    return false;
                }
                cs.binder.uav_texture(0, texture, item.format);
                const bool ok = kit.run(
                    [&](ID3D12GraphicsCommandList* list) {
                        D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                        cs.bind(list);
                        list->Dispatch(8, 8, 1);
                    },
                    error);
                rr::Image image;
                if (!ok || !kit.read_image(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, item.format, image, error)) {
                    return false;
                }
                out.add("", std::move(image));
                return true;
            },
            [item](Kit& kit) { return needs_format(kit, item.format, D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW, "typed UAV stores of this format"); });
    }
}

bool run_words(Kit& kit, const std::string& source, UINT dispatch_x, UINT in_words, std::uint32_t seed, std::uint32_t modulo, UINT out_words, UINT image_width, rr::CaptureSet& out,
               std::string& error, const std::vector<const wchar_t*>& args = {}, bool sorted = false) {
    Cs cs;
    if (!cs.open(kit, source, error, args)) {
        return false;
    }
    ID3D12Resource* input = srv_words(kit, noise_words(in_words, seed, modulo), error);
    ID3D12Resource* output = uav_buffer(kit, out_words, error);
    if (input == nullptr || output == nullptr) {
        return false;
    }
    cs.binder.srv_buffer(0, input, DXGI_FORMAT_UNKNOWN, 0, in_words, 4);
    cs.binder.uav_buffer(0, output, DXGI_FORMAT_UNKNOWN, 0, out_words, 4);
    if (!kit.run([&](ID3D12GraphicsCommandList* list) {
            cs.bind(list);
            list->Dispatch(dispatch_x, 1, 1);
        }, error)) {
        return false;
    }
    std::vector<std::uint32_t> words;
    if (!D3d12Tools::read_words(kit.rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, out_words, words, error)) {
        return false;
    }
    if (sorted) {
        std::sort(words.begin(), words.end());
    }
    out.add("", words_image(words, image_width));
    return true;
}

void register_buffers() {
    Cases::single(rr::CaseSpec("cs", "groupshared_reduction_sum").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const char* source =
            "StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> dst : register(u0);\ngroupshared uint smem[256];\n"
            "[numthreads(256, 1, 1)] void main(uint3 gid : SV_GroupID, uint t : SV_GroupIndex) {\n"
            "  smem[t] = src[gid.x * 256 + t]; GroupMemoryBarrierWithGroupSync();\n"
            "  for (uint s = 128; s > 0; s >>= 1) { if (t < s) smem[t] += smem[t + s]; GroupMemoryBarrierWithGroupSync(); }\n"
            "  if (t == 0) dst[gid.x] = smem[0]; }\n";
        return run_words(kit, source, 16, 4096, 11, 1000, 16, 2, out, error);
    });
    Cases::single(rr::CaseSpec("cs", "groupshared_prefix_scan").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const char* source =
            "StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> dst : register(u0);\ngroupshared uint a[256];\n"
            "[numthreads(256, 1, 1)] void main(uint t : SV_GroupIndex) {\n"
            "  a[t] = src[t]; GroupMemoryBarrierWithGroupSync();\n"
            "  for (uint o = 1; o < 256; o <<= 1) { uint v = t >= o ? a[t - o] : 0; GroupMemoryBarrierWithGroupSync(); a[t] += v; GroupMemoryBarrierWithGroupSync(); }\n"
            "  dst[t] = a[t]; }\n";
        return run_words(kit, source, 1, 256, 5, 100, 256, 8, out, error);
    });
    Cases::single(rr::CaseSpec("cs", "atomic_histogram").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const char* source =
            "StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> bins : register(u0);\n"
            "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { uint v = src[id.x] & 63; uint old; InterlockedAdd(bins[v], 1 + (v & 3), old); }\n";
        return run_words(kit, source, 64, 4096, 21, 64, 64, 4, out, error);
    });
    Cases::single(rr::CaseSpec("cs", "atomic_min_max_or_xor_exchange").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const char* source =
            "StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> r : register(u0);\n"
            "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID, uint t : SV_GroupIndex) {\n"
            "  uint v = src[id.x]; uint o; InterlockedMin(r[0], v, o); InterlockedMax(r[1], v, o); InterlockedOr(r[2], v, o); InterlockedXor(r[3], v, o);\n"
            "  InterlockedAdd(r[4], v & 7, o); if (id.x == 100) { uint old; InterlockedCompareExchange(r[5], 0, v + 1, old); } }\n";
        Cs cs;
        if (!cs.open(kit, source, error)) {
            return false;
        }
        ID3D12Resource* input = srv_words(kit, noise_words(1024, 3, 100000), error);
        const std::uint32_t init[8] = {0xffffffffu, 0, 0, 0, 0, 0, 0, 0};
        ID3D12Resource* output = uav_buffer(kit, 8, error, init);
        if (input == nullptr || output == nullptr) {
            return false;
        }
        cs.binder.srv_buffer(0, input, DXGI_FORMAT_UNKNOWN, 0, 1024, 4);
        cs.binder.uav_buffer(0, output, DXGI_FORMAT_UNKNOWN, 0, 8, 4);
        if (!kit.run([&](ID3D12GraphicsCommandList* list) { cs.bind(list); list->Dispatch(16, 1, 1); }, error)) {
            return false;
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 8, words, error)) {
            return false;
        }
        out.add("", words_image(words, 2));
        return true;
    });
    Cases::single(rr::CaseSpec("cs", "append_buffer_uav_counter").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const char* source =
            "StructuredBuffer<uint> src : register(t0); AppendStructuredBuffer<uint> list : register(u0);\n"
            "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) { uint v = src[id.x]; if ((v % 3) == 0) list.Append(v); }\n";
        Cs cs;
        if (!cs.open(kit, source, error)) {
            return false;
        }
        ID3D12Resource* input = srv_words(kit, noise_words(512, 17, 90), error);
        ID3D12Resource* output = uav_buffer(kit, 1024 + 16, error);
        if (input == nullptr || output == nullptr) {
            return false;
        }
        cs.binder.srv_buffer(0, input, DXGI_FORMAT_UNKNOWN, 0, 512, 4);
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = 512;
        desc.Buffer.StructureByteStride = 4;
        desc.Buffer.CounterOffsetInBytes = 4096;
        kit.device()->CreateUnorderedAccessView(output, output, &desc, D3d12Tools::cpu_at(cs.binder.heap, Binder::kSrvSlots, cs.binder.increment));
        if (!kit.run([&](ID3D12GraphicsCommandList* list) { cs.bind(list); list->Dispatch(8, 1, 1); }, error)) {
            return false;
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 1024 + 16, words, error)) {
            return false;
        }
        const std::uint32_t count = words[1024];
        std::vector<std::uint32_t> values(words.begin(), words.begin() + std::min<std::size_t>(count, 512));
        std::sort(values.begin(), values.end());
        values.resize(512, 0);
        values[0] = count;
        out.add("", words_image(values, 16));
        return true;
    });
    Cases::single(rr::CaseSpec("cs", "dispatch_indirect_args_from_buffer").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        const char* source =
            "RWStructuredBuffer<uint> dst : register(u0);\n"
            "[numthreads(4, 1, 1)] void main(uint3 gid : SV_GroupID, uint3 id : SV_DispatchThreadID) { uint o; InterlockedAdd(dst[0], 1, o); dst[1 + id.x + id.y * 8 + id.z * 32] = gid.x * 100 + gid.y * 10 + gid.z + 1; }\n";
        Cs cs;
        if (!cs.open(kit, source, error)) {
            return false;
        }
        ID3D12Resource* output = uav_buffer(kit, 128, error);
        const UINT args[3] = {2, 3, 2};
        ID3D12Resource* arguments = kit.make_upload(args, sizeof args, error);
        if (output == nullptr || arguments == nullptr) {
            return false;
        }
        cs.binder.uav_buffer(0, output, DXGI_FORMAT_UNKNOWN, 0, 128, 4);
        D3D12_INDIRECT_ARGUMENT_DESC argument{};
        argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC signature_desc{};
        signature_desc.ByteStride = sizeof(args);
        signature_desc.NumArgumentDescs = 1;
        signature_desc.pArgumentDescs = &argument;
        Com<ID3D12CommandSignature> signature;
        const HRESULT created = kit.device()->CreateCommandSignature(&signature_desc, nullptr, __uuidof(ID3D12CommandSignature), signature.put_void());
        if (FAILED(created)) {
            return fail_with(error, "CreateCommandSignature: " + D3d12Tools::hr(created));
        }
        ID3D12CommandSignature* held = kit.rig.hold(std::move(signature));
        if (!kit.run([&](ID3D12GraphicsCommandList* list) { cs.bind(list); list->ExecuteIndirect(held, 1, arguments, 0, nullptr, 0); }, error)) {
            return false;
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 128, words, error)) {
            return false;
        }
        out.add("", words_image(words, 8));
        return true;
    });
}

void register_texture_uavs() {
    Cases::single(rr::CaseSpec("cs", "uav_texture3d_slices").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        spec.format = DXGI_FORMAT_R8G8B8A8_UINT;
        spec.width = 16;
        spec.height = 16;
        spec.depth_or_array = 8;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
        Cs cs;
        if (texture == nullptr || !cs.open(kit, "RWTexture3D<uint4> dst : register(u0);\n[numthreads(4, 4, 4)] void main(uint3 id : SV_DispatchThreadID) { dst[id] = uint4(id.x * 16, id.y * 16, id.z * 32, (id.x ^ id.y ^ id.z) * 8); }\n", error)) {
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D.WSize = 8;
        cs.binder.uav(0, texture, &desc);
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cs.bind(list);
                list->Dispatch(4, 4, 2);
            }, error)) {
            return false;
        }
        rr::Image image;
        if (!kit.read_image(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, DXGI_FORMAT_R8G8B8A8_UINT, image, error)) {
            return false;
        }
        out.add("", std::move(image));
        return true;
    });
    Cases::single(rr::CaseSpec("cs", "uav_texture2d_array_and_mip_slice").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.format = DXGI_FORMAT_R8G8B8A8_UINT;
        spec.width = 32;
        spec.height = 32;
        spec.depth_or_array = 3;
        spec.mips = 3;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
        Cs cs;
        if (texture == nullptr || !cs.open(kit, "RWTexture2DArray<uint4> dst : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { dst[uint3(id.xy, 2)] = uint4(id.x * 40, id.y * 40, 7, 255); dst[uint3(id.xy, 0)] = uint4(id.y * 40, id.x * 40, 9, 255); }\n", error)) {
            return false;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
        desc.Texture2DArray.MipSlice = 2;
        desc.Texture2DArray.ArraySize = 3;
        cs.binder.uav(0, texture, &desc);
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cs.bind(list);
                list->Dispatch(1, 1, 1);
            }, error)) {
            return false;
        }
        for (UINT slice = 0; slice < 3; slice += 2) {
            rr::Image image;
            if (!kit.read_image(texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 2 + slice * 3, DXGI_FORMAT_R8G8B8A8_UINT, image, error)) {
                return false;
            }
            out.add("slice" + std::to_string(slice), std::move(image));
        }
        return true;
    });
    Cases::single(rr::CaseSpec("cs", "texture_written_by_compute_sampled_by_draw").tolerance(sampler_tolerance()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        TextureSpec spec;
        spec.width = 64;
        spec.height = 64;
        spec.mips = 1;
        spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        ID3D12Resource* texture = TextureFactory::create(kit, spec, false, error);
        Cs cs;
        if (texture == nullptr || !cs.open(kit, "RWTexture2D<float4> dst : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) { float2 p = (id.xy + 0.5) / 64.0; float r = length(p - 0.5); dst[id.xy] = float4(0.5 + 0.5 * sin(r * 60.0), p.x, p.y, 1); }\n", error)) {
            return false;
        }
        cs.binder.uav_texture(0, texture, DXGI_FORMAT_R8G8B8A8_UNORM);
        if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cs.bind(list);
                list->Dispatch(8, 8, 1);
                D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }, error)) {
            return false;
        }
        const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const float tint[4] = {1, 1, 1, 1};
        const float scale[4] = {1, 1, 0, 0};
        return sample_scene(kit, texture, srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1), &sampler,
                            std::string(kSamplePrefix) + "Texture2D<float4> tex : register(t0); SamplerState smp : register(s0);\nfloat4 main(VsOut i) : SV_Target { return tex.Sample(smp, i.uv); }\n", tint, scale, -0.4f,
                            out, error);
    });
}

const char* kWaveSum =
    "RWStructuredBuffer<uint> r : register(u0);\n"
    "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
    "  uint v = id.x * 7 + 1; uint s = WaveActiveSum(v); uint mx = WaveActiveMax(v); uint mn = WaveActiveMin(v); uint o;\n"
    "  if (WaveIsFirstLane()) { InterlockedAdd(r[0], s, o); InterlockedMax(r[1], mx, o); }\n"
    "  uint count = WaveActiveCountBits(v % 3 == 0); if (WaveIsFirstLane()) InterlockedAdd(r[2], count, o);\n"
    "  uint prefix = WavePrefixSum(1); if (prefix != WavePrefixCountBits(true)) InterlockedAdd(r[3], 1, o);\n"
    "  if (WaveActiveAllTrue(v > 0)) InterlockedAdd(r[4], 1, o);\n"
    "  uint first = WaveReadLaneFirst(id.x / 64); uint lane = WaveActiveSum(first == id.x / 64 ? 1u : 0u); if (WaveIsFirstLane()) InterlockedAdd(r[5], lane, o);\n"
    "  bool eq = WaveActiveAllEqual(id.x / 64); InterlockedAdd(r[6], eq ? 1 : 0, o);\n"
    "  uint minAll = 0; InterlockedMin(r[7], mn == 0 ? 0xffffffffu : mn, minAll); }\n";

void register_wave() {
    Cases::single(rr::CaseSpec("cs", "wave_intrinsics_totals_sm6").variant(1).tolerance(rr::Tolerance::exact()).tag("compute").tag("x64-only"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Cs cs;
        if (!cs.open(kit, kWaveSum, error)) {
            return false;
        }
        const std::uint32_t init[8] = {0, 0, 0, 0, 0, 0, 0, 0xffffffffu};
        ID3D12Resource* output = uav_buffer(kit, 8, error, init);
        if (output == nullptr) {
            return false;
        }
        cs.binder.uav_buffer(0, output, DXGI_FORMAT_UNKNOWN, 0, 8, 4);
        if (!kit.run([&](ID3D12GraphicsCommandList* list) { cs.bind(list); list->Dispatch(16, 1, 1); }, error)) {
            return false;
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 8, words, error)) {
            return false;
        }
        out.add("", words_image(words, 2));
        return true;
    });
    for (int mode = 0; mode < 2; ++mode) {
        Cases::single(rr::CaseSpec("cs", mode == 0 ? "quad_read_across_x_y_diagonal_sm6" : "quad_read_lane_at_sm6").variant(1).tolerance(rr::Tolerance::blend(2)).tag("compute").tag("x64-only"),
                      [mode](Kit& kit, rr::CaptureSet& out, std::string& error) {
                          Stage stage;
                          if (!stage.open(kit, error)) {
                              return false;
                          }
                          const char* body0 =
                              "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
                              "  float v = (float)((((uint)pos.x * 7u + (uint)pos.y * 13u + ((uint)pos.x ^ (uint)pos.y) * 3u) % 17u)) / 16.0;\n"
                              "  float x = QuadReadAcrossX(v); float y = QuadReadAcrossY(v); float d = QuadReadAcrossDiagonal(v);\n"
                              "  return float4(abs(v - x), abs(v - y), abs(v - d), 1); }\n";
                          const char* body1 =
                              "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {\n"
                              "  float v = (float)((((uint)pos.x * 7u + (uint)pos.y * 13u + ((uint)pos.x ^ (uint)pos.y) * 3u) % 17u)) / 16.0;\n"
                              "  float a = QuadReadLaneAt(v, 0); float b = QuadReadLaneAt(v, 1); float c = QuadReadLaneAt(v, 2); float e = QuadReadLaneAt(v, 3);\n"
                              "  return float4(a, b, (c + e) * 0.5, 1); }\n";
                          Pso builder(kit, stage.canvas.binder.signature);
                          builder.vs(Hlsl12::kFullVs).ps(mode == 0 ? body0 : body1).layout({}).targets({DXGI_FORMAT_R8G8B8A8_UNORM});
                          ID3D12PipelineState* state = builder.build(error);
                          if (state == nullptr) {
                              return false;
                          }
                          const bool ok = kit.run(
                              [&](ID3D12GraphicsCommandList* list) {
                                  const float clear[4] = {0, 0, 0, 1};
                                  stage.canvas.begin(list, clear);
                                  list->SetPipelineState(state);
                                  list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                                  list->DrawInstanced(3, 1, 0, 0);
                              },
                              error);
                          return ok && stage.finish(out, error);
                      });
    }
}

void register_shader_model_features() {
    Cases::single(
        rr::CaseSpec("cs", "native_16bit_types_sm6_2").variant(3).tolerance(rr::Tolerance::exact()).tag("compute").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            const char* source =
                "StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> dst : register(u0);\n"
                "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
                "  uint v = src[id.x]; float16_t a = (float16_t)(v & 255) * (float16_t)0.5; precise float16_t sq = a * a; precise float16_t b = sq + (float16_t)3.0; uint16_t k = (uint16_t)(v >> 3) * (uint16_t)3;\n"
                "  dst[id.x] = (uint)b + ((uint)k << 16); }\n";
            return run_words(kit, source, 4, 256, 9, 4000, 256, 8, out, error, {L"-enable-16bit-types"});
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS4 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS4, options);
            return SUCCEEDED(result) && options.Native16BitShaderOpsSupported ? std::string() : std::string("native 16-bit shader ops");
        });
    Cases::single(
        rr::CaseSpec("cs", "int64_shader_ops_sm6").variant(1).tolerance(rr::Tolerance::exact()).tag("compute").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            const char* source =
                "StructuredBuffer<uint> src : register(t0); RWStructuredBuffer<uint> dst : register(u0);\n"
                "[numthreads(64, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
                "  uint64_t a = (uint64_t)src[id.x] * 4294967297ull + 12345; uint64_t b = (a >> 7) ^ (a * 31); dst[id.x * 2] = (uint)(b & 0xffffffff); dst[id.x * 2 + 1] = (uint)(b >> 32); }\n";
            return run_words(kit, source, 2, 128, 4, 1000000, 256, 8, out, error);
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS1 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS1, options);
            return SUCCEEDED(result) && options.Int64ShaderOps ? std::string() : std::string("Int64 shader ops");
        });
    Cases::single(
        rr::CaseSpec("cs", "dynamic_resources_descriptor_heap_sm6_6").variant(7).tolerance(rr::Tolerance::blend(2)).tag("compute").tag("x64-only"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) {
            TextureSpec spec;
            spec.width = 64;
            spec.height = 64;
            spec.content = 3;
            ID3D12Resource* source_texture = TextureFactory::create(kit, spec, true, error);
            TextureSpec target_spec = spec;
            target_spec.flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ID3D12Resource* target = TextureFactory::create(kit, target_spec, false, error);
            if (source_texture == nullptr || target == nullptr) {
                return false;
            }
            D3d12Root root;
            root.constants(4, 0).flags(static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(0x400 | 0x800));
            ID3D12RootSignature* signature = root.build(kit.rig, error);
            if (signature == nullptr) {
                return false;
            }
            const char* source =
                "cbuffer C : register(b0) { uint4 indices; };\n"
                "[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
                "  Texture2D<float4> src = ResourceDescriptorHeap[indices.x]; RWTexture2D<float4> dst = ResourceDescriptorHeap[indices.y]; SamplerState smp = SamplerDescriptorHeap[0];\n"
                "  float2 uv = (id.xy + 0.5) / 64.0; float4 c = src.SampleLevel(smp, uv, 0); dst[id.xy] = float4(c.bgr * (0.5 + 0.5 * uv.x), 1); }\n";
            Kit::Bytes code;
            if (!kit.compile_as(source, "main", "cs", code, error)) {
                return false;
            }
            ID3D12PipelineState* pso = D3d12Tools::compute_pso(kit.rig, signature, code, error);
            HRESULT result = S_OK;
            ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true, &result);
            ID3D12DescriptorHeap* samplers = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 1, true, &result);
            if (pso == nullptr || heap == nullptr || samplers == nullptr) {
                return fail_with(error, pso == nullptr ? error : "descriptor heaps: " + D3d12Tools::hr(result));
            }
            const UINT increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            D3D12_SHADER_RESOURCE_VIEW_DESC srv = srv_2d(DXGI_FORMAT_R8G8B8A8_UNORM, 1);
            kit.device()->CreateShaderResourceView(source_texture, &srv, D3d12Tools::cpu_at(heap, 2, increment));
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            kit.device()->CreateUnorderedAccessView(target, nullptr, &uav, D3d12Tools::cpu_at(heap, 3, increment));
            const D3D12_SAMPLER_DESC sampler = Binder::sampler_desc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
            kit.device()->CreateSampler(&sampler, samplers->GetCPUDescriptorHandleForHeapStart());
            if (!kit.run([&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                    ID3D12DescriptorHeap* heaps[2] = {heap, samplers};
                    list->SetDescriptorHeaps(2, heaps);
                    list->SetComputeRootSignature(signature);
                    list->SetPipelineState(pso);
                    const UINT indices[4] = {2, 3, 0, 0};
                    list->SetComputeRoot32BitConstants(0, 4, indices, 0);
                    list->Dispatch(8, 8, 1);
                }, error)) {
                return false;
            }
            rr::Image image;
            if (!kit.read_image(target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, DXGI_FORMAT_R8G8B8A8_UNORM, image, error)) {
                return false;
            }
            out.add("", std::move(image));
            return true;
        },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_SHADER_MODEL model{};
            model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(0x66);
            const HRESULT result = kit.device()->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof model);
            return SUCCEEDED(result) && model.HighestShaderModel >= 0x66 ? std::string() : std::string("shader model 6.6 dynamic resources");
        });
}

void register_immediate_writes() {
    Cases::single(rr::CaseSpec("cs", "write_buffer_immediate_values_and_ordering").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* buffer = uav_buffer(kit, 64, error);
        if (buffer == nullptr) {
            return false;
        }
        bool supported = true;
        const bool ok = kit.run([&](ID3D12GraphicsCommandList* list) {
            ID3D12GraphicsCommandList2* list2 = nullptr;
            if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList2), reinterpret_cast<void**>(&list2)))) {
                supported = false;
                return;
            }
            const D3D12_GPU_VIRTUAL_ADDRESS base = buffer->GetGPUVirtualAddress();
            D3D12_WRITEBUFFERIMMEDIATE_PARAMETER first[3] = {{base, 0x11111111u}, {base + 4, 0x22222222u}, {base + 8, 0x33333333u}};
            D3D12_WRITEBUFFERIMMEDIATE_MODE modes[3] = {D3D12_WRITEBUFFERIMMEDIATE_MODE_DEFAULT, D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_IN, D3D12_WRITEBUFFERIMMEDIATE_MODE_MARKER_OUT};
            list2->WriteBufferImmediate(3, first, modes);
            D3D12_WRITEBUFFERIMMEDIATE_PARAMETER second[2] = {{base + 8, 0xabcdef01u}, {base + 252, 0x7fu}};
            list2->WriteBufferImmediate(2, second, nullptr);
            list2->Release();
        }, error);
        if (!ok) {
            return false;
        }
        if (!supported) {
            return fail_with(error, "ID3D12GraphicsCommandList2 is not available");
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, buffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 64, words, error)) {
            return false;
        }
        out.add("", words_image(words, 4));
        return true;
    });
    Cases::single(rr::CaseSpec("cs", "atomic_copy_buffer_uint").tolerance(rr::Tolerance::exact()).tag("compute"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        ID3D12Resource* source = uav_buffer(kit, 64, error, noise_words(64, 5, 1000000).data());
        ID3D12Resource* destination = uav_buffer(kit, 64, error);
        if (source == nullptr || destination == nullptr) {
            return false;
        }
        bool supported = true;
        const bool ok = kit.run([&](ID3D12GraphicsCommandList* list) {
            ID3D12GraphicsCommandList1* list1 = nullptr;
            if (FAILED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void**>(&list1)))) {
                supported = false;
                return;
            }
            for (UINT k = 0; k < 16; ++k) {
                list1->AtomicCopyBufferUINT(destination, k * 16 + 4, source, (15 - k) * 16, 0, nullptr, nullptr);
            }
            list1->Release();
        }, error);
        if (!ok) {
            return false;
        }
        if (!supported) {
            return fail_with(error, "ID3D12GraphicsCommandList1 is not available");
        }
        std::vector<std::uint32_t> words;
        if (!D3d12Tools::read_words(kit.rig, destination, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 64, words, error)) {
            return false;
        }
        out.add("", words_image(words, 4));
        return true;
    });
}

const struct Registrar {
    Registrar() {
        register_uav_formats();
        register_buffers();
        register_texture_uavs();
        register_wave();
        register_shader_model_features();
        register_immediate_writes();
    }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
