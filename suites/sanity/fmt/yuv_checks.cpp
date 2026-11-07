#include "sanity/fmt/yuv_checks.hpp"

#include "sanity/fmt/kit.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/format_runner.hpp"
#include "support/pattern.hpp"

#include <d3d11_4.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

using Bytes = std::vector<std::uint8_t>;

class Limited {
public:
    explicit Limited(Problems& target) : problems_(target) {}

    void add(const std::string& text) {
        if (++count_ <= 30) {
            problems_.add(text);
        }
    }

private:
    Problems& problems_;
    int count_ = 0;
};

struct Planar {
    DXGI_FORMAT format;
    const char* name;
    unsigned bytes;
    DXGI_FORMAT luma;
    DXGI_FORMAT chroma;
    double scale;
};

const Planar kPlanar[] = {{DXGI_FORMAT_NV12, "NV12", 1, DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8G8_UNORM, 255.0},
                          {DXGI_FORMAT_P010, "P010", 2, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_R16G16_UNORM, 65535.0}};

struct Frame {
    unsigned width = 0;
    unsigned height = 0;
    Bytes luma;
    Bytes chroma;
};

Frame make_frame(const Planar& planar, unsigned width, unsigned height, std::uint64_t seed) {
    Frame frame;
    frame.width = width;
    frame.height = height;
    frame.luma.resize(static_cast<std::size_t>(width) * height * planar.bytes);
    frame.chroma.resize(static_cast<std::size_t>(width / 2) * (height / 2) * 2 * planar.bytes);
    Pattern::fill(seed, 0, frame.luma.data(), frame.luma.size());
    Pattern::fill(seed + 1, 0, frame.chroma.data(), frame.chroma.size());
    if (planar.bytes == 2) {
        for (Bytes* plane : {&frame.luma, &frame.chroma}) {
            for (std::size_t i = 0; i + 1 < plane->size(); i += 2) {
                std::uint16_t word = 0;
                std::memcpy(&word, plane->data() + i, 2);
                word = static_cast<std::uint16_t>((word & 0x03FFu) << 6);
                std::memcpy(plane->data() + i, &word, 2);
            }
        }
    }
    return frame;
}

double plane_value(const Planar& planar, const Bytes& plane, std::size_t index) {
    if (planar.bytes == 1) {
        return plane[index];
    }
    std::uint16_t word = 0;
    std::memcpy(&word, plane.data() + index * 2, 2);
    return word;
}

struct Rgb {
    double r;
    double g;
    double b;
};

Rgb decode601(double y, double u, double v) {
    const double c = 1.164383 * (y - 16.0);
    return {c + 1.596027 * (v - 128.0), c - 0.391762 * (u - 128.0) - 0.812968 * (v - 128.0), c + 2.017232 * (u - 128.0)};
}

Rgb decode709(double y, double u, double v) {
    const double c = 1.164383 * (y - 16.0);
    return {c + 1.792741 * (v - 128.0), c - 0.213249 * (u - 128.0) - 0.532909 * (v - 128.0), c + 2.112402 * (u - 128.0)};
}

