#include "sanity/fmt/depth_checks.hpp"

#include "sanity/fmt/kit.hpp"
#include "sanity/fmt/vulkan_format_io.hpp"
#include "sanity/gl/kit.hpp"
#include "sanity/vk/kit.hpp"
#include "support/format_runner.hpp"
#include "support/gl_fetch.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lsw::suites::sanity::fmt {

namespace {

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

struct DepthCase {
    const char* name;
    DXGI_FORMAT typeless;
    DXGI_FORMAT dsv;
    DXGI_FORMAT depth_view;
    DXGI_FORMAT stencil_view;
    int depth_bits;
    bool floating;
};

const DepthCase kCases[] = {{"D16_UNORM", DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_UNKNOWN, 16, false},
                            {"D24_UNORM_S8_UINT", DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
                             DXGI_FORMAT_X24_TYPELESS_G8_UINT, 24, false},
                            {"D32_FLOAT", DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN, 32, true},
                            {"D32_FLOAT_S8X24_UINT", DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS,
                             DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, 32, true}};

double quantize(const DepthCase& c, double depth) {
    if (c.floating) {
        return static_cast<double>(static_cast<float>(depth));
    }
    const double top = c.depth_bits == 16 ? 65535.0 : 16777215.0;
    return std::nearbyint(depth * top) / top;
}

double resolution(const DepthCase& c) {
    if (c.floating) {
        return 1e-7;
    }
    return (c.depth_bits == 16 ? 1.0 / 65535.0 : 1.0 / 16777215.0) * 1.01 + 1e-7;
}

std::string vertex_at(const std::string& depth) {
    std::string source = std::string("float4 main(uint id : SV_VertexID) : SV_Position {\n  float2 p = float2((id << 1) & 2, id & 2);\n  return float4(p * float2(2, -2) + float2(-1, 1), ") +
                         depth + ", 1);\n}\n";
    return source;
}

class D3d11Depth {
public:
    D3d11Depth(D3d11Device& device, const DepthCase& item, Limited& out) : device_(device), case_(item), out_(out) {}

    bool prepare() {
        HRESULT result = S_OK;
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(8, 8, case_.typeless, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE);
        texture_ = D3d11Tools::texture(device_, desc, nullptr, &result);
        if (!texture_) {
            out_.add(std::string("D3D11 ") + case_.name + " depth stencil texture with shader access cannot be created: " + D3d11Tools::hr(result));
            return false;
        }
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
        dsv.Format = case_.dsv;
        dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
        result = device_.device->CreateDepthStencilView(texture_.get(), &dsv, view_.put());
        if (FAILED(result)) {
            out_.add(std::string("D3D11 ") + case_.name + " depth stencil view cannot be created: " + D3d11Tools::hr(result));
            return false;
        }
        return true;
    }

    void clear(double depth, int stencil) {
        const UINT flags = case_.stencil_view != DXGI_FORMAT_UNKNOWN ? (D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL) : D3D11_CLEAR_DEPTH;
        device_.context->ClearDepthStencilView(view_.get(), flags, static_cast<float>(depth), static_cast<UINT8>(stencil));
    }

