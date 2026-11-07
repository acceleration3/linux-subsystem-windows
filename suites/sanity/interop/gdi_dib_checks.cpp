#include "sanity/interop/gdi_dib_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/api_policy.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_frame_draw.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d12_rig.hpp"
#include "support/d3d12_root.hpp"
#include "support/d3d12_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/format_codec.hpp"
#include "support/gdi_dib.hpp"
#include "support/gl_fetch.hpp"
#include "support/gl_rig.hpp"
#include "support/vulkan_image.hpp"
#include "support/vulkan_image_io.hpp"
#include "support/window_presenter.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

namespace lsw::suites::sanity::interop {

namespace {

constexpr UINT kEdge = 64;

struct Texels {
    std::vector<std::uint8_t> bytes;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

bool via_d3d11(const std::vector<std::uint8_t>& dib, Texels& out, std::string& error) {
    AvAdapter adapter;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, error) || !device.create(adapter.luid, error)) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    D3D11_SUBRESOURCE_DATA initial{dib.data(), kEdge * 4, 0};
    Com<ID3D11Texture2D> source = D3d11Tools::texture(device, desc, &initial);
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11ShaderResourceView> view = source ? D3d11Tools::srv(device, source.get()) : Com<ID3D11ShaderResourceView>();
    Com<ID3D11RenderTargetView> rtv = target ? D3d11Tools::rtv(device, target.get()) : Com<ID3D11RenderTargetView>();
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }\n", error);
    if (!view || !rtv || !ps) {
        error = "the Direct3D 11 resources cannot be created: " + error;
        return false;
    }
    ID3D11ShaderResourceView* views[1] = {view.get()};
    device.context->PSSetShaderResources(0, 1, views);
    if (!D3d11Tools::draw_fullscreen(device, ps.get(), rtv.get(), kEdge, kEdge, error)) {
        return false;
    }
    ID3D11ShaderResourceView* none[1] = {nullptr};
    device.context->PSSetShaderResources(0, 1, none);
    device.context->OMSetRenderTargets(0, nullptr, nullptr);
    if (!D3d11Tools::wait_event(device, device.context, error)) {
        return false;
    }
    TexelImage image;
    if (!D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
        return false;
    }
    out.bytes = image.data;
    out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    return true;
}

bool via_d3d9(const std::vector<std::uint8_t>& dib, Texels& out, std::string& error) {
    D3d9Rig rig;
    D3d9Settings settings;
    settings.depth = false;
    settings.width = kEdge;
    settings.height = kEdge;
    if (!rig.open(false, error) || !rig.create(settings, error)) {
        return false;
    }
    HRESULT made = S_OK;
    Com<IDirect3DTexture9> texture = D3d9Tools::texture(rig.dev(), kEdge, kEdge, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &made);
    D3DLOCKED_RECT locked{};
    if (!texture || FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        error = "the managed A8R8G8B8 texture cannot be created or locked: " + D3d9Tools::hr(made);
        return false;
    }
    for (UINT y = 0; y < kEdge; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), dib.data() + static_cast<std::size_t>(y) * kEdge * 4u, kEdge * 4u);
    }
    texture->UnlockRect(0);
    SceneImage image;
    if (!D3d9Tools::sample(rig.dev(), texture.get(), kEdge, kEdge, image, error)) {
        return false;
    }
    out.bytes = image.data;
    out.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return true;
}