bool compare_planes(const Planar& planar, const Frame& frame, const std::vector<std::uint32_t>& luma_words, const std::vector<std::uint32_t>& chroma_words,
                    unsigned chroma_stride_texels, Limited& out, const std::string& name) {
    const unsigned width = frame.width;
    const unsigned height = frame.height;
    int reported = 0;
    for (unsigned y = 0; y < height && reported < 3; ++y) {
        for (unsigned x = 0; x < width && reported < 3; ++x) {
            float luma = 0.0f;
            std::memcpy(&luma, &luma_words[(static_cast<std::size_t>(y) * width + x) * 4], 4);
            const double want_luma = plane_value(planar, frame.luma, static_cast<std::size_t>(y) * width + x) / planar.scale;
            const std::size_t chroma_index = (static_cast<std::size_t>(y / 2) * chroma_stride_texels + x / 2);
            float u = 0.0f;
            float v = 0.0f;
            std::memcpy(&u, &chroma_words[chroma_index * 4], 4);
            std::memcpy(&v, &chroma_words[chroma_index * 4 + 1], 4);
            const double want_u = plane_value(planar, frame.chroma, ((y / 2) * (width / 2) + x / 2) * 2) / planar.scale;
            const double want_v = plane_value(planar, frame.chroma, ((y / 2) * (width / 2) + x / 2) * 2 + 1) / planar.scale;
            if (std::fabs(luma - want_luma) > 2e-5 || std::fabs(u - want_u) > 2e-5 || std::fabs(v - want_v) > 2e-5) {
                out.add(name + " texel (" + std::to_string(x) + "," + std::to_string(y) + ") sampled Y/U/V " + std::to_string(luma) + "/" + std::to_string(u) + "/" +
                        std::to_string(v) + ", the frame holds " + std::to_string(want_luma) + "/" + std::to_string(want_u) + "/" + std::to_string(want_v));
                ++reported;
                continue;
            }
            const Rgb sampled = decode601(luma * 255.0, u * 255.0, v * 255.0);
            const Rgb source = decode601(want_luma * 255.0, want_u * 255.0, want_v * 255.0);
            if (std::fabs(sampled.r - source.r) > 1.0 || std::fabs(sampled.g - source.g) > 1.0 || std::fabs(sampled.b - source.b) > 1.0) {
                out.add(name + " texel (" + std::to_string(x) + "," + std::to_string(y) + ") decodes to a different colour than the CPU decode of the frame");
                ++reported;
            }
        }
    }
    return reported == 0;
}

bool d3d11_sample(D3d11Device& device, ID3D11Texture2D* texture, DXGI_FORMAT view_format, unsigned width, unsigned height, std::vector<std::uint32_t>& words,
                  std::string& error) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = view_format;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;
    Com<ID3D11ShaderResourceView> srv;
    HRESULT result = device.device->CreateShaderResourceView(texture, &desc, srv.put());
    if (FAILED(result)) {
        error = std::string("creating the ") + Kit::number_name(static_cast<int>(view_format)) + " view failed: " + D3d11Tools::hr(result);
        return false;
    }
    Com<ID3D11Buffer> output = D3d11Tools::buffer(device, width * height * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                                  D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, &result);
    Com<ID3D11UnorderedAccessView> uav = output ? D3d11Tools::raw_uav(device, output.get()) : Com<ID3D11UnorderedAccessView>();
    Com<ID3D11ComputeShader> shader = D3d11Tools::cs(device, FormatRunner::sample_source("float4", width, height), error);
    if (!output || !uav || !shader) {
        if (error.empty()) {
            error = "creating the output buffer failed: " + D3d11Tools::hr(result);
        }
        return false;
    }
    ID3D11ShaderResourceView* views[1] = {srv.get()};
    ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
    device.context->CSSetShader(shader.get(), nullptr, 0);
    device.context->CSSetShaderResources(0, 1, views);
    device.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    device.context->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    D3d11Tools::reset_state(device);
    words = D3d11Tools::read_words(device, output.get(), error);
    return words.size() == static_cast<std::size_t>(width) * height * 4;
}

