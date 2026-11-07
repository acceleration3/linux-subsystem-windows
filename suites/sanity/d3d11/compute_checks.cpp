#include "sanity/d3d11/compute_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/format_codec.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cmath>

namespace lsw::suites::sanity::d3d11 {

namespace {

constexpr UINT kRawMisc = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

Com<ID3D11Buffer> raw_buffer(D3d11Device& d, UINT words, const void* initial = nullptr) {
    return D3d11Tools::buffer(d, words * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, 0, kRawMisc, 0, initial);
}

void dispatch(D3d11Device& d, ID3D11ComputeShader* cs, std::initializer_list<ID3D11UnorderedAccessView*> uavs, UINT groups,
              const UINT* initial_counts = nullptr) {
    std::vector<ID3D11UnorderedAccessView*> list(uavs);
    d.context->CSSetShader(cs, nullptr, 0);
    d.context->CSSetUnorderedAccessViews(0, static_cast<UINT>(list.size()), list.data(), initial_counts);
    d.context->Dispatch(groups, 1, 1);
}

}  // namespace

Outcome ComputeChecks::uav_basic(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("compute shader 5 needs feature level 11_0; downlevel compute is covered by cs_on_downlevel");
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    const char* source =
        "#if MODE == 0\nRWByteAddressBuffer o : register(u0);\n#define STORE(i, v) o.Store((i) * 4, v)\n"
        "#elif MODE == 1\nRWStructuredBuffer<uint> o : register(u0);\n#define STORE(i, v) o[i] = v\n"
        "#else\nRWBuffer<uint> o : register(u0);\n#define STORE(i, v) o[i] = v\n#endif\n"
        "groupshared uint s[N];\n"
        "[numthreads(N, 1, 1)] void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID, uint3 id : SV_DispatchThreadID) {\n"
        "  s[t.x] = id.x * 7 + 3; GroupMemoryBarrierWithGroupSync();\n"
        "  uint v = s[N - 1 - t.x]; STORE(id.x, v ^ g.x);\n"
        "}\n";
    constexpr UINT kGroups = 17;
    int cases = 0;
    for (UINT n : {1u, 3u, 32u, 64u, 128u, 256u, 1024u}) {
        for (int mode = 0; mode < 3; ++mode) {
            const std::string n_text = std::to_string(n);
            const std::string mode_text = std::to_string(mode);
            const D3D_SHADER_MACRO macros[] = {{"N", n_text.c_str()}, {"MODE", mode_text.c_str()}, {nullptr, nullptr}};
            Com<ID3D11ComputeShader> cs = D3d11Tools::cs(device, source, error, "main", macros);
            if (!cs) {
                problems.add("N " + n_text + " mode " + mode_text + ": " + error);
                continue;
            }
            const UINT total = n * kGroups;
            Com<ID3D11Buffer> buffer;
            Com<ID3D11UnorderedAccessView> uav;
            if (mode == 0) {
                buffer = raw_buffer(device, total);
                uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
            } else if (mode == 1) {
                buffer = D3d11Tools::buffer(device, total * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
                uav = buffer ? D3d11Tools::structured_uav(device, buffer.get(), total) : Com<ID3D11UnorderedAccessView>();
            } else {
                buffer = D3d11Tools::buffer(device, total * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0, nullptr);
                D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
                desc.Format = DXGI_FORMAT_R32_UINT;
                desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                desc.Buffer.NumElements = total;
                if (buffer) {
                    device.device->CreateUnorderedAccessView(buffer.get(), &desc, uav.put());
                }
            }
            if (!uav) {
                problems.add("N " + n_text + " mode " + mode_text + ": the unordered access view cannot be created");
                continue;
            }
            for (int indirect = 0; indirect < 2; ++indirect) {
                const std::vector<std::uint32_t> poison(total, 0xCDCDCDCD);
                ctx->UpdateSubresource(buffer.get(), 0, nullptr, poison.data(), 0, 0);
                if (indirect == 0) {
                    dispatch(device, cs.get(), {uav.get()}, kGroups);
                } else {
                    const UINT args[3] = {kGroups, 1, 1};
                    Com<ID3D11Buffer> arg_buffer = D3d11Tools::buffer(device, 12, D3D11_USAGE_DEFAULT, 0, 0, D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS, 0, args);
                    ctx->CSSetShader(cs.get(), nullptr, 0);
                    ID3D11UnorderedAccessView* list[1] = {uav.get()};
                    ctx->CSSetUnorderedAccessViews(0, 1, list, nullptr);
                    ctx->DispatchIndirect(arg_buffer.get(), 0);
                }
                const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffer.get(), error);
                if (words.size() != total) {
                    problems.add("readback: " + error);
                    continue;
                }
                for (UINT i = 0; i < total; ++i) {
                    const UINT g = i / n;
                    const UINT t = i % n;
                    const std::uint32_t want = ((g * n + (n - 1 - t)) * 7 + 3) ^ g;
                    if (words[i] != want) {
                        problems.add("N " + n_text + " mode " + mode_text + (indirect != 0 ? " indirect" : " direct") + ": element " + std::to_string(i) + " is " + std::to_string(words[i]) + ", expected " + std::to_string(want));
                        break;
                    }
                }
                ++cases;
            }
        }
    }
    context.measure("cases", cases);
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome ComputeChecks::atomics(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("raw buffer atomics in compute need feature level 11_0");
    }
    Problems problems;
    constexpr UINT kThreads = 65536;
    const char* source =
        "RWByteAddressBuffer o : register(u0); RWByteAddressBuffer olds : register(u1);\n"
        "[numthreads(256, 1, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  uint i = id.x; uint d;\n"
        "  o.InterlockedAdd(0, 1, d); o.InterlockedMin(4, i + 5, d); o.InterlockedMax(8, i + 5, d);\n"
        "  o.InterlockedAnd(12, ~(1u << (i % 32)), d); o.InterlockedOr(16, 1u << (i % 32), d); o.InterlockedXor(20, i + 1, d);\n"
        "  uint old; o.InterlockedExchange(24, i + 1, old); olds.Store(i * 4, old);\n"
        "  [loop] for (uint k = 0; k < 1000000; ++k) { uint cur = o.Load(28); uint prev; o.InterlockedCompareExchange(28, cur, cur + 1, prev); if (prev == cur) break; }\n"
        "}\n";
    Com<ID3D11ComputeShader> cs = D3d11Tools::cs(device, source, error);
    if (!cs) {
        return Outcome::fail(error);
    }
    const std::uint32_t initial[8] = {0, 0xFFFFFFFFu, 0, 0xFFFFFFFFu, 0, 0, 0, 0};
    Com<ID3D11Buffer> out = raw_buffer(device, 8, initial);
    Com<ID3D11Buffer> olds = raw_buffer(device, kThreads);
    Com<ID3D11UnorderedAccessView> out_uav = out ? D3d11Tools::raw_uav(device, out.get()) : Com<ID3D11UnorderedAccessView>();
    Com<ID3D11UnorderedAccessView> olds_uav = olds ? D3d11Tools::raw_uav(device, olds.get()) : Com<ID3D11UnorderedAccessView>();
    if (!out_uav || !olds_uav) {
        return Outcome::fail("raw unordered access views cannot be created");
    }
    dispatch(device, cs.get(), {out_uav.get(), olds_uav.get()}, kThreads / 256);
    const std::vector<std::uint32_t> result = D3d11Tools::read_words(device, out.get(), error);
    const std::vector<std::uint32_t> previous = D3d11Tools::read_words(device, olds.get(), error);
    if (result.size() != 8 || previous.size() != kThreads) {
        return Outcome::fail("readback: " + error);
    }
    std::uint32_t xor_all = 0;
    for (UINT i = 0; i < kThreads; ++i) {
        xor_all ^= i + 1;
    }
    auto expect = [&](const char* name, std::uint32_t got, std::uint32_t want) {
        if (got != want) {
            problems.add(std::string(name) + " ended at " + std::to_string(got) + ", expected " + std::to_string(want));
        }
    };
    expect("InterlockedAdd", result[0], kThreads);
    expect("InterlockedMin", result[1], 5);
    expect("InterlockedMax", result[2], kThreads - 1 + 5);
    expect("InterlockedAnd", result[3], 0);
    expect("InterlockedOr", result[4], 0xFFFFFFFFu);
    expect("InterlockedXor", result[5], xor_all);
    expect("InterlockedCompareExchange increments", result[7], kThreads);
    std::vector<std::uint32_t> seen(kThreads + 1, 0);
    bool in_range = true;
    for (std::uint32_t v : previous) {
        if (v > kThreads) {
            in_range = false;
        } else {
            ++seen[v];
        }
    }
    if (result[6] <= kThreads) {
        ++seen[result[6]];
    } else {
        in_range = false;
    }
    bool permutation = in_range;
    for (std::uint32_t count : seen) {
        permutation = permutation && count == 1;
    }
    if (!permutation) {
        problems.add("InterlockedExchange did not hand every value to exactly one thread");
    }
    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support{DXGI_FORMAT_R32_UINT, 0};
    device.device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &support, sizeof(support));
    if ((support.OutFormatSupport2 & (D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE)) ==
        (D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_ADD | D3D11_FORMAT_SUPPORT2_UAV_ATOMIC_EXCHANGE)) {
        Com<ID3D11ComputeShader> typed = D3d11Tools::cs(device,
            "RWBuffer<uint> t : register(u0);\n[numthreads(256,1,1)] void main(uint3 id : SV_DispatchThreadID) { uint d; InterlockedAdd(t[0], 2, d); InterlockedExchange(t[1], id.x, d); }", error);
        Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, 0, 0, nullptr);
        D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        desc.Buffer.NumElements = 4;
        Com<ID3D11UnorderedAccessView> view;
        if (buffer) {
            device.device->CreateUnorderedAccessView(buffer.get(), &desc, view.put());
        }
        if (!typed || !view) {
            problems.add("typed UAV atomics are reported but the shader or view fails: " + error);
        } else {
            const std::uint32_t zeros[4] = {};
            device.context->UpdateSubresource(buffer.get(), 0, nullptr, zeros, 0, 0);
            dispatch(device, typed.get(), {view.get()}, kThreads / 256);
            const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, buffer.get(), error);
            if (words.size() < 2 || words[0] != kThreads * 2) {
                problems.add("typed UAV InterlockedAdd ended at " + std::to_string(words.empty() ? 0 : words[0]));
            }
        }
    } else {
        context.note("typed UAV atomics are not reported for R32_UINT; that part is not tested");
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome ComputeChecks::append_consume(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("append and consume buffers need feature level 11_0");
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    constexpr UINT kN = 4096;
    constexpr UINT kCapacity = 8192;
    Com<ID3D11ComputeShader> append = D3d11Tools::cs(device,
        "AppendStructuredBuffer<uint> a : register(u0);\n[numthreads(256,1,1)] void main(uint3 id : SV_DispatchThreadID) { a.Append(id.x + 1); }", error);
    Com<ID3D11ComputeShader> consume = D3d11Tools::cs(device,
        "ConsumeStructuredBuffer<uint> c : register(u0); RWByteAddressBuffer seen : register(u1);\n"
        "[numthreads(256,1,1)] void main(uint3 id : SV_DispatchThreadID) { uint v = c.Consume(); uint d; seen.InterlockedAdd((v - 1) * 4, 1, d); }", error);
    Com<ID3D11Buffer> list = D3d11Tools::buffer(device, kCapacity * 4, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
    Com<ID3D11Buffer> count = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, 0, 0, 0, 0, nullptr);
    Com<ID3D11Buffer> seen = raw_buffer(device, kCapacity);
    if (!append || !consume || !list || !count || !seen) {
        return Outcome::fail("scene objects: " + error);
    }
    Com<ID3D11UnorderedAccessView> list_uav = D3d11Tools::structured_uav(device, list.get(), kCapacity, D3D11_BUFFER_UAV_FLAG_APPEND);
    Com<ID3D11UnorderedAccessView> seen_uav = D3d11Tools::raw_uav(device, seen.get());
    if (!list_uav || !seen_uav) {
        return Outcome::fail("append views cannot be created");
    }
    auto counter = [&]() -> long long {
        ctx->CopyStructureCount(count.get(), 0, list_uav.get());
        const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, count.get(), error);
        return words.empty() ? -1 : words[0];
    };
    auto histogram_all_ones = [&](UINT from, UINT to) {
        const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, seen.get(), error);
        for (UINT i = from; i < to; ++i) {
            if (words.size() <= i || words[i] != 1) {
                return false;
            }
        }
        return true;
    };
    const UINT keep = 0xFFFFFFFF;
    const UINT zero = 0;
    ID3D11UnorderedAccessView* only[1] = {list_uav.get()};
    ctx->CSSetUnorderedAccessViews(0, 1, only, &zero);
    ctx->CSSetShader(append.get(), nullptr, 0);
    ctx->Dispatch(kN / 256, 1, 1);
    if (counter() != kN) {
        problems.add("after " + std::to_string(kN) + " appends the counter is " + std::to_string(counter()));
    }
    const std::vector<std::uint32_t> zeros(kCapacity, 0);
    ctx->UpdateSubresource(seen.get(), 0, nullptr, zeros.data(), 0, 0);
    ID3D11UnorderedAccessView* both[2] = {list_uav.get(), seen_uav.get()};
    ctx->CSSetShader(consume.get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 2, both, &keep);
    ctx->Dispatch(kN / 256, 1, 1);
    if (!histogram_all_ones(0, kN)) {
        problems.add("consuming after appending did not see every element exactly once");
    }
    if (counter() != 0) {
        problems.add("after consuming everything the counter is " + std::to_string(counter()));
    }
    std::vector<std::uint32_t> prefilled(kCapacity, 0);
    for (UINT i = 0; i < 100; ++i) {
        prefilled[i] = i + 1;
    }
    ctx->UpdateSubresource(list.get(), 0, nullptr, prefilled.data(), 0, 0);
    ctx->UpdateSubresource(seen.get(), 0, nullptr, zeros.data(), 0, 0);
    const UINT hundred = 100;
    ctx->CSSetUnorderedAccessViews(0, 2, both, &hundred);
    ctx->Dispatch(1, 1, 1);
    const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, seen.get(), error);
    UINT consumed = 0;
    for (UINT i = 0; i < 256 && i < words.size(); ++i) {
        consumed += words[i];
    }
    if (consumed < 100 || !histogram_all_ones(0, 100)) {
        problems.add("an initial count of 100 did not make 100 elements consumable exactly once");
    }
    const UINT fifty = 50;
    ctx->CSSetUnorderedAccessViews(0, 1, only, &fifty);
    ctx->CSSetShader(append.get(), nullptr, 0);
    ctx->Dispatch(kN / 256, 1, 1);
    if (counter() != 50 + kN) {
        problems.add("with an initial count of 50 the counter after appends is " + std::to_string(counter()));
    }
    std::vector<std::uint8_t> bytes;
    if (D3d11Tools::read_buffer(device, list.get(), bytes, error)) {
        std::vector<int> found(kN + 1, 0);
        for (UINT i = 0; i < kN; ++i) {
            std::uint32_t v = 0;
            std::memcpy(&v, bytes.data() + (50 + i) * 4, 4);
            if (v >= 1 && v <= kN) {
                ++found[v];
            }
        }
        for (UINT v = 1; v <= kN; ++v) {
            if (found[v] != 1) {
                problems.add("with an initial count of 50 appended value " + std::to_string(v) + " is stored " + std::to_string(found[v]) + " times");
                break;
            }
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome ComputeChecks::typed_uav_load(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("typed UAV access needs feature level 11_0");
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    const DXGI_FORMAT candidates[] = {DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT, DXGI_FORMAT_R32G32B32A32_FLOAT,
                                      DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UNORM,
                                      DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SNORM,
                                      DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R10G10B10A2_UNORM,
                                      DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R8G8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM};
    int tested = 0;
    for (DXGI_FORMAT format : candidates) {
        D3D11_FEATURE_DATA_FORMAT_SUPPORT2 s2{format, 0};
        device.device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &s2, sizeof(s2));
        UINT s1 = 0;
        device.device->CheckFormatSupport(format, &s1);
        const bool uav_bit = (s1 & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0;
        Com<ID3D11Texture2D> tex = D3d11Tools::texture(device, D3d11Tools::desc2d(8, 8, format, D3D11_BIND_UNORDERED_ACCESS));
        Com<ID3D11UnorderedAccessView> view = tex ? D3d11Tools::uav(device, tex.get()) : Com<ID3D11UnorderedAccessView>();
        if (static_cast<bool>(view) != uav_bit) {
            problems.add("format " + std::to_string(format) + ": the typed UAV bit is " + (uav_bit ? "set" : "clear") + " but UAV creation " + (view ? "succeeds" : "fails"));
            continue;
        }
        if ((s2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) == 0 || (s2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) == 0 || !view) {
            continue;
        }
        const FormatSpec spec = FormatCodec::spec(format);
        if (!spec.known) {
            continue;
        }
        ++tested;
        const bool is_float = spec.kind == FormatKind::Float || spec.kind == FormatKind::Unorm || spec.kind == FormatKind::Snorm || spec.kind == FormatKind::Srgb;
        const char* type = is_float ? "float4" : (spec.kind == FormatKind::Uint ? "uint4" : "int4");
        const std::string value = is_float ? "float4(0.25, 0.5, 0.75, 1.0)" : (spec.kind == FormatKind::Uint ? "uint4(1, 2, 3, 1)" : "int4(-1, 2, -3, 1)");
        const std::string element = std::string(is_float ? "unorm float" : "") + (spec.kind == FormatKind::Uint ? "uint" : (spec.kind == FormatKind::Sint ? "int" : ""));
        static_cast<void>(element);
        const std::string decl = std::string("RWTexture2D<") + type + "> t : register(u0); RWByteAddressBuffer o : register(u1);\n";
        Com<ID3D11ComputeShader> store = D3d11Tools::cs(device, decl + "[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { t[id.xy] = " + value + "; }", error);
        Com<ID3D11ComputeShader> load = D3d11Tools::cs(device, decl +
            "[numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { " + type + " v = t[id.xy]; uint base = (id.y * 8 + id.x) * 16; "
            "o.Store4(base, asuint(v)); }", error);
        Com<ID3D11Buffer> out = raw_buffer(device, 8 * 8 * 4);
        Com<ID3D11UnorderedAccessView> out_uav = out ? D3d11Tools::raw_uav(device, out.get()) : Com<ID3D11UnorderedAccessView>();
        if (!store || !load || !out_uav) {
            problems.add("format " + std::to_string(format) + ": " + error);
            continue;
        }
        dispatch(device, store.get(), {view.get()}, 1);
        dispatch(device, load.get(), {view.get(), out_uav.get()}, 1);
        TexelImage image;
        const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, out.get(), error);
        if (!D3d11Tools::read_texture(device, tex.get(), 0, image, error) || words.size() != 8 * 8 * 4) {
            problems.add("format " + std::to_string(format) + ": " + error);
            continue;
        }
        const std::size_t texel = 9;
        double decoded[4];
        FormatCodec::decode(spec, image.data.data() + texel * spec.bytes, decoded);
        for (int i = 0; i < spec.count; ++i) {
            const int channel = spec.field[i].channel;
            std::uint32_t bits = words[texel * 4 + static_cast<std::size_t>(channel)];
            double loaded = 0;
            if (is_float) {
                float f = 0;
                std::memcpy(&f, &bits, 4);
                loaded = f;
            } else if (spec.kind == FormatKind::Sint) {
                loaded = static_cast<std::int32_t>(bits);
            } else {
                loaded = bits;
            }
            if (std::fabs(loaded - decoded[channel]) > FormatCodec::tolerance(spec, i, decoded[channel]) * 0.5 + 1e-9) {
                problems.add("format " + std::to_string(format) + " channel " + std::to_string(channel) + ": a typed load returned " + std::to_string(loaded) + " but the stored texel decodes to " + std::to_string(decoded[channel]));
                break;
            }
        }
    }
    context.measure("formats_with_typed_load", tested);
    if (tested == 0) {
        problems.add("no tested format reports typed UAV load and store");
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome ComputeChecks::barriers_sharing(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("group shared memory kernels need feature level 11_0");
    }
    Problems problems;
    const char* reduce_source =
        "ByteAddressBuffer in_ : register(t0); RWByteAddressBuffer sums : register(u0); cbuffer C : register(b0) { uint n; uint3 pad; }\n"
        "groupshared uint s[256];\n"
        "[numthreads(256,1,1)] void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID, uint3 id : SV_DispatchThreadID) {\n"
        "  s[t.x] = id.x < n ? in_.Load(id.x * 4) : 0; GroupMemoryBarrierWithGroupSync();\n"
        "  for (uint k = 128; k > 0; k >>= 1) { if (t.x < k) s[t.x] += s[t.x + k]; GroupMemoryBarrierWithGroupSync(); }\n"
        "  if (t.x == 0) sums.Store(g.x * 4, s[0]);\n"
        "}\n";
    const char* scan_source =
        "ByteAddressBuffer in_ : register(t0); RWByteAddressBuffer out_ : register(u0); RWByteAddressBuffer sums : register(u1); cbuffer C : register(b0) { uint n; uint3 pad; }\n"
        "groupshared uint s[256];\n"
        "[numthreads(256,1,1)] void main(uint3 g : SV_GroupID, uint3 t : SV_GroupThreadID, uint3 id : SV_DispatchThreadID) {\n"
        "  s[t.x] = id.x < n ? in_.Load(id.x * 4) : 0; GroupMemoryBarrierWithGroupSync();\n"
        "  for (uint k = 1; k < 256; k <<= 1) { uint add = t.x >= k ? s[t.x - k] : 0; GroupMemoryBarrierWithGroupSync(); s[t.x] += add; GroupMemoryBarrierWithGroupSync(); }\n"
        "  if (id.x < n) out_.Store(id.x * 4, s[t.x]);\n"
        "  if (t.x == 255) sums.Store(g.x * 4, s[255]);\n"
        "}\n";
    const char* offset_source =
        "RWByteAddressBuffer out_ : register(u0); ByteAddressBuffer offsets : register(t0); cbuffer C : register(b0) { uint n; uint3 pad; }\n"
        "[numthreads(256,1,1)] void main(uint3 g : SV_GroupID, uint3 id : SV_DispatchThreadID) { if (id.x < n) out_.Store(id.x * 4, out_.Load(id.x * 4) + offsets.Load(g.x * 4)); }\n";
    const char* histogram_source =
        "ByteAddressBuffer in_ : register(t0); RWByteAddressBuffer bins : register(u0); cbuffer C : register(b0) { uint n; uint3 pad; }\n"
        "groupshared uint h[256];\n"
        "[numthreads(256,1,1)] void main(uint3 t : SV_GroupThreadID, uint3 id : SV_DispatchThreadID) {\n"
        "  h[t.x] = 0; GroupMemoryBarrierWithGroupSync();\n"
        "  if (id.x < n) { uint d; InterlockedAdd(h[in_.Load(id.x * 4) & 255], 1, d); }\n"
        "  GroupMemoryBarrierWithGroupSync();\n"
        "  uint d2; bins.InterlockedAdd(t.x * 4, h[t.x], d2);\n"
        "}\n";
    Com<ID3D11ComputeShader> reduce = D3d11Tools::cs(device, reduce_source, error);
    Com<ID3D11ComputeShader> scan = D3d11Tools::cs(device, scan_source, error);
    Com<ID3D11ComputeShader> offset = D3d11Tools::cs(device, offset_source, error);
    Com<ID3D11ComputeShader> histogram = D3d11Tools::cs(device, histogram_source, error);
    if (!reduce || !scan || !offset || !histogram) {
        return Outcome::fail("kernels: " + error);
    }
    ID3D11DeviceContext* ctx = device.context;
    for (UINT n : {1000u, 4099u, 70001u}) {
        std::vector<std::uint32_t> data(n);
        for (UINT i = 0; i < n; ++i) {
            data[i] = static_cast<std::uint32_t>(Pattern::pixel(33, i, n)) & 0xFFFF;
        }
        const UINT groups = (n + 255) / 256;
        Com<ID3D11Buffer> input = raw_buffer(device, n, data.data());
        Com<ID3D11Buffer> sums = raw_buffer(device, groups);
        Com<ID3D11Buffer> output = raw_buffer(device, n);
        Com<ID3D11Buffer> offsets = raw_buffer(device, groups);
        Com<ID3D11Buffer> bins = raw_buffer(device, 256);
        const UINT params[4] = {n, 0, 0, 0};
        Com<ID3D11Buffer> constants = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, params);
        Com<ID3D11ShaderResourceView> input_srv = D3d11Tools::raw_srv(device, input.get());
        Com<ID3D11ShaderResourceView> offsets_srv = D3d11Tools::raw_srv(device, offsets.get());
        Com<ID3D11UnorderedAccessView> sums_uav = D3d11Tools::raw_uav(device, sums.get());
        Com<ID3D11UnorderedAccessView> output_uav = D3d11Tools::raw_uav(device, output.get());
        Com<ID3D11UnorderedAccessView> bins_uav = D3d11Tools::raw_uav(device, bins.get());
        if (!input_srv || !sums_uav || !output_uav || !bins_uav || !offsets_srv || !constants) {
            return Outcome::fail("buffers or views cannot be created");
        }
        ID3D11Buffer* cb = constants.get();
        ctx->CSSetConstantBuffers(0, 1, &cb);
        ID3D11ShaderResourceView* in_view[1] = {input_srv.get()};
        ctx->CSSetShaderResources(0, 1, in_view);
        std::uint64_t total = 0;
        for (std::uint32_t v : data) {
            total += v;
        }
        dispatch(device, reduce.get(), {sums_uav.get()}, groups);
        std::vector<std::uint32_t> group_sums = D3d11Tools::read_words(device, sums.get(), error);
        std::uint64_t reduced = 0;
        for (std::uint32_t v : group_sums) {
            reduced += v;
        }
        if (group_sums.size() != groups || reduced != total) {
            problems.add("n " + std::to_string(n) + ": the reduction is " + std::to_string(reduced) + ", expected " + std::to_string(total));
        }
        dispatch(device, scan.get(), {output_uav.get(), sums_uav.get()}, groups);
        group_sums = D3d11Tools::read_words(device, sums.get(), error);
        std::vector<std::uint32_t> group_offsets(groups, 0);
        for (UINT g = 1; g < groups && g < group_sums.size(); ++g) {
            group_offsets[g] = group_offsets[g - 1] + group_sums[g - 1];
        }
        ctx->UpdateSubresource(offsets.get(), 0, nullptr, group_offsets.data(), 0, 0);
        ID3D11ShaderResourceView* offset_view[1] = {offsets_srv.get()};
        ctx->CSSetShaderResources(0, 1, offset_view);
        dispatch(device, offset.get(), {output_uav.get()}, groups);
        const std::vector<std::uint32_t> scanned = D3d11Tools::read_words(device, output.get(), error);
        std::uint32_t running = 0;
        for (UINT i = 0; i < n && scanned.size() == n; ++i) {
            running += data[i];
            if (scanned[i] != running) {
                problems.add("n " + std::to_string(n) + ": the prefix sum at " + std::to_string(i) + " is " + std::to_string(scanned[i]) + ", expected " + std::to_string(running));
                break;
            }
        }
        ctx->CSSetShaderResources(0, 1, in_view);
        dispatch(device, histogram.get(), {bins_uav.get()}, groups);
        const std::vector<std::uint32_t> counted = D3d11Tools::read_words(device, bins.get(), error);
        std::vector<std::uint32_t> want(256, 0);
        for (std::uint32_t v : data) {
            ++want[v & 255];
        }
        if (counted != want) {
            problems.add("n " + std::to_string(n) + ": the histogram differs from the CPU result");
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome ComputeChecks::cs_on_downlevel(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3d11Device device;
    if (!device.create_ex(adapter.luid, levels, 2, 0, error)) {
        return Context::skip("the adapter cannot create a feature level 10 device (" + D3d11Tools::hr(device.result) + "), which the API reports as unsupported");
    }
    Problems problems;
    D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS options{};
    if (FAILED(device.device->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS, &options, sizeof(options)))) {
        return Outcome::fail("CheckFeatureSupport(D3D10_X_HARDWARE_OPTIONS) fails on a feature level 10 device");
    }
    const bool available = options.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x != FALSE;
    const char* source = "RWByteAddressBuffer o : register(u0);\n[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) { o.Store(id.x * 4, id.x * 3 + 1); }";
    Com<ID3D11ComputeShader> cs = D3d11Tools::cs(device, source, error);
    if (available && !cs) {
        problems.add("the option reports compute shaders on this device but creating one fails: " + error);
    }
    if (!available && cs) {
        problems.add("the option reports no compute shaders but creating one succeeds");
    }
    if (available && cs) {
        Com<ID3D11Buffer> out = raw_buffer(device, 128);
        Com<ID3D11UnorderedAccessView> uav = out ? D3d11Tools::raw_uav(device, out.get()) : Com<ID3D11UnorderedAccessView>();
        if (!uav) {
            problems.add("a raw UAV cannot be created for the downlevel compute shader");
        } else {
            dispatch(device, cs.get(), {uav.get()}, 2);
            const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, out.get(), error);
            for (UINT i = 0; i < 128 && words.size() == 128; ++i) {
                if (words[i] != i * 3 + 1) {
                    problems.add("downlevel compute wrote " + std::to_string(words[i]) + " at " + std::to_string(i));
                    break;
                }
            }
        }
    }
    D3d11Device modern;
    if (modern.create(adapter.luid, error) && modern.level >= D3D_FEATURE_LEVEL_11_0) {
        std::vector<std::uint8_t> code;
        if (D3d11Tools::compile(source, "main", "cs_4_0", code, error)) {
            Com<ID3D11ComputeShader> shader;
            if (FAILED(modern.device->CreateComputeShader(code.data(), code.size(), nullptr, shader.put()))) {
                problems.add("a cs_4_0 shader cannot be created on a feature level 11 device");
            }
        }
    }
    context.measure("downlevel_compute", available ? 1 : 0);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