bool via_d3d12(const std::vector<std::uint8_t>& dib, Texels& out, std::string& error) {
    AvAdapter adapter;
    D3d12Rig rig;
    if (!TargetAdapter::pick(adapter, error) || !rig.open_luid(adapter.luid, D3D_FEATURE_LEVEL_11_0, error)) {
        return false;
    }
    D3d12Root root;
    root.table(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0).uav(0);
    ID3D12RootSignature* signature = root.build(rig, error);
    const std::string source =
        "Texture2D<float4> t : register(t0);\nRWByteAddressBuffer o : register(u0);\n[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  float4 c = t.Load(int3(id.xy, 0));\n  uint4 b = (uint4)(round(c * 255.0));\n  o.Store((id.y * 64 + id.x) * 4, b.x | (b.y << 8) | (b.z << 16) | (b.w << 24));\n}\n";
    ID3D12PipelineState* pso = signature != nullptr ? D3d12Tools::compute_from(rig, signature, source, error) : nullptr;
    ID3D12Resource* texture = D3d12Tools::texture(rig, D3d12Tools::texture_desc(kEdge, kEdge, DXGI_FORMAT_B8G8R8A8_UNORM), D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12Resource* output = D3d12Tools::device_buffer(rig, kEdge * kEdge * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12DescriptorHeap* views = D3d12Tools::descriptor_heap(rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4, true);
    if (pso == nullptr || texture == nullptr || output == nullptr || views == nullptr) {
        error = "the compute pipeline or resources cannot be created: " + error;
        return false;
    }
    if (!D3d12Tools::write_texture(rig, texture, D3D12_RESOURCE_STATE_COPY_DEST, 0, dib.data(), error)) {
        return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    rig.device()->CreateShaderResourceView(texture, &srv, views->GetCPUDescriptorHandleForHeapStart());
    const bool done = rig.direct().run_wait(
        [&](ID3D12GraphicsCommandList* list) {
            D3d12Tools::move(list, texture, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            ID3D12DescriptorHeap* heaps[] = {views};
            list->SetDescriptorHeaps(1, heaps);
            list->SetComputeRootSignature(signature);
            list->SetPipelineState(pso);
            list->SetComputeRootDescriptorTable(0, views->GetGPUDescriptorHandleForHeapStart());
            list->SetComputeRootUnorderedAccessView(1, output->GetGPUVirtualAddress());
            list->Dispatch(kEdge / 8, kEdge / 8, 1);
        },
        error);
    if (!done || !D3d12Tools::read_buffer(rig, output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 0, kEdge * kEdge * 4, out.bytes, error)) {
        return false;
    }
    out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    static_cast<void>(rig.idle(error));
    return true;
}

bool via_gl(const std::vector<std::uint8_t>& dib, Texels& out, std::string& error) {
    GlRig rig;
    GlRigOptions options;
    options.width = kEdge;
    options.height = kEdge;
    options.visible = false;
    if (!rig.open(options, error)) {
        return false;
    }
    const GlApi& api = rig.gl;
    unsigned texture = 0;
    api.glGenTextures(1, &texture);
    api.glBindTexture(glc::TEXTURE_2D, texture);
    api.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(glc::RGBA8), kEdge, kEdge, 0, glc::BGRA, glc::UNSIGNED_INT_8_8_8_8_REV, dib.data());
    api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
    GlFetchRequest request;
    request.sampler = "sampler2D";
    request.expression = "texelFetch(tex, p, 0)";
    request.target = glc::TEXTURE_2D;
    request.texture = texture;
    request.width = kEdge;
    request.height = kEdge;
    std::vector<float> values;
    const bool fetched = GlFetch::run(api, request, values, error);
    api.glFinish();
    api.glBindTexture(glc::TEXTURE_2D, 0);
    api.glDeleteTextures(1, &texture);
    api.glFinish();
    if (!fetched || values.size() < static_cast<std::size_t>(kEdge) * kEdge * 4u) {
        if (error.empty()) {
            error = "the OpenGL fetch returned too few values";
        }
        return false;
    }
    out.bytes.resize(static_cast<std::size_t>(kEdge) * kEdge * 4u);
    for (std::size_t i = 0; i < out.bytes.size(); ++i) {
        out.bytes[i] = static_cast<std::uint8_t>(std::lround(values[i] * 255.0f));
    }
    out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    return true;
}

bool via_vulkan(const std::vector<std::uint8_t>& dib, Texels& out, std::string& error) {
    std::string target_error;
    const std::vector<AvAdapter> adapters = VulkanRig::target_adapters(target_error);
    if (adapters.empty()) {
        error = "no Vulkan target adapter: " + target_error;
        return false;
    }
    VulkanRig rig;
    if (!rig.open(adapters.front(), VulkanRigOptions{}, error)) {
        return false;
    }
    VkFormatProperties source_props{};
    VkFormatProperties target_props{};
    rig.fn.vkGetPhysicalDeviceFormatProperties(rig.physical, VK_FORMAT_B8G8R8A8_UNORM, &source_props);
    rig.fn.vkGetPhysicalDeviceFormatProperties(rig.physical, VK_FORMAT_R8G8B8A8_UNORM, &target_props);
    if ((source_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) == 0 || (target_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) == 0) {
        error = "B8G8R8A8_UNORM cannot be a blit source or R8G8B8A8_UNORM a blit destination";
        return false;
    }
    VulkanImage source;
    VulkanImage target;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (source.create_2d(rig, VK_FORMAT_B8G8R8A8_UNORM, kEdge, kEdge, usage) != VK_SUCCESS || target.create_2d(rig, VK_FORMAT_R8G8B8A8_UNORM, kEdge, kEdge, usage) != VK_SUCCESS ||
        VulkanImageIo::to_general(rig, source) != VK_SUCCESS || VulkanImageIo::to_general(rig, target) != VK_SUCCESS) {
        error = "the Vulkan images cannot be created";
        return false;
    }
    ImageRegion region;
    region.extent = {kEdge, kEdge, 1};
    VkResult result = VulkanImageIo::write(rig, source, region, dib);
    if (result == VK_SUCCESS) {
        result = rig.run([&](VkCommandBuffer commands) {
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blit.srcOffsets[1] = {static_cast<int32_t>(kEdge), static_cast<int32_t>(kEdge), 1};
            blit.dstOffsets[1] = {static_cast<int32_t>(kEdge), static_cast<int32_t>(kEdge), 1};
            rig.fn.vkCmdBlitImage(commands, source.image, VK_IMAGE_LAYOUT_GENERAL, target.image, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
        });
    }
    if (result == VK_SUCCESS) {
        result = VulkanImageIo::read(rig, target, region, out.bytes, 4);
    }
    rig.wait_idle();
    if (result != VK_SUCCESS) {
        error = "upload, blit or readback returned " + VulkanTools::result_name(result);
        return false;
    }
    out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    return true;
}

}  // namespace

Outcome GdiDibChecks::section_upload(Context& context) {
    GdiDib dib;
    std::string error;
    if (!dib.create(kEdge, kEdge, error)) {
        return Outcome::fail(error);
    }
    Pattern::fill_pixels(Kit::seed(77), kEdge, kEdge, kEdge * 4, dib.bits());
    for (std::size_t i = 0; i < static_cast<std::size_t>(kEdge) * kEdge; ++i) {
        dib.bits()[i] |= 0xFF000000u;
    }
    ::GdiFlush();
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kEdge) * kEdge * 4u);
    std::memcpy(bytes.data(), dib.bits(), bytes.size());
    const FormatSpec source_spec = FormatCodec::spec(DXGI_FORMAT_B8G8R8A8_UNORM);
    struct Path {
        const char* api;
        bool (*run)(const std::vector<std::uint8_t>&, Texels&, std::string&);
    };
    Problems problems;
    for (const Path& path : {Path{"d3d9", via_d3d9}, Path{"d3d11", via_d3d11}, Path{"d3d12", via_d3d12}, Path{"gl", via_gl}, Path{"vulkan", via_vulkan}}) {
        if (ApiPolicy::is_skipped(path.api)) {
            continue;
        }
        Texels got;
        std::string local;
        if (!path.run(bytes, got, local)) {
            problems.add(std::string(path.api) + ": " + local);
            continue;
        }
        const FormatSpec spec = FormatCodec::spec(got.format);
        if (got.bytes.size() < bytes.size()) {
            problems.add(std::string(path.api) + ": " + std::to_string(got.bytes.size()) + " bytes came back, expected " + std::to_string(bytes.size()));
            continue;
        }
        for (UINT i = 0; i < kEdge * kEdge; ++i) {
            double want[4];
            double have[4];
            FormatCodec::decode(source_spec, &bytes[static_cast<std::size_t>(i) * 4u], want);
            FormatCodec::decode(spec, &got.bytes[static_cast<std::size_t>(i) * 4u], have);
            if (want[0] != have[0] || want[1] != have[1] || want[2] != have[2] || want[3] != have[3]) {
                problems.add(std::string(path.api) + ": pixel (" + std::to_string(i % kEdge) + "," + std::to_string(i / kEdge) + ") reads back R" + std::to_string(have[0] * 255.0) + " G" +
                             std::to_string(have[1] * 255.0) + " B" + std::to_string(have[2] * 255.0) + " A" + std::to_string(have[3] * 255.0) + " instead of the DIB's R" +
                             std::to_string(want[0] * 255.0) + " G" + std::to_string(want[1] * 255.0) + " B" + std::to_string(want[2] * 255.0) + " A" + std::to_string(want[3] * 255.0));
                ArtifactWriter writer(context);
                writer.bytes(std::string(path.api) + ".actual", got.bytes.data(), got.bytes.size());
                writer.bytes(std::string(path.api) + ".expected", bytes.data(), bytes.size());
                break;
            }
        }
    }
    return problems.outcome();
}

Outcome GdiDibChecks::after_gpu_writes(Context& context) {
    Problems problems;
    GdiDib dib;
    std::string error;
    if (!dib.create(kEdge, kEdge, error)) {
        return Outcome::fail(error);
    }
    AvAdapter adapter;
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        if (!TargetAdapter::pick(adapter, error) || !device.create_ex(adapter.luid, nullptr, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, error)) {
            problems.add("d3d11: " + error);
        } else {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
            desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
            Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, desc);
            Com<ID3D11RenderTargetView> view = texture ? D3d11Tools::rtv(device, texture.get()) : Com<ID3D11RenderTargetView>();
            Com<IDXGISurface1> surface = Kit::query<IDXGISurface1>(texture.get());
            D3d11FrameDraw draw;
            if (!texture || !view || !surface || !draw.open(device, error)) {
                problems.add("d3d11: a GDI compatible render target cannot be created: " + error);
            } else {
                for (int frame = 0; frame < 8; ++frame) {
                    draw.frame(view.get(), kEdge, kEdge, frame);
                    device.context->OMSetRenderTargets(0, nullptr, nullptr);
                    if (!D3d11Tools::wait_event(device, device.context, error)) {
                        problems.add("d3d11: " + error);
                        break;
                    }
                    HDC dc = nullptr;
                    HRESULT result = surface->GetDC(FALSE, &dc);
                    if (FAILED(result) || dc == nullptr) {
                        problems.add("d3d11 frame " + std::to_string(frame) + ": GetDC returned " + Kit::hr(result));
                        break;
                    }
                    dib.clear(0x00FF00FFu);
                    const BOOL copied = ::BitBlt(dib.dc(), 0, 0, kEdge, kEdge, dc, 0, 0, SRCCOPY);
                    surface->ReleaseDC(nullptr);
                    ::GdiFlush();
                    if (copied == FALSE) {
                        problems.add("d3d11 frame " + std::to_string(frame) + ": BitBlt from the surface device context failed");
                        break;
                    }
                    Kit::judge(context, problems, "d3d11_gpu_to_dib_" + std::to_string(frame), dib.snapshot(), 0, 0, kEdge, kEdge, FrameImage::render(frame, kEdge, kEdge), 0);
                    std::vector<std::uint8_t> pixels = Kit::frame_bytes(Kit::seed(88), frame, kEdge, kEdge);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(kEdge) * kEdge; ++i) {
                        reinterpret_cast<std::uint32_t*>(pixels.data())[i] |= 0xFF000000u;
                    }
                    std::memcpy(dib.bits(), pixels.data(), pixels.size());
                    ::GdiFlush();
                    result = surface->GetDC(TRUE, &dc);
                    if (FAILED(result) || dc == nullptr) {
                        problems.add("d3d11 frame " + std::to_string(frame) + ": GetDC (discard) returned " + Kit::hr(result));
                        break;
                    }
                    ::BitBlt(dc, 0, 0, kEdge, kEdge, dib.dc(), 0, 0, SRCCOPY);
                    ::GdiFlush();
                    surface->ReleaseDC(nullptr);
                    TexelImage image;
                    if (!D3d11Tools::read_texture(device, texture.get(), 0, image, error)) {
                        problems.add("d3d11: " + error);
                        break;
                    }
                    for (UINT i = 0; i < kEdge * kEdge; ++i) {
                        const std::uint32_t want = reinterpret_cast<const std::uint32_t*>(pixels.data())[i] & 0xFFFFFFu;
                        if ((image.pixel(i % kEdge, i / kEdge) & 0xFFFFFFu) != want) {
                            problems.add("d3d11 frame " + std::to_string(frame) + ": the GDI-written pixel (" + std::to_string(i % kEdge) + "," + std::to_string(i / kEdge) + ") reads " +
                                         Kit::rgb(image.pixel(i % kEdge, i / kEdge)) + " in Direct3D 11, expected " + Kit::rgb(want));
                            break;
                        }
                    }
                }
            }
            draw.release();
        }
    }
    if (!ApiPolicy::is_skipped("d3d9")) {
        D3d9Rig rig;
        D3d9Settings settings;
        settings.depth = false;
        if (!rig.open(false, error) || !rig.create(settings, error)) {
            problems.add("d3d9: " + error);
        } else {
            HRESULT made = S_OK;
            Com<IDirect3DSurface9> target = D3d9Tools::target(rig.dev(), kEdge, kEdge, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, true, &made);
            if (!target) {
                problems.add("d3d9: a lockable render target cannot be created: " + D3d9Tools::hr(made));
            } else {
                Com<IDirect3DSurface9> saved;
                rig.dev()->GetRenderTarget(0, saved.put());
                rig.dev()->SetRenderTarget(0, target.get());
                for (int frame = 0; frame < 8; ++frame) {
                    D3DRECT rects[1];
                    for (int q = 0; q < 4; ++q) {
                        const RECT r = FrameImage::quadrant(kEdge, kEdge, q);
                        rects[0] = {r.left, r.top, r.right, r.bottom};
                        rig.dev()->Clear(1, rects, D3DCLEAR_TARGET, FrameImage::color(frame, q), 1.0f, 0);
                    }
                    if (!D3d9Tools::wait_event(rig.dev(), error)) {
                        problems.add("d3d9: " + error);
                        break;
                    }
                    HDC dc = nullptr;
                    const HRESULT result = target->GetDC(&dc);
                    if (FAILED(result) || dc == nullptr) {
                        problems.add("d3d9 frame " + std::to_string(frame) + ": GetDC on a lockable render target returned " + D3d9Tools::hr(result));
                        break;
                    }
                    dib.clear(0x00FF00FFu);
                    const BOOL copied = ::BitBlt(dib.dc(), 0, 0, kEdge, kEdge, dc, 0, 0, SRCCOPY);
                    target->ReleaseDC(dc);
                    ::GdiFlush();
                    if (copied == FALSE) {
                        problems.add("d3d9 frame " + std::to_string(frame) + ": BitBlt from the surface device context failed");
                        break;
                    }
                    Kit::judge(context, problems, "d3d9_gpu_to_dib_" + std::to_string(frame), dib.snapshot(), 0, 0, kEdge, kEdge, FrameImage::render(frame, kEdge, kEdge), 0);
                }
                rig.dev()->SetRenderTarget(0, saved.get());
            }
            static_cast<void>(D3d9Tools::wait_event(rig.dev(), error));
        }
    }
    return problems.outcome();
}