void d3d11_planar(D3d11Device& device, Limited& out, int& tested) {
    for (const Planar& planar : kPlanar) {
        UINT bits = 0;
        if (FAILED(device.device->CheckFormatSupport(planar.format, &bits)) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0 || (bits & Kit::kSampleBits) == 0) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(64, 32, planar.format, D3D11_BIND_SHADER_RESOURCE);
            Com<ID3D11Texture2D> texture;
            if (SUCCEEDED(device.device->CreateTexture2D(&desc, nullptr, texture.put())) && (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
                out.add(std::string("D3D11 ") + planar.name + " reports no Texture2D support but a shader resource texture was created");
            }
            continue;
        }
        const unsigned sizes[][2] = {{46, 20}, {34, 18}, {64, 32}};
        for (const auto& size : sizes) {
            ++tested;
            const std::string name = std::string("D3D11 ") + planar.name + " " + std::to_string(size[0]) + "x" + std::to_string(size[1]);
            const Frame frame = make_frame(planar, size[0], size[1], 0xC0 + size[0]);
            Bytes data = frame.luma;
            data.insert(data.end(), frame.chroma.begin(), frame.chroma.end());
            D3D11_SUBRESOURCE_DATA initial{data.data(), size[0] * planar.bytes, 0};
            HRESULT result = S_OK;
            Com<ID3D11Texture2D> texture =
                D3d11Tools::texture(device, D3d11Tools::desc2d(size[0], size[1], planar.format, D3D11_BIND_SHADER_RESOURCE), &initial, &result);
            if (!texture) {
                out.add(name + " cannot be created: " + D3d11Tools::hr(result));
                continue;
            }
            std::vector<std::uint32_t> luma;
            std::vector<std::uint32_t> chroma;
            std::string error;
            if (!d3d11_sample(device, texture.get(), planar.luma, size[0], size[1], luma, error) ||
                !d3d11_sample(device, texture.get(), planar.chroma, size[0] / 2, size[1] / 2, chroma, error)) {
                out.add(name + " sampling failed: " + error);
                continue;
            }
            compare_planes(planar, frame, luma, chroma, size[0] / 2, out, name);
        }
        D3D11_TEXTURE2D_DESC odd = D3d11Tools::desc2d(35, 17, planar.format, D3D11_BIND_SHADER_RESOURCE);
        Com<ID3D11Texture2D> odd_texture;
        const HRESULT odd_result = device.device->CreateTexture2D(&odd, nullptr, odd_texture.put());
        if (FAILED(odd_result) && odd_result != E_INVALIDARG) {
            out.add(std::string("D3D11 ") + planar.name + " 35x17 was refused with " + D3d11Tools::hr(odd_result) + ", expected success or E_INVALIDARG");
        }
    }
    const struct {
        DXGI_FORMAT format;
        const char* name;
        unsigned view_divisor;
    } packed[] = {{DXGI_FORMAT_YUY2, "YUY2", 2}, {DXGI_FORMAT_AYUV, "AYUV", 1}};
    for (const auto& item : packed) {
        UINT bits = 0;
        if (FAILED(device.device->CheckFormatSupport(item.format, &bits)) || (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0 || (bits & Kit::kSampleBits) == 0) {
            continue;
        }
        ++tested;
        const std::string name = std::string("D3D11 ") + item.name;
        const unsigned width = 34;
        const unsigned height = 18;
        const unsigned view_width = width / item.view_divisor;
        Bytes data(static_cast<std::size_t>(view_width) * height * 4);
        Pattern::fill(0xD0, 0, data.data(), data.size());
        D3D11_SUBRESOURCE_DATA initial{data.data(), view_width * 4, 0};
        HRESULT result = S_OK;
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(width, height, item.format, D3D11_BIND_SHADER_RESOURCE), &initial, &result);
        std::vector<std::uint32_t> words;
        std::string error;
        if (!texture) {
            out.add(name + " cannot be created: " + D3d11Tools::hr(result));
            continue;
        }
        if (!d3d11_sample(device, texture.get(), DXGI_FORMAT_R8G8B8A8_UNORM, view_width, height, words, error)) {
            out.add(name + " sampling through an R8G8B8A8_UNORM view failed: " + error);
            continue;
        }
        int reported = 0;
        for (std::size_t t = 0; t < static_cast<std::size_t>(view_width) * height && reported < 3; ++t) {
            for (int c = 0; c < 4; ++c) {
                float got = 0.0f;
                std::memcpy(&got, &words[t * 4 + static_cast<std::size_t>(c)], 4);
                const double want = data[t * 4 + static_cast<std::size_t>(c)] / 255.0;
                if (std::fabs(got - want) > 2e-5) {
                    out.add(name + " texel " + std::to_string(t) + " byte " + std::to_string(c) + " samples " + std::to_string(got) + ", the frame holds " +
                            std::to_string(want));
                    ++reported;
                    break;
                }
            }
        }
    }
}