    bool plane(DXGI_FORMAT view_format, const char* type, std::vector<std::uint32_t>& words, std::string& error) {
        D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = view_format;
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        desc.Texture2D.MipLevels = 1;
        Com<ID3D11ShaderResourceView> srv;
        HRESULT result = device_.device->CreateShaderResourceView(texture_.get(), &desc, srv.put());
        if (FAILED(result)) {
            error = "creating the " + std::string(Kit::number_name(static_cast<int>(view_format))) + " view failed: " + D3d11Tools::hr(result);
            return false;
        }
        Com<ID3D11Buffer> output = D3d11Tools::buffer(device_, 8 * 8 * 16, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0,
                                                      D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr, &result);
        Com<ID3D11UnorderedAccessView> uav = output ? D3d11Tools::raw_uav(device_, output.get()) : Com<ID3D11UnorderedAccessView>();
        Com<ID3D11ComputeShader> shader = D3d11Tools::cs(device_, FormatRunner::sample_source(type, 8, 8), error);
        if (!output || !uav || !shader) {
            if (error.empty()) {
                error = "creating the output buffer failed: " + D3d11Tools::hr(result);
            }
            return false;
        }
        ID3D11RenderTargetView* none[1] = {nullptr};
        device_.context->OMSetRenderTargets(1, none, nullptr);
        ID3D11ShaderResourceView* views[1] = {srv.get()};
        ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
        device_.context->CSSetShader(shader.get(), nullptr, 0);
        device_.context->CSSetShaderResources(0, 1, views);
        device_.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
        device_.context->Dispatch(1, 1, 1);
        ID3D11ShaderResourceView* no_views[1] = {nullptr};
        ID3D11UnorderedAccessView* no_uavs[1] = {nullptr};
        device_.context->CSSetShaderResources(0, 1, no_views);
        device_.context->CSSetUnorderedAccessViews(0, 1, no_uavs, nullptr);
        words = D3d11Tools::read_words(device_, output.get(), error);
        return words.size() == 64 * 4;
    }

    bool draw(const std::string& depth, bool stencil, D3D11_STENCIL_OP pass_op, UINT reference, std::string& error) {
        Com<ID3D11VertexShader> vertex = D3d11Tools::vs(device_, vertex_at(depth), error);
        if (!vertex) {
            return false;
        }
        D3D11_DEPTH_STENCIL_DESC state{};
        state.DepthEnable = TRUE;
        state.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        state.DepthFunc = D3D11_COMPARISON_LESS;
        state.StencilEnable = stencil ? TRUE : FALSE;
        state.StencilReadMask = 0xFF;
        state.StencilWriteMask = 0xFF;
        for (D3D11_DEPTH_STENCILOP_DESC* face : {&state.FrontFace, &state.BackFace}) {
            face->StencilFunc = D3D11_COMPARISON_EQUAL;
            face->StencilPassOp = pass_op;
            face->StencilFailOp = D3D11_STENCIL_OP_KEEP;
            face->StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
        }
        Com<ID3D11DepthStencilState> ds;
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        Com<ID3D11RasterizerState> rs;
        if (FAILED(device_.device->CreateDepthStencilState(&state, ds.put())) || FAILED(device_.device->CreateRasterizerState(&raster, rs.put()))) {
            error = "creating the depth stencil state failed";
            return false;
        }
        device_.context->OMSetRenderTargets(0, nullptr, view_.get());
        device_.context->OMSetDepthStencilState(ds.get(), reference);
        device_.context->RSSetState(rs.get());
        D3d11Tools::set_viewport(device_, 8, 8);
        device_.context->IASetInputLayout(nullptr);
        device_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device_.context->VSSetShader(vertex.get(), nullptr, 0);
        device_.context->PSSetShader(nullptr, nullptr, 0);
        device_.context->Draw(3, 0);
        device_.context->OMSetRenderTargets(0, nullptr, nullptr);
        return true;
    }