Outcome GdiDibChecks::multi_thread(Context& context) {
    constexpr int kCanvas = 128;
    std::string error;
    TestWindow gdi_window;
    TestWindowConfig config;
    config.x = 40;
    config.y = 40;
    TestWindow::outer_size(config.x, config.y, kCanvas, kCanvas, config.width, config.height);
    if (!gdi_window.create(config, error)) {
        return Outcome::fail(error);
    }
    ::SetWindowPos(gdi_window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    std::unique_ptr<WindowPresenter> presenter = WindowPresenter::make("d3d11");
    if (!presenter->open(360, 40, kCanvas, kCanvas, error)) {
        presenter->close();
        return Outcome::fail("the Direct3D 11 window: " + error);
    }
    Problems problems;
    std::mutex guard;
    std::atomic<int> gdi_rounds{0};
    std::atomic<int> d3d_rounds{0};
    std::atomic<long long> slowest_gdi_ms{0};
    std::atomic<long long> slowest_d3d_ms{0};
    const auto stop = std::chrono::steady_clock::now() + std::chrono::seconds(4);
    std::thread gdi_thread([&] {
        HWND window = gdi_window.hwnd();
        for (int i = 0; std::chrono::steady_clock::now() < stop; ++i) {
            const auto begun = std::chrono::steady_clock::now();
            const std::uint32_t color = FrameImage::color(i % FrameImage::kFrames, 0) & 0xFFFFFFu;
            HDC dc = ::GetDC(window);
            HBRUSH brush = ::CreateSolidBrush(Kit::colorref(color));
            RECT area{0, 0, kCanvas, kCanvas};
            ::FillRect(dc, &area, brush);
            ::DeleteObject(brush);
            ::SetBkMode(dc, TRANSPARENT);
            ::TextOutA(dc, 4, 4, "thread", 6);
            ::GdiFlush();
            ::ReleaseDC(window, dc);
            Bitmap shot;
            std::string local;
            if (!ScreenCapture::window_client(window, shot, local)) {
                std::lock_guard lock(guard);
                problems.add("GDI thread: " + local);
                return;
            }
            if (shot.at(kCanvas - 4, kCanvas - 4) != color) {
                std::lock_guard lock(guard);
                problems.add("GDI thread round " + std::to_string(i) + ": the window device context reads " + Kit::rgb(shot.at(kCanvas - 4, kCanvas - 4)) + " after drawing " + Kit::rgb(color));
                return;
            }
            const long long took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begun).count();
            slowest_gdi_ms = std::max<long long>(slowest_gdi_ms, took);
            ++gdi_rounds;
        }
    });
    for (int i = 0; std::chrono::steady_clock::now() < stop; ++i) {
        const auto begun = std::chrono::steady_clock::now();
        if (!presenter->present(i % FrameImage::kFrames, error)) {
            std::lock_guard lock(guard);
            problems.add("Direct3D thread round " + std::to_string(i) + ": " + error);
            break;
        }
        std::string detail;
        if (i % 8 == 0 && !Kit::settle_screen(context, "d3d_round_" + std::to_string(i), ScreenCapture::client_on_screen(presenter->hwnd()), FrameImage::render(i % FrameImage::kFrames, kCanvas, kCanvas), 0, 3000, detail)) {
            std::lock_guard lock(guard);
            problems.add("Direct3D thread round " + std::to_string(i) + " is not on screen: " + detail);
            break;
        }
        const long long took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begun).count();
        slowest_d3d_ms = std::max<long long>(slowest_d3d_ms, took);
        ++d3d_rounds;
    }
    gdi_thread.join();
    presenter->close();
    context.measure("gdi_rounds", gdi_rounds.load());
    context.measure("d3d_rounds", d3d_rounds.load());
    context.measure("slowest_gdi_round_ms", slowest_gdi_ms.load());
    context.measure("slowest_d3d_round_ms", slowest_d3d_ms.load());
    if (gdi_rounds < 20) {
        problems.add("the GDI thread completed only " + std::to_string(gdi_rounds.load()) + " rounds in 4 s");
    }
    if (d3d_rounds < 20) {
        problems.add("the Direct3D thread completed only " + std::to_string(d3d_rounds.load()) + " rounds in 4 s");
    }
    if (slowest_gdi_ms > 2000 || slowest_d3d_ms > 2000) {
        problems.add("a round took longer than 2 s (GDI " + std::to_string(slowest_gdi_ms.load()) + " ms, Direct3D " + std::to_string(slowest_d3d_ms.load()) + " ms)");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