void d3d12_planar(D3d12Rig& rig, Limited& out, int& tested) {
    for (const Planar& planar : kPlanar) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
        support.Format = planar.format;
        const bool known = SUCCEEDED(rig.device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support)));
        const UINT bits = known ? static_cast<UINT>(support.Support1) : 0;
        if ((bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0 || (bits & Kit::kSampleBits) == 0) {
            HRESULT result = S_OK;
            if (D3d12Tools::texture(rig, D3d12Tools::texture_desc(64, 32, planar.format), D3D12_RESOURCE_STATE_COMMON, nullptr, &result) != nullptr &&
                (bits & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0) {
                out.add(std::string("D3D12 ") + planar.name + " reports no Texture2D support but a texture was created");
            }
            continue;
        }
        const unsigned sizes[][2] = {{46, 20}, {34, 18}, {64, 32}};
        for (const auto& size : sizes) {
            ++tested;
            const std::string name = std::string("D3D12 ") + planar.name + " " + std::to_string(size[0]) + "x" + std::to_string(size[1]);
            const Frame frame = make_frame(planar, size[0], size[1], 0xC0 + size[0]);
            HRESULT result = S_OK;
            ID3D12Resource* texture =
                D3d12Tools::texture(rig, D3d12Tools::texture_desc(size[0], size[1], planar.format), D3D12_RESOURCE_STATE_COPY_DEST, nullptr, &result);
            if (texture == nullptr) {
                out.add(name + " cannot be created: " + D3d12Tools::hr(result));
                continue;
            }
            std::string error;
            if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, frame.luma.data(), error) ||
                !D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 1, frame.chroma.data(), error)) {
                out.add(name + " plane upload failed: " + error);
                continue;
            }
            const std::string source =
                "Texture2D<float> ty : register(t0);\nTexture2D<float2> tc : register(t1);\nRWByteAddressBuffer o : register(u0);\n[numthreads(8, 8, 1)]\n"
                "void main(uint3 id : SV_DispatchThreadID) {\n  if (id.x < " + std::to_string(size[0]) + " && id.y < " + std::to_string(size[1]) +
                ") { float2 c = tc.Load(int3(id.xy / 2, 0)); o.Store4((id.y * " + std::to_string(size[0]) + " + id.x) * 16, asuint(float4(ty.Load(int3(id.xy, 0)), c.x, c.y, 1))); }\n}\n";
            D3d12Root root;
            root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
            root.uav(0);
            ID3D12RootSignature* signature = root.build(rig, error);
            ID3D12PipelineState* state = signature != nullptr ? D3d12Tools::compute_from(rig, signature, source, error) : nullptr;
            ID3D12DescriptorHeap* heap = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true);
            const UINT64 bytes = static_cast<UINT64>(size[0]) * size[1] * 16;
            ID3D12Resource* output = D3d12Tools::device_buffer(rig, bytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
            if (state == nullptr || heap == nullptr || output == nullptr) {
                out.add(name + " sampling resources failed: " + error);
                continue;
            }
            const UINT increment = rig.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for (UINT plane = 0; plane < 2; ++plane) {
                D3D12_SHADER_RESOURCE_VIEW_DESC view{};
                view.Format = plane == 0 ? planar.luma : planar.chroma;
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                view.Texture2D.MipLevels = 1;
                view.Texture2D.PlaneSlice = plane;
                rig.device()->CreateShaderResourceView(texture, &view, D3d12Tools::cpu_at(heap, plane, increment));
            }
            Bytes raw;
            const bool ran = rig.direct().run_wait(
                [&](ID3D12GraphicsCommandList* list) {
                    D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    ID3D12DescriptorHeap* heaps[] = {heap};
                    list->SetDescriptorHeaps(1, heaps);
                    list->SetComputeRootSignature(signature);
                    list->SetPipelineState(state);
                    list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
                    list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
                    list->Dispatch((size[0] + 7) / 8, (size[1] + 7) / 8, 1);
                },
                error);
            if (!ran || !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, bytes, raw, error)) {
                out.add(name + " sampling failed: " + error);
                continue;
            }
            std::vector<std::uint32_t> words(raw.size() / 4);
            std::memcpy(words.data(), raw.data(), words.size() * 4);
            compare_planes(planar, frame, words, words, size[0], out, name);
        }
    }
}