    bool read(double& depth, int& stencil, std::string& error) {
        std::vector<std::uint32_t> words;
        if (!plane(case_.depth_view, "float4", words, error)) {
            return false;
        }
        float value = 0.0f;
        std::memcpy(&value, &words[9 * 4], 4);
        depth = value;
        for (int t = 0; t < 64; ++t) {
            float other = 0.0f;
            std::memcpy(&other, &words[static_cast<std::size_t>(t) * 4], 4);
            if (other != value) {
                error = "the depth plane is not uniform after a full-surface operation (texel " + std::to_string(t) + " holds " + std::to_string(other) +
                        ", texel 9 holds " + std::to_string(value) + ")";
                return false;
            }
        }
        stencil = -1;
        if (case_.stencil_view != DXGI_FORMAT_UNKNOWN) {
            if (!plane(case_.stencil_view, "uint4", words, error)) {
                return false;
            }
            stencil = static_cast<int>(words[9 * 4 + 1]);
            for (int t = 0; t < 64; ++t) {
                if (static_cast<int>(words[static_cast<std::size_t>(t) * 4 + 1]) != stencil) {
                    error = "the stencil plane is not uniform after a full-surface operation";
                    return false;
                }
            }
        }
        return true;
    }

private:
    D3d11Device& device_;
    const DepthCase& case_;
    Limited& out_;
    Com<ID3D11Texture2D> texture_;
    Com<ID3D11DepthStencilView> view_;
};

void d3d11_part(Context& context, Problems& problems) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open_d3d11(adapter, device, error)) {
        problems.add("D3D11: " + error);
        return;
    }
    Limited out(problems);
    int tested = 0;
    for (const DepthCase& item : kCases) {
        UINT bits = 0;
        if (FAILED(device.device->CheckFormatSupport(item.dsv, &bits)) || (bits & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) == 0) {
            continue;
        }
        const std::string name = std::string("D3D11 ") + item.name;
        D3d11Depth depth(device, item, out);
        if (!depth.prepare()) {
            continue;
        }
        ++tested;
        const bool has_stencil = item.stencil_view != DXGI_FORMAT_UNKNOWN;
        const double depths[] = {0.0, 0.25, 0.5, 0.75, 1.0, 1.0 / 3.0};
        const int stencils[] = {0, 0x55, 0xAA, 0xFF, 0x01, 0x80};
        for (int i = 0; i < 6; ++i) {
            depth.clear(depths[i], stencils[i]);
            double got_depth = 0.0;
            int got_stencil = 0;
            if (!depth.read(got_depth, got_stencil, error)) {
                out.add(name + " clear to depth " + std::to_string(depths[i]) + ": " + error);
                continue;
            }
            if (std::fabs(got_depth - quantize(item, depths[i])) > resolution(item)) {
                out.add(name + " cleared to depth " + std::to_string(depths[i]) + " reads back as " + std::to_string(got_depth) + ", the format stores " +
                        std::to_string(quantize(item, depths[i])));
            }
            if (has_stencil && got_stencil != stencils[i]) {
                out.add(name + " cleared to stencil " + std::to_string(stencils[i]) + " reads back as " + std::to_string(got_stencil));
            }
        }
        depth.clear(0.5, 0x55);
        const struct {
            const char* z;
            D3D11_STENCIL_OP op;
            UINT reference;
            double depth_after;
            int stencil_after;
            const char* what;
        } steps[] = {{"0.25", D3D11_STENCIL_OP_INCR_SAT, 0x55, 0.25, 0x56, "a draw nearer than the stored depth with a matching stencil"},
                     {"0.75", D3D11_STENCIL_OP_INCR_SAT, 0x56, 0.25, 0x56, "a draw behind the stored depth"},
                     {"0.1", D3D11_STENCIL_OP_INCR_SAT, 0x00, 0.25, 0x56, "a draw nearer than the stored depth with a failing stencil test"},
                     {"0.1", D3D11_STENCIL_OP_INCR_SAT, 0x56, 0.1, 0x57, "a draw nearer than the stored depth with a matching stencil"}};
        for (const auto& step : steps) {
            if (!has_stencil && step.reference == 0x00) {
                continue;
            }
            if (!depth.draw(step.z, has_stencil, step.op, step.reference, error)) {
                out.add(name + ": " + error);
                break;
            }
            double got_depth = 0.0;
            int got_stencil = 0;
            if (!depth.read(got_depth, got_stencil, error)) {
                out.add(name + " after " + step.what + ": " + error);
                break;
            }
            if (std::fabs(got_depth - quantize(item, step.depth_after)) > resolution(item)) {
                out.add(name + " after " + step.what + " the depth is " + std::to_string(got_depth) + ", expected " + std::to_string(quantize(item, step.depth_after)));
            }
            if (has_stencil && got_stencil != step.stencil_after) {
                out.add(name + " after " + step.what + " the stencil is " + std::to_string(got_stencil) + ", expected " + std::to_string(step.stencil_after));
            }
        }
        if (has_stencil) {
            depth.clear(1.0, 0xFF);
            double got_depth = 0.0;
            int got_stencil = 0;
            if (depth.draw("0.5", true, D3D11_STENCIL_OP_INCR_SAT, 0xFF, error) && depth.read(got_depth, got_stencil, error) && got_stencil != 0xFF) {
                out.add(name + " saturating increment of stencil 255 gives " + std::to_string(got_stencil));
            }
            depth.clear(1.0, 0xFF);
            if (depth.draw("0.5", true, D3D11_STENCIL_OP_INCR, 0xFF, error) && depth.read(got_depth, got_stencil, error) && got_stencil != 0) {
                out.add(name + " wrapping increment of stencil 255 gives " + std::to_string(got_stencil) + ", expected 0 for an 8 bit stencil");
            }
        }
    }
    context.measure("fmt.depth.d3d11.formats", tested);
}

struct VulkanDepth {
    const char* name;
    VkFormat format;
    int depth_bits;
    bool floating;
    bool stencil;
};

const VulkanDepth kVulkanCases[] = {{"D16_UNORM", VK_FORMAT_D16_UNORM, 16, false, false},
                                    {"D32_SFLOAT", VK_FORMAT_D32_SFLOAT, 32, true, false},
                                    {"D24_UNORM_S8_UINT", VK_FORMAT_D24_UNORM_S8_UINT, 24, false, true},
                                    {"D32_SFLOAT_S8_UINT", VK_FORMAT_D32_SFLOAT_S8_UINT, 32, true, true}};

Outcome vulkan_part(Context& context) {
    return vk::Kit::each_device(context, [&](VulkanRig& rig, Problems& problems) -> std::string {
        Limited out(problems);
        for (const VulkanDepth& item : kVulkanCases) {
            const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            if (!VulkanFormatIo::image_ok(rig, item.format, usage, 8, 8)) {
                continue;
            }
            const std::string name = std::string("Vulkan ") + item.name;
            VulkanImage image;
            VkResult result = VulkanFormatIo::make(rig, image, item.format, 8, 8, usage);
            if (result != VK_SUCCESS) {
                out.add(name + " image creation failed: " + vk::Kit::name(result));
                continue;
            }
            const double depths[] = {0.0, 0.25, 0.5, 0.75, 1.0, 1.0 / 3.0};
            const int stencils[] = {0, 0x55, 0xAA, 0xFF, 0x01, 0x80};
            for (int i = 0; i < 6; ++i) {
                VkClearDepthStencilValue value{static_cast<float>(depths[i]), static_cast<std::uint32_t>(stencils[i])};
                const VkImageSubresourceRange range = {image.aspect(), 0, 1, 0, 1};
                result = rig.run([&](VkCommandBuffer commands) {
                    rig.fn.vkCmdClearDepthStencilImage(commands, image.image, VK_IMAGE_LAYOUT_GENERAL, &value, 1, &range);
                    VkMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    rig.fn.vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
                });
                std::vector<std::uint8_t> depth_bytes;
                if (result == VK_SUCCESS) {
                    ImageRegion region;
                    region.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                    region.extent = {8, 8, 1};
                    const std::size_t texel = item.depth_bits == 16 ? 2 : 4;
                    result = VulkanImageIo::read_raw(rig, image, region, 64 * texel, depth_bytes);
                }
                if (result != VK_SUCCESS) {
                    out.add(name + " clear or readback failed: " + vk::Kit::name(result));
                    break;
                }
                double got = 0.0;
                double want = depths[i];
                if (item.floating) {
                    float single = 0.0f;
                    std::memcpy(&single, depth_bytes.data() + 9 * 4, 4);
                    got = single;
                    want = static_cast<float>(depths[i]);
                } else if (item.depth_bits == 16) {
                    std::uint16_t raw = 0;
                    std::memcpy(&raw, depth_bytes.data() + 9 * 2, 2);
                    got = raw / 65535.0;
                    want = std::nearbyint(depths[i] * 65535.0) / 65535.0;
                } else {
                    std::uint32_t raw = 0;
                    std::memcpy(&raw, depth_bytes.data() + 9 * 4, 4);
                    got = (raw & 0xFFFFFFu) / 16777215.0;
                    want = std::nearbyint(depths[i] * 16777215.0) / 16777215.0;
                }
                const double limit = item.floating ? 1e-7 : (item.depth_bits == 16 ? 1.01 / 65535.0 : 1.01 / 16777215.0) + 1e-7;
                if (std::fabs(got - want) > limit) {
                    out.add(name + " cleared to depth " + std::to_string(depths[i]) + " reads back as " + std::to_string(got) + ", expected " + std::to_string(want));
                }
                if (item.stencil) {
                    std::vector<std::uint8_t> stencil_bytes;
                    ImageRegion region;
                    region.aspect = VK_IMAGE_ASPECT_STENCIL_BIT;
                    region.extent = {8, 8, 1};
                    result = VulkanImageIo::read_raw(rig, image, region, 64, stencil_bytes);
                    if (result != VK_SUCCESS) {
                        out.add(name + " stencil readback failed: " + vk::Kit::name(result));
                    } else if (stencil_bytes[9] != stencils[i]) {
                        out.add(name + " cleared to stencil " + std::to_string(stencils[i]) + " reads back as " + std::to_string(stencil_bytes[9]));
                    }
                }
            }
        }
        return {};
    });
}