struct BarColor {
    int y;
    int u;
    int v;
};

const BarColor kBars[8] = {{16, 128, 128}, {235, 128, 128}, {81, 90, 240}, {145, 54, 34}, {41, 240, 110}, {210, 16, 146}, {170, 166, 16}, {106, 202, 222}};

}  // namespace

Outcome YuvChecks::yuv_nv12_p010(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    Limited out(problems);
    if (Kit::enabled("d3d11")) {
        AvAdapter adapter;
        D3d11Device device;
        std::string error;
        if (!Kit::open_d3d11(adapter, device, error)) {
            problems.add("D3D11: " + error);
        } else {
            int tested = 0;
            d3d11_planar(device, out, tested);
            context.measure("fmt.yuv.d3d11.cases", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("d3d12")) {
        D3d12Rig rig;
        std::string error;
        if (!Kit::open_d3d12(rig, error)) {
            problems.add("D3D12: " + error);
        } else {
            int tested = 0;
            d3d12_planar(rig, out, tested);
            context.measure("fmt.yuv.d3d12.cases", tested);
        }
        exercised = true;
    } else {
        skips.push_back("D3D12 excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

Outcome YuvChecks::yuv_video_processing(Context& context) {
    if (!Kit::enabled("d3d11")) {
        return Context::skip("D3D11 excluded by LSW_SUITES_SKIP_APIS");
    }
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT)) {
        return Context::skip("a device with video support cannot be created: " + error);
    }
    Com<ID3D11VideoDevice> video_device;
    device.device->QueryInterface(__uuidof(ID3D11VideoDevice), video_device.put_void());
    Com<ID3D11VideoContext> video_context;
    device.context->QueryInterface(__uuidof(ID3D11VideoContext), video_context.put_void());
    if (!video_device || !video_context) {
        return Context::skip("the device exposes no video interfaces");
    }
    const UINT width = 64;
    const UINT height = 16;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
    content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    content.InputFrameRate = {30, 1};
    content.InputWidth = width;
    content.InputHeight = height;
    content.OutputFrameRate = {30, 1};
    content.OutputWidth = width;
    content.OutputHeight = height;
    content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    Com<ID3D11VideoProcessorEnumerator> enumerator;
    HRESULT result = video_device->CreateVideoProcessorEnumerator(&content, enumerator.put());
    if (FAILED(result)) {
        return Context::skip("no video processor enumerator: " + D3d11Tools::hr(result));
    }
    UINT input_flags = 0;
    UINT output_flags = 0;
    enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &input_flags);
    const DXGI_FORMAT output_format = DXGI_FORMAT_B8G8R8A8_UNORM;
    enumerator->CheckVideoProcessorFormat(output_format, &output_flags);
    if ((input_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0 || (output_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
        return Context::skip("the video processor reports no NV12 input or B8G8R8A8 output support");
    }
    Com<ID3D11VideoProcessor> processor;
    result = video_device->CreateVideoProcessor(enumerator.get(), 0, processor.put());
    if (FAILED(result)) {
        return Outcome::fail("the enumerator reports NV12 to B8G8R8A8 support but CreateVideoProcessor failed: " + D3d11Tools::hr(result));
    }
    Bytes frame(static_cast<std::size_t>(width) * height * 3 / 2);
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            frame[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(kBars[(x / 16) + (y / 8) * 4].y);
        }
    }
    for (UINT y = 0; y < height / 2; ++y) {
        for (UINT x = 0; x < width / 2; ++x) {
            const BarColor& bar = kBars[(x / 8) + (y / 4) * 4];
            frame[static_cast<std::size_t>(width) * height + (static_cast<std::size_t>(y) * (width / 2) + x) * 2] = static_cast<std::uint8_t>(bar.u);
            frame[static_cast<std::size_t>(width) * height + (static_cast<std::size_t>(y) * (width / 2) + x) * 2 + 1] = static_cast<std::uint8_t>(bar.v);
        }
    }
    D3D11_SUBRESOURCE_DATA initial{frame.data(), width, 0};
    Com<ID3D11Texture2D> input = D3d11Tools::texture(device, D3d11Tools::desc2d(width, height, DXGI_FORMAT_NV12, D3D11_BIND_SHADER_RESOURCE), &initial, &result);
    Com<ID3D11Texture2D> output = D3d11Tools::target(device, width, height, output_format, 0);
    if (!input || !output) {
        return Outcome::fail("the NV12 input or the B8G8R8A8 output texture cannot be created: " + D3d11Tools::hr(result));
    }
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc{};
    input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    Com<ID3D11VideoProcessorInputView> input_view;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc{};
    output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    Com<ID3D11VideoProcessorOutputView> output_view;
    if (FAILED(result = video_device->CreateVideoProcessorInputView(input.get(), enumerator.get(), &input_desc, input_view.put())) ||
        FAILED(result = video_device->CreateVideoProcessorOutputView(output.get(), enumerator.get(), &output_desc, output_view.put()))) {
        return Outcome::fail("creating the video processor views failed: " + D3d11Tools::hr(result));
    }
    Problems problems;
    for (int matrix = 0; matrix < 2; ++matrix) {
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE stream_space{};
        if (matrix == 1) {
            stream_space.YCbCr_Matrix = 1;
        }
        stream_space.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_space{};
        output_space.RGB_Range = 0;
        output_space.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        video_context->VideoProcessorSetStreamFrameFormat(processor.get(), 0, D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
        video_context->VideoProcessorSetStreamColorSpace(processor.get(), 0, &stream_space);
        video_context->VideoProcessorSetOutputColorSpace(processor.get(), &output_space);
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input_view.get();
        result = video_context->VideoProcessorBlt(processor.get(), output_view.get(), 0, 1, &stream);
        const std::string label = matrix == 0 ? "BT.601" : "BT.709";
        if (FAILED(result)) {
            problems.add(label + " VideoProcessorBlt failed: " + D3d11Tools::hr(result));
            continue;
        }
        TexelImage image;
        if (!D3d11Tools::read_texture(device, output.get(), 0, image, error)) {
            problems.add(label + " readback failed: " + error);
            continue;
        }
        int reported = 0;
        for (int bar = 0; bar < 8 && reported < 4; ++bar) {
            const UINT x = static_cast<UINT>(bar % 4) * 16 + 8;
            const UINT y = static_cast<UINT>(bar / 4) * 8 + 4;
            const Rgb want = matrix == 0 ? decode601(kBars[bar].y, kBars[bar].u, kBars[bar].v) : decode709(kBars[bar].y, kBars[bar].u, kBars[bar].v);
            const std::uint32_t pixel = image.pixel(x, y);
            const double got[3] = {static_cast<double>((pixel >> 16) & 255), static_cast<double>((pixel >> 8) & 255), static_cast<double>(pixel & 255)};
            const double expected[3] = {std::clamp(want.r, 0.0, 255.0), std::clamp(want.g, 0.0, 255.0), std::clamp(want.b, 0.0, 255.0)};
            for (int c = 0; c < 3; ++c) {
                if (std::fabs(got[c] - expected[c]) > 6.0) {
                    problems.add(label + " bar " + std::to_string(bar) + " converted to channel " + std::to_string(c) + " = " + std::to_string(got[c]) +
                                 ", the CPU conversion gives " + std::to_string(expected[c]));
                    ++reported;
                    break;
                }
            }
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::fmt