struct GlDepth {
    const char* name;
    unsigned internal;
    unsigned format;
    unsigned type;
    unsigned attachment;
    int depth_bits;
    bool floating;
    bool stencil;
};

void gl_part(Context& context, Problems& problems) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        problems.add("OpenGL: " + error);
        return;
    }
    const GlApi& api = rig.gl;
    if (!api.supports(3, 0, "GL_ARB_framebuffer_object")) {
        return;
    }
    const GlDepth cases[] = {{"DEPTH_COMPONENT16", glc::DEPTH_COMPONENT16, glc::DEPTH_COMPONENT, glc::UNSIGNED_SHORT, glc::DEPTH_ATTACHMENT, 16, false, false},
                             {"DEPTH_COMPONENT24", glc::DEPTH_COMPONENT24, glc::DEPTH_COMPONENT, glc::UNSIGNED_INT, glc::DEPTH_ATTACHMENT, 24, false, false},
                             {"DEPTH_COMPONENT32F", glc::DEPTH_COMPONENT32F, glc::DEPTH_COMPONENT, glc::FLOAT, glc::DEPTH_ATTACHMENT, 32, true, false},
                             {"DEPTH24_STENCIL8", glc::DEPTH24_STENCIL8, glc::DEPTH_STENCIL, glc::UNSIGNED_INT_24_8, glc::DEPTH_STENCIL_ATTACHMENT, 24, false, true},
                             {"DEPTH32F_STENCIL8", glc::DEPTH32F_STENCIL8, glc::DEPTH_STENCIL, glc::FLOAT_32_UNSIGNED_INT_24_8_REV,
                              glc::DEPTH_STENCIL_ATTACHMENT, 32, true, true}};
    Limited out(problems);
    for (const GlDepth& item : cases) {
        api.drain_errors();
        unsigned texture = 0;
        unsigned fbo = 0;
        api.glGenTextures(1, &texture);
        api.glBindTexture(glc::TEXTURE_2D, texture);
        api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<int>(glc::NEAREST));
        api.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<int>(glc::NEAREST));
        api.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<int>(item.internal), 8, 8, 0, item.format, item.type, nullptr);
        api.glGenFramebuffers(1, &fbo);
        api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        api.glFramebufferTexture2D(glc::FRAMEBUFFER, item.attachment, glc::TEXTURE_2D, texture, 0);
        api.glDrawBuffer(glc::NONE);
        api.glReadBuffer(glc::NONE);
        const std::string name = std::string("OpenGL ") + item.name;
        if (api.glGetError() == 0 && api.glCheckFramebufferStatus(glc::FRAMEBUFFER) == glc::FRAMEBUFFER_COMPLETE) {
            const double depths[] = {0.0, 0.25, 0.5, 0.75, 1.0, 1.0 / 3.0};
            const int stencils[] = {0, 0x55, 0xAA, 0xFF, 0x01, 0x80};
            for (int i = 0; i < 6; ++i) {
                api.glDepthMask(1);
                api.glClearDepth(depths[i]);
                api.glClearStencil(stencils[i]);
                api.glClear(glc::DEPTH_BUFFER_BIT | (item.stencil ? glc::STENCIL_BUFFER_BIT : 0u));
                float depth_values[64];
                api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
                api.glReadPixels(0, 0, 8, 8, glc::DEPTH_COMPONENT, glc::FLOAT, depth_values);
                const double top = item.floating ? 0.0 : (item.depth_bits == 16 ? 65535.0 : 16777215.0);
                const double want = item.floating ? static_cast<double>(static_cast<float>(depths[i])) : std::nearbyint(depths[i] * top) / top;
                const double limit = item.floating ? 1e-7 : 1.01 / top + 1e-7;
                if (std::fabs(static_cast<double>(depth_values[9]) - want) > limit) {
                    out.add(name + " cleared to depth " + std::to_string(depths[i]) + " reads back as " + std::to_string(depth_values[9]) + ", expected " +
                            std::to_string(want));
                }
                if (item.stencil) {
                    std::uint8_t stencil_values[64];
                    api.glReadPixels(0, 0, 8, 8, glc::STENCIL_INDEX, glc::UNSIGNED_BYTE, stencil_values);
                    if (stencil_values[9] != stencils[i]) {
                        out.add(name + " cleared to stencil " + std::to_string(stencils[i]) + " reads back as " + std::to_string(stencil_values[9]));
                    }
                }
                const unsigned failure = api.glGetError();
                if (failure != 0) {
                    out.add(name + " clear or readback raised " + GlApi::error_name(failure));
                    break;
                }
            }
        } else {
            api.drain_errors();
        }
        api.glFinish();
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        api.glDeleteFramebuffers(1, &fbo);
        api.glBindTexture(glc::TEXTURE_2D, 0);
        api.glDeleteTextures(1, &texture);
    }
}

void merge(const char* api, const Outcome& outcome, Problems& problems, std::vector<std::string>& skips, bool& exercised) {
    if (outcome.status() == Status::Fail) {
        problems.add(std::string(api) + ": " + outcome.reason());
    } else if (outcome.status() == Status::Skip) {
        skips.push_back(std::string(api) + ": " + outcome.reason());
    } else {
        exercised = true;
    }
}

}  // namespace

Outcome DepthChecks::depth_stencil(Context& context) {
    Problems problems;
    std::vector<std::string> skips;
    bool exercised = false;
    if (Kit::enabled("d3d11")) {
        d3d11_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("D3D11 excluded");
    }
    if (Kit::enabled("vulkan")) {
        merge("Vulkan", vulkan_part(context), problems, skips, exercised);
    } else {
        skips.push_back("Vulkan excluded");
    }
    if (Kit::enabled("gl")) {
        gl_part(context, problems);
        exercised = true;
    } else {
        skips.push_back("OpenGL excluded");
    }
    return Kit::finish(skips, problems, exercised);
}

}  // namespace lsw::suites::sanity::fmt
