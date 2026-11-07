#include "sanity/d3d11/transfer_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/format_codec.hpp"
#include "support/oracle_rig.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

namespace lsw::suites::sanity::d3d11 {

namespace {

struct Volume {
    UINT w = 0, h = 0, d = 1, bpp = 4;
    std::vector<std::uint8_t> bytes;

    Volume() = default;
    Volume(UINT width, UINT height, UINT depth, UINT bytes_per_pixel, std::uint64_t seed)
        : w(width), h(height), d(depth), bpp(bytes_per_pixel), bytes(static_cast<std::size_t>(width) * height * depth * bytes_per_pixel) {
        Pattern::fill(seed, 0, bytes.data(), bytes.size());
    }

    [[nodiscard]] std::size_t at(UINT x, UINT y, UINT z) const { return ((static_cast<std::size_t>(z) * h + y) * w + x) * bpp; }

    void blit(const Volume& src, UINT sx, UINT sy, UINT sz, UINT bw, UINT bh, UINT bd, UINT dx, UINT dy, UINT dz) {
        std::vector<std::uint8_t> copy = src.bytes;
        for (UINT z = 0; z < bd; ++z) {
            for (UINT y = 0; y < bh; ++y) {
                for (UINT x = 0; x < bw; ++x) {
                    std::memcpy(bytes.data() + at(dx + x, dy + y, dz + z), copy.data() + src.at(sx + x, sy + y, sz + z), bpp);
                }
            }
        }
    }
};

}  // namespace

Outcome TransferChecks::update_boxes_pitches(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    auto padded = [](const Volume& v, UINT bx, UINT by, UINT bz, UINT bw, UINT bh, UINT bd, UINT row_extra, UINT slice_extra,
                     UINT& row_pitch, UINT& slice_pitch) {
        row_pitch = bw * v.bpp + row_extra;
        slice_pitch = row_pitch * bh + slice_extra;
        std::vector<std::uint8_t> out(static_cast<std::size_t>(slice_pitch) * bd, 0xFE);
        for (UINT z = 0; z < bd; ++z) {
            for (UINT y = 0; y < bh; ++y) {
                std::memcpy(out.data() + static_cast<std::size_t>(z) * slice_pitch + static_cast<std::size_t>(y) * row_pitch,
                            v.bytes.data() + v.at(bx, by + y, bz + z), static_cast<std::size_t>(bw) * v.bpp);
            }
        }
        return out;
    };
    struct Box2 {
        const char* name;
        UINT x, y, w, h, row_extra;
    };
    const Box2 boxes[] = {{"tight", 3, 5, 8, 4, 0}, {"padded rows", 1, 1, 14, 7, 36}, {"single texel", 15, 15, 1, 1, 0}, {"one row", 0, 9, 16, 1, 12}, {"full", 0, 0, 16, 16, 0}};
    for (const Box2& b : boxes) {
        Com<ID3D11Texture2D> tex = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1, 2));
        if (!tex) {
            return Outcome::fail("a 2D array texture cannot be created");
        }
        for (UINT slice = 0; slice < 2; ++slice) {
            Volume model(16, 16, 1, 4, 10 + slice);
            ctx->UpdateSubresource(tex.get(), D3D11CalcSubresource(0, slice, 1), nullptr, model.bytes.data(), 16 * 4, 0);
        }
        Volume patch(b.w, b.h, 1, 4, 99);
        UINT row_pitch = 0;
        UINT slice_pitch = 0;
        Volume source(b.w, b.h, 1, 4, 99);
        const std::vector<std::uint8_t> data = padded(source, 0, 0, 0, b.w, b.h, 1, b.row_extra, 0, row_pitch, slice_pitch);
        const D3D11_BOX box{b.x, b.y, 0, b.x + b.w, b.y + b.h, 1};
        ctx->UpdateSubresource(tex.get(), D3D11CalcSubresource(0, 1, 1), &box, data.data(), row_pitch, 0);
        for (UINT slice = 0; slice < 2; ++slice) {
            Volume model(16, 16, 1, 4, 10 + slice);
            if (slice == 1) {
                model.blit(patch, 0, 0, 0, b.w, b.h, 1, b.x, b.y, 0);
            }
            TexelImage image;
            if (!D3d11Tools::read_texture(device, tex.get(), D3D11CalcSubresource(0, slice, 1), image, error)) {
                problems.add(error);
                continue;
            }
            if (image.data != model.bytes) {
                problems.add(std::string("2D box '") + b.name + "' slice " + std::to_string(slice) + " differs from the model");
            }
        }
    }
    {
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = desc.Height = desc.Depth = 8;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture3D> volume;
        if (FAILED(device.device->CreateTexture3D(&desc, nullptr, volume.put()))) {
            return Outcome::fail("a 3D texture cannot be created");
        }
        Volume model(8, 8, 8, 4, 21);
        ctx->UpdateSubresource(volume.get(), 0, nullptr, model.bytes.data(), 8 * 4, 8 * 8 * 4);
        Volume patch(4, 3, 5, 4, 22);
        UINT row_pitch = 0;
        UINT slice_pitch = 0;
        const std::vector<std::uint8_t> data = padded(patch, 0, 0, 0, 4, 3, 5, 20, 44, row_pitch, slice_pitch);
        const D3D11_BOX box{2, 1, 3, 6, 4, 8};
        ctx->UpdateSubresource(volume.get(), 0, &box, data.data(), row_pitch, slice_pitch);
        model.blit(patch, 0, 0, 0, 4, 3, 5, 2, 1, 3);
        TexelImage image;
        if (!D3d11Tools::read_volume(device, volume.get(), 0, image, error) || image.data != model.bytes) {
            problems.add("a 3D box update with custom row and depth pitches differs from the model");
        }
    }
    {
        D3D11_TEXTURE1D_DESC desc{};
        desc.Width = 32;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture1D> line;
        D3D11_TEXTURE1D_DESC staging_desc = desc;
        staging_desc.Usage = D3D11_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture1D> staging;
        if (FAILED(device.device->CreateTexture1D(&desc, nullptr, line.put())) || FAILED(device.device->CreateTexture1D(&staging_desc, nullptr, staging.put()))) {
            problems.add("1D textures cannot be created");
        } else {
            Volume model(32, 1, 1, 4, 30);
            ctx->UpdateSubresource(line.get(), 0, nullptr, model.bytes.data(), 0, 0);
            Volume patch(15, 1, 1, 4, 31);
            const D3D11_BOX box{5, 0, 0, 20, 1, 1};
            ctx->UpdateSubresource(line.get(), 0, &box, patch.bytes.data(), 0, 0);
            model.blit(patch, 0, 0, 0, 15, 1, 1, 5, 0, 0);
            ctx->CopyResource(staging.get(), line.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped)) || std::memcmp(mapped.pData, model.bytes.data(), model.bytes.size()) != 0) {
                problems.add("a 1D box update differs from the model");
            }
            ctx->Unmap(staging.get(), 0);
        }
    }
    {
        Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr);
        Volume model(256, 1, 1, 1, 40);
        ctx->UpdateSubresource(buffer.get(), 0, nullptr, model.bytes.data(), 0, 0);
        Volume patch(37, 1, 1, 1, 41);
        const D3D11_BOX box{11, 0, 0, 48, 1, 1};
        ctx->UpdateSubresource(buffer.get(), 0, &box, patch.bytes.data(), 0, 0);
        model.blit(patch, 0, 0, 0, 37, 1, 1, 11, 0, 0);
        std::vector<std::uint8_t> back;
        if (!buffer || !D3d11Tools::read_buffer(device, buffer.get(), back, error) || back != model.bytes) {
            problems.add("a buffer box update with null pitches differs from the model");
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome TransferChecks::update_bc_and_formats(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    struct Format {
        DXGI_FORMAT format;
        UINT block;
    };
    const Format blocks[] = {{DXGI_FORMAT_BC1_UNORM, 8}, {DXGI_FORMAT_BC2_UNORM, 16}, {DXGI_FORMAT_BC3_UNORM, 16}, {DXGI_FORMAT_BC4_UNORM, 8},
                             {DXGI_FORMAT_BC5_UNORM, 16}, {DXGI_FORMAT_BC6H_UF16, 16}, {DXGI_FORMAT_BC7_UNORM, 16}};
    D3d11Device& device = rig.av;
    for (const Format& f : blocks) {
        Com<ID3D11Texture2D> tex = D3d11Tools::texture(device, D3d11Tools::desc2d(20, 20, f.format, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 2));
        if (!tex) {
            problems.add("BC format " + std::to_string(f.format) + " cannot be created");
            continue;
        }
        const UINT row0 = 5 * f.block;
        Volume base(5, 5, 1, f.block, 50);
        device.context->UpdateSubresource(tex.get(), 0, nullptr, base.bytes.data(), row0, 0);
        Volume patch(2, 2, 1, f.block, 51);
        const D3D11_BOX aligned{4, 8, 0, 12, 16, 1};
        device.context->UpdateSubresource(tex.get(), 0, &aligned, patch.bytes.data(), 2 * f.block, 0);
        base.blit(patch, 0, 0, 0, 2, 2, 1, 1, 2, 0);
        TexelImage image;
        if (!D3d11Tools::read_texture(device, tex.get(), 0, image, error) || image.data != base.bytes) {
            problems.add("BC format " + std::to_string(f.format) + ": an aligned box update differs from the model");
        }
        Volume mip(3, 3, 1, f.block, 52);
        device.context->UpdateSubresource(tex.get(), 1, nullptr, mip.bytes.data(), 3 * f.block, 0);
        Volume edge(1, 1, 1, f.block, 53);
        const D3D11_BOX edge_box{8, 8, 0, 10, 10, 1};
        device.context->UpdateSubresource(tex.get(), 1, &edge_box, edge.bytes.data(), f.block, 0);
        mip.blit(edge, 0, 0, 0, 1, 1, 1, 2, 2, 0);
        if (!D3d11Tools::read_texture(device, tex.get(), 1, image, error) || image.data != mip.bytes) {
            problems.add("BC format " + std::to_string(f.format) + ": an edge-block update of mip 1 differs from the model");
        }
    }
    const DXGI_FORMAT packed[] = {DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R11G11B10_FLOAT, DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
                                  DXGI_FORMAT_B5G6R5_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16_UNORM, DXGI_FORMAT_R8G8_UNORM};
    for (DXGI_FORMAT f : packed) {
        const UINT bpp = static_cast<UINT>(FormatCodec::element_bytes(f));
        Com<ID3D11Texture2D> tex = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, f, D3D11_BIND_SHADER_RESOURCE));
        if (!tex) {
            problems.add("format " + std::to_string(f) + " cannot be created");
            continue;
        }
        Volume base(16, 16, 1, bpp, 60);
        device.context->UpdateSubresource(tex.get(), 0, nullptr, base.bytes.data(), 16 * bpp, 0);
        Volume patch(7, 5, 1, bpp, 61);
        const D3D11_BOX box{3, 4, 0, 10, 9, 1};
        device.context->UpdateSubresource(tex.get(), 0, &box, patch.bytes.data(), 7 * bpp, 0);
        base.blit(patch, 0, 0, 0, 7, 5, 1, 3, 4, 0);
        TexelImage image;
        if (!D3d11Tools::read_texture(device, tex.get(), 0, image, error) || image.data != base.bytes) {
            problems.add("format " + std::to_string(f) + ": a box update differs from the model");
        }
    }
    auto unaligned = [&](D3d11Device& d, SceneImage& out, std::string& why) {
        Com<ID3D11Texture2D> tex = D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_BC1_UNORM, D3D11_BIND_SHADER_RESOURCE));
        if (!tex) {
            why = "BC1 texture";
            return false;
        }
        Volume base(4, 4, 1, 8, 70);
        d.context->UpdateSubresource(tex.get(), 0, nullptr, base.bytes.data(), 32, 0);
        Volume patch(2, 2, 1, 8, 71);
        const D3D11_BOX bad{1, 1, 0, 9, 9, 1};
        d.context->UpdateSubresource(tex.get(), 0, &bad, patch.bytes.data(), 16, 0);
        TexelImage image;
        if (!D3d11Tools::read_texture(d, tex.get(), 0, image, why)) {
            return false;
        }
        out.data = image.data;
        return true;
    };
    SceneImage on_warp;
    SceneImage on_av;
    if (!unaligned(rig.warp, on_warp, error) || !unaligned(rig.av, on_av, error)) {
        problems.add("unaligned box scene: " + error);
    } else if (on_warp.data != on_av.data) {
        problems.add("an update with an unaligned BC box leaves different data than WARP");
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome TransferChecks::update_copy_flags(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    OracleRig::Scene constants = [](D3d11Device& d, SceneImage& out, std::string& why) {
        Com<ID3D11DeviceContext1> ctx = Kit::query<ID3D11DeviceContext1>(d.context);
        if (!ctx) {
            why = "ID3D11DeviceContext1 is not available";
            return false;
        }
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(d, "cbuffer C : register(b0) { float4 col; }\nfloat4 main() : SV_Target { return col; }", why);
        Com<ID3D11Buffer> cb = D3d11Tools::buffer(d, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
        Com<ID3D11Texture2D> targets[3] = {D3d11Tools::target(d, 8, 8), D3d11Tools::target(d, 8, 8), D3d11Tools::target(d, 8, 8)};
        if (!ps || !cb) {
            return false;
        }
        ID3D11Buffer* raw = cb.get();
        d.context->PSSetConstantBuffers(0, 1, &raw);
        const float colors[3][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
        const UINT flags[3] = {D3D11_COPY_DISCARD, D3D11_COPY_DISCARD, D3D11_COPY_NO_OVERWRITE};
        for (int i = 0; i < 3; ++i) {
            if (i == 2) {
                const D3D11_BOX box{0, 0, 0, 16, 1, 1};
                ctx->UpdateSubresource1(cb.get(), 0, &box, colors[i], 0, 0, flags[i]);
            } else {
                ctx->UpdateSubresource1(cb.get(), 0, nullptr, colors[i], 0, 0, flags[i]);
            }
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, targets[i].get());
            if (!D3d11Tools::draw_fullscreen(d, ps.get(), view.get(), 8, 8, why)) {
                return false;
            }
        }
        out.width = 8;
        out.height = 24;
        out.rgba = true;
        for (int i = 0; i < 3; ++i) {
            std::vector<std::uint8_t> px;
            if (!D3d11Tools::read_pixels(d, targets[i].get(), px, why)) {
                return false;
            }
            out.data.insert(out.data.end(), px.begin(), px.end());
        }
        D3d11Tools::reset_state(d);
        return true;
    };
    rig.compare(context, problems, "update_constants", constants, Tolerance::exact());
    OracleRig::Scene appends = [](D3d11Device& d, SceneImage& out, std::string& why) {
        Com<ID3D11DeviceContext1> ctx = Kit::query<ID3D11DeviceContext1>(d.context);
        if (!ctx) {
            why = "ID3D11DeviceContext1 is not available";
            return false;
        }
        struct Vertex {
            float x, y;
            std::uint32_t color;
        };
        const char* vs_source =
            "struct I { float2 p : POSITION; float4 c : COLOR; }; struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
            "O main(I i) { O o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }";
        Com<ID3D11VertexShader> vs = D3d11Tools::vs(d, vs_source, why);
        const std::vector<std::uint8_t> code = D3d11Tools::vs_code(d, vs_source, why);
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(d, "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }", why);
        const D3D11_INPUT_ELEMENT_DESC elements[2] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        Com<ID3D11InputLayout> layout;
        if (!vs || !ps || code.empty() || FAILED(d.device->CreateInputLayout(elements, 2, code.data(), code.size(), layout.put()))) {
            return false;
        }
        constexpr UINT kCount = 200;
        Com<ID3D11Buffer> vb = D3d11Tools::buffer(d, sizeof(Vertex) * kCount, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, nullptr);
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 100, 2);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        D3d11Tools::clear(d, view.get(), 0, 0, 0, 0);
        ID3D11RenderTargetView* list[1] = {view.get()};
        d.context->OMSetRenderTargets(1, list, nullptr);
        D3d11Tools::set_viewport(d, 100, 2);
        d.context->IASetInputLayout(layout.get());
        d.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        d.context->VSSetShader(vs.get(), nullptr, 0);
        d.context->PSSetShader(ps.get(), nullptr, 0);
        ID3D11Buffer* raw = vb.get();
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        d.context->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
        for (UINT i = 0; i < kCount; ++i) {
            Vertex v{(static_cast<float>(i % 100) + 0.5f) / 100.0f * 2.0f - 1.0f, 0.5f - static_cast<float>(i / 100), Kit::rgba(static_cast<int>(i), 255 - static_cast<int>(i), 9, 255)};
            const D3D11_BOX box{i * static_cast<UINT>(sizeof(Vertex)), 0, 0, (i + 1) * static_cast<UINT>(sizeof(Vertex)), 1, 1};
            ctx->UpdateSubresource1(vb.get(), 0, &box, &v, 0, 0, D3D11_COPY_NO_OVERWRITE);
            d.context->Draw(1, i);
        }
        std::vector<std::uint8_t> px;
        if (!D3d11Tools::read_pixels(d, target.get(), px, why)) {
            return false;
        }
        out.width = 100;
        out.height = 2;
        out.rgba = true;
        out.data = px;
        D3d11Tools::reset_state(d);
        return true;
    };
    rig.compare(context, problems, "update_appends", appends, Tolerance::exact());
    return problems.outcome();
}

Outcome TransferChecks::update_deferred_ctx(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11DeviceContext> deferred;
    if (FAILED(device.device->CreateDeferredContext(0, deferred.put()))) {
        return Outcome::fail("CreateDeferredContext failed");
    }
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "cbuffer C : register(b0) { float4 col; }\nfloat4 main() : SV_Target { return col; }", error);
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    Com<ID3D11Buffer> cb = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
    Com<ID3D11Texture2D> targets[3] = {D3d11Tools::target(device, 8, 8), D3d11Tools::target(device, 8, 8), D3d11Tools::target(device, 8, 8)};
    if (!ps || !vs || !cb) {
        return Outcome::fail("scene objects: " + error);
    }
    const float red[4] = {1, 0, 0, 1};
    const float green[4] = {0, 1, 0, 1};
    const float blue[4] = {0, 0, 1, 1};
    ID3D11Buffer* raw = cb.get();
    auto draw_into = [&](ID3D11DeviceContext* c, ID3D11Texture2D* target) {
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target);
        ID3D11RenderTargetView* list[1] = {view.get()};
        c->OMSetRenderTargets(1, list, nullptr);
        D3D11_VIEWPORT vp{0, 0, 8, 8, 0, 1};
        c->RSSetViewports(1, &vp);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.get(), nullptr, 0);
        c->PSSetShader(ps.get(), nullptr, 0);
        c->PSSetConstantBuffers(0, 1, &raw);
        c->Draw(3, 0);
    };
    deferred->UpdateSubresource(cb.get(), 0, nullptr, red, 0, 0);
    draw_into(deferred.get(), targets[0].get());
    deferred->UpdateSubresource(cb.get(), 0, nullptr, blue, 0, 0);
    draw_into(deferred.get(), targets[1].get());
    Com<ID3D11CommandList> list;
    if (FAILED(deferred->FinishCommandList(FALSE, list.put()))) {
        return Outcome::fail("FinishCommandList failed");
    }
    device.context->UpdateSubresource(cb.get(), 0, nullptr, green, 0, 0);
    draw_into(device.context, targets[2].get());
    device.context->ExecuteCommandList(list.get(), FALSE);
    const std::uint32_t expected[3] = {Kit::rgba(255, 0, 0, 255), Kit::rgba(0, 0, 255, 255), Kit::rgba(0, 255, 0, 255)};
    for (int i = 0; i < 3; ++i) {
        TexelImage image;
        std::string why;
        if (!D3d11Tools::read_texture(device, targets[i].get(), 0, image, error)) {
            problems.add(error);
        } else if (!Kit::uniform(image, expected[i], why)) {
            problems.add("target " + std::to_string(i) + ": " + why);
        }
    }
    std::vector<std::uint8_t> back;
    float final_color[4] = {};
    if (D3d11Tools::read_buffer(device, cb.get(), back, error) && back.size() >= 16) {
        std::memcpy(final_color, back.data(), 16);
        if (final_color[2] != 1.0f || final_color[1] != 0.0f) {
            problems.add("the constant buffer does not hold the last recorded update after execution");
        }
    } else {
        problems.add(error);
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome TransferChecks::copy_variants(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    D3d11Device& device = rig.av;
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    constexpr UINT kMips = 3;
    constexpr UINT kSlices = 3;
    const auto make = [&](D3d11Device& d, D3D11_USAGE usage, UINT cpu) {
        return D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 0, usage, cpu, kMips, kSlices));
    };
    auto fill = [&](D3d11Device& d, ID3D11Texture2D* t, std::vector<Volume>& model) {
        model.clear();
        for (UINT s = 0; s < kMips * kSlices; ++s) {
            const UINT mip = s % kMips;
            const UINT dim = 16u >> mip;
            model.emplace_back(dim, dim, 1, 4, 200 + s);
            d.context->UpdateSubresource(t, s, nullptr, model.back().bytes.data(), dim * 4, 0);
        }
    };
    auto check_all = [&](D3d11Device& d, ID3D11Texture2D* t, const std::vector<Volume>& model, const std::string& label) {
        for (UINT s = 0; s < model.size(); ++s) {
            TexelImage image;
            std::string why;
            if (!D3d11Tools::read_texture(d, t, s, image, why)) {
                problems.add(label + ": " + why);
                return;
            }
            if (image.data != model[s].bytes) {
                problems.add(label + ": subresource " + std::to_string(s) + " differs");
                return;
            }
        }
    };
    Com<ID3D11Texture2D> source = make(device, D3D11_USAGE_DEFAULT, 0);
    Com<ID3D11Texture2D> dest = make(device, D3D11_USAGE_DEFAULT, 0);
    Com<ID3D11Texture2D> stage_a = make(device, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);
    Com<ID3D11Texture2D> stage_b = make(device, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE);
    if (!source || !dest || !stage_a || !stage_b) {
        return Outcome::fail("array textures with mips cannot be created");
    }
    std::vector<Volume> model;
    fill(device, source.get(), model);
    ctx->CopyResource(dest.get(), source.get());
    check_all(device, dest.get(), model, "default to default");
    ctx->CopyResource(stage_a.get(), source.get());
    check_all(device, stage_a.get(), model, "default to staging");
    ctx->CopyResource(stage_b.get(), stage_a.get());
    check_all(device, stage_b.get(), model, "staging to staging");
    ctx->CopyResource(dest.get(), stage_b.get());
    check_all(device, dest.get(), model, "staging to default");
    {
        std::vector<Volume> expected = model;
        const D3D11_BOX box{2, 3, 0, 10, 11, 1};
        const UINT src_sub = D3D11CalcSubresource(0, 1, kMips);
        const UINT dst_sub = D3D11CalcSubresource(0, 2, kMips);
        ctx->CopySubresourceRegion(dest.get(), dst_sub, 5, 4, 0, source.get(), src_sub, &box);
        expected[dst_sub].blit(model[src_sub], 2, 3, 0, 8, 8, 1, 5, 4, 0);
        check_all(device, dest.get(), expected, "region within one mip, slice to slice");
        ctx->CopyResource(dest.get(), source.get());
        const UINT small = D3D11CalcSubresource(1, 2, kMips);
        const D3D11_BOX box2{0, 0, 0, 6, 6, 1};
        ctx->CopySubresourceRegion(dest.get(), small, 1, 1, 0, source.get(), D3D11CalcSubresource(0, 0, kMips), &box2);
        expected = model;
        expected[small].blit(model[D3D11CalcSubresource(0, 0, kMips)], 0, 0, 0, 6, 6, 1, 1, 1, 0);
        check_all(device, dest.get(), expected, "region from a large mip to a small mip");
        ctx->CopyResource(dest.get(), source.get());
        ctx->CopySubresourceRegion(stage_b.get(), 0, 0, 0, 0, source.get(), D3D11CalcSubresource(1, 1, kMips), nullptr);
        ctx->CopyResource(stage_b.get(), source.get());
    }
    {
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = desc.Height = desc.Depth = 8;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        Com<ID3D11Texture3D> a;
        Com<ID3D11Texture3D> b;
        if (FAILED(device.device->CreateTexture3D(&desc, nullptr, a.put())) || FAILED(device.device->CreateTexture3D(&desc, nullptr, b.put()))) {
            problems.add("3D textures cannot be created");
        } else {
            Volume va(8, 8, 8, 4, 300);
            Volume vb(8, 8, 8, 4, 301);
            ctx->UpdateSubresource(a.get(), 0, nullptr, va.bytes.data(), 32, 256);
            ctx->UpdateSubresource(b.get(), 0, nullptr, vb.bytes.data(), 32, 256);
            const D3D11_BOX box{1, 1, 1, 6, 5, 7};
            ctx->CopySubresourceRegion(b.get(), 0, 2, 3, 0, a.get(), 0, &box);
            vb.blit(va, 1, 1, 1, 5, 4, 6, 2, 3, 0);
            TexelImage image;
            if (!D3d11Tools::read_volume(device, b.get(), 0, image, error) || image.data != vb.bytes) {
                problems.add("a 3D box copy differs from the model");
            }
        }
    }
    auto overlap = [](D3d11Device& d, SceneImage& out, std::string& why) {
        Com<ID3D11Texture2D> t = D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        Volume v(16, 16, 1, 4, 400);
        d.context->UpdateSubresource(t.get(), 0, nullptr, v.bytes.data(), 64, 0);
        const D3D11_BOX box{0, 0, 0, 8, 8, 1};
        d.context->CopySubresourceRegion(t.get(), 0, 4, 4, 0, t.get(), 0, &box);
        TexelImage image;
        if (!D3d11Tools::read_texture(d, t.get(), 0, image, why)) {
            return false;
        }
        out.data = image.data;
        return true;
    };
    SceneImage on_warp;
    SceneImage on_av;
    if (!overlap(rig.warp, on_warp, error) || !overlap(rig.av, on_av, error)) {
        problems.add("overlap scene: " + error);
    } else if (on_warp.data != on_av.data) {
        problems.add("a copy within one subresource with overlapping rectangles gives a different result than WARP");
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome TransferChecks::copy_typeless_cross_format(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    D3d11Device& device = rig.av;
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    struct Chain {
        DXGI_FORMAT formats[4];
        UINT count;
        UINT bpp;
    };
    const Chain chains[] = {{{DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_TYPELESS, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB}, 4, 4},
                            {{DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT}, 4, 4},
                            {{DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_TYPELESS, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_UNORM}, 4, 8}};
    for (const Chain& chain : chains) {
        Volume model(16, 16, 1, chain.bpp, 500 + chain.bpp);
        std::vector<Com<ID3D11Texture2D>> textures;
        for (UINT i = 0; i < chain.count; ++i) {
            textures.push_back(D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, chain.formats[i], 0)));
            if (!textures.back()) {
                problems.add("format " + std::to_string(chain.formats[i]) + " cannot be created");
                break;
            }
        }
        if (textures.size() != chain.count || !textures.back()) {
            continue;
        }
        ctx->UpdateSubresource(textures[0].get(), 0, nullptr, model.bytes.data(), 16 * chain.bpp, 0);
        for (UINT i = 1; i < chain.count; ++i) {
            ctx->CopyResource(textures[i].get(), textures[i - 1].get());
        }
        for (UINT i = 0; i < chain.count; ++i) {
            TexelImage image;
            if (!D3d11Tools::read_texture(device, textures[i].get(), 0, image, error) || image.data != model.bytes) {
                problems.add("format " + std::to_string(chain.formats[i]) + " lost bits in a typeless copy chain");
            }
        }
    }
    struct Block {
        DXGI_FORMAT block;
        DXGI_FORMAT uncompressed;
        UINT bytes;
    };
    const Block blocks[] = {{DXGI_FORMAT_BC1_UNORM, DXGI_FORMAT_R32G32_UINT, 8}, {DXGI_FORMAT_BC4_UNORM, DXGI_FORMAT_R16G16B16A16_UINT, 8},
                            {DXGI_FORMAT_BC3_UNORM, DXGI_FORMAT_R32G32B32A32_UINT, 16}, {DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_R32G32B32A32_UINT, 16},
                            {DXGI_FORMAT_BC5_UNORM, DXGI_FORMAT_R16G16B16A16_UINT, 16}};
    for (const Block& b : blocks) {
        if (b.bytes == 16 && b.uncompressed == DXGI_FORMAT_R16G16B16A16_UINT) {
            continue;
        }
        Volume model(4, 4, 1, b.bytes, 600 + b.bytes);
        Com<ID3D11Texture2D> compressed = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, b.block, 0));
        Com<ID3D11Texture2D> raw = D3d11Tools::texture(device, D3d11Tools::desc2d(4, 4, b.uncompressed, 0));
        if (!compressed || !raw) {
            problems.add("block copy textures for format " + std::to_string(b.block) + " cannot be created");
            continue;
        }
        ctx->UpdateSubresource(raw.get(), 0, nullptr, model.bytes.data(), 4 * b.bytes, 0);
        ctx->CopySubresourceRegion(compressed.get(), 0, 0, 0, 0, raw.get(), 0, nullptr);
        TexelImage image;
        if (!D3d11Tools::read_texture(device, compressed.get(), 0, image, error) || image.data != model.bytes) {
            problems.add("copying uncompressed texels into BC format " + std::to_string(b.block) + " does not preserve the bits");
        }
        Com<ID3D11Texture2D> back = D3d11Tools::texture(device, D3d11Tools::desc2d(4, 4, b.uncompressed, 0));
        ctx->CopySubresourceRegion(back.get(), 0, 0, 0, 0, compressed.get(), 0, nullptr);
        if (!D3d11Tools::read_texture(device, back.get(), 0, image, error) || image.data != model.bytes) {
            problems.add("copying BC format " + std::to_string(b.block) + " blocks into uncompressed texels does not preserve the bits");
        }
    }
    auto incompatible = [](D3d11Device& d, SceneImage& out, std::string& why) {
        Com<ID3D11Texture2D> src = D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
        Com<ID3D11Texture2D> dst = D3d11Tools::texture(d, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R16G16B16A16_UNORM, 0));
        Volume a(16, 16, 1, 4, 700);
        Volume b(16, 16, 1, 8, 701);
        d.context->UpdateSubresource(src.get(), 0, nullptr, a.bytes.data(), 64, 0);
        d.context->UpdateSubresource(dst.get(), 0, nullptr, b.bytes.data(), 128, 0);
        d.context->CopyResource(dst.get(), src.get());
        TexelImage image;
        if (!D3d11Tools::read_texture(d, dst.get(), 0, image, why)) {
            return false;
        }
        out.data = image.data;
        return true;
    };
    SceneImage on_warp;
    SceneImage on_av;
    if (!incompatible(rig.warp, on_warp, error) || !incompatible(rig.av, on_av, error)) {
        problems.add("incompatible copy scene: " + error);
    } else if (on_warp.data != on_av.data) {
        problems.add("a copy between incompatible formats leaves different data than WARP");
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome TransferChecks::copy_structure_copy(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("append and consume buffers need feature level 11_0 and the device is " + std::to_string(device.level));
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    Com<ID3D11ComputeShader> append = D3d11Tools::cs(device,
        "AppendStructuredBuffer<uint> a : register(u0); [numthreads(37,1,1)] void main(uint3 id : SV_GroupThreadID) { a.Append(id.x + 1); }", error);
    Com<ID3D11ComputeShader> consume = D3d11Tools::cs(device,
        "ConsumeStructuredBuffer<uint> c : register(u0); RWByteAddressBuffer o : register(u1);\n"
        "[numthreads(10,1,1)] void main(uint3 id : SV_GroupThreadID) { o.Store(id.x * 4, c.Consume()); }", error);
    Com<ID3D11Buffer> list = D3d11Tools::buffer(device, 4 * 256, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 4, nullptr);
    Com<ID3D11Buffer> sink = D3d11Tools::buffer(device, 64, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
    Com<ID3D11Buffer> count = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, 0, 0, 0, 0, nullptr);
    if (!append || !consume || !list || !sink || !count) {
        return Outcome::fail("scene objects: " + error);
    }
    Com<ID3D11UnorderedAccessView> list_uav = D3d11Tools::structured_uav(device, list.get(), 256, D3D11_BUFFER_UAV_FLAG_APPEND);
    Com<ID3D11UnorderedAccessView> sink_uav = D3d11Tools::raw_uav(device, sink.get());
    if (!list_uav || !sink_uav) {
        return Outcome::fail("append views cannot be created");
    }
    auto counter = [&]() -> long long {
        ctx->CopyStructureCount(count.get(), 0, list_uav.get());
        const std::vector<std::uint32_t> words = D3d11Tools::read_words(device, count.get(), error);
        return words.empty() ? -1LL : static_cast<long long>(words[0]);
    };
    ID3D11UnorderedAccessView* views[2] = {list_uav.get(), sink_uav.get()};
    const UINT start = 5;
    const UINT keep = 0xFFFFFFFF;
    ctx->CSSetUnorderedAccessViews(0, 1, views, &start);
    if (counter() != 5) {
        problems.add("the initial count of 5 reads back as " + std::to_string(counter()));
    }
    ctx->CSSetUnorderedAccessViews(0, 1, views, &keep);
    ctx->CSSetShader(append.get(), nullptr, 0);
    ctx->Dispatch(1, 1, 1);
    if (counter() != 42) {
        problems.add("after one append dispatch from an initial count of 5 the count is " + std::to_string(counter()) + ", expected 42");
    }
    ctx->Dispatch(1, 1, 1);
    if (counter() != 79) {
        problems.add("after a second append dispatch the count is " + std::to_string(counter()) + ", expected 79");
    }
    ctx->CSSetShader(consume.get(), nullptr, 0);
    ctx->CSSetUnorderedAccessViews(0, 2, views, &keep);
    ctx->Dispatch(1, 1, 1);
    if (counter() != 69) {
        problems.add("after consuming 10 the count is " + std::to_string(counter()) + ", expected 69");
    }
    Com<ID3D11Buffer> structured = D3d11Tools::buffer(device, 12 * 20, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 12, nullptr);
    Com<ID3D11Buffer> copy = D3d11Tools::buffer(device, 12 * 20, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 12, nullptr);
    Volume model(20, 1, 1, 12, 800);
    ctx->UpdateSubresource(structured.get(), 0, nullptr, model.bytes.data(), 0, 0);
    ctx->CopyResource(copy.get(), structured.get());
    std::vector<std::uint8_t> back;
    if (!D3d11Tools::read_buffer(device, copy.get(), back, error) || back != model.bytes) {
        problems.add("CopyResource of a structured buffer does not preserve its layout");
    }
    D3D11_BUFFER_DESC desc{};
    copy->GetDesc(&desc);
    if (desc.StructureByteStride != 12 || (desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) == 0) {
        problems.add("the structured destination lost its stride");
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome TransferChecks::copy_resolve(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    struct Target {
        DXGI_FORMAT format;
        const char* name;
        int tolerance;
    };
    const Target targets[] = {{DXGI_FORMAT_R8G8B8A8_UNORM, "unorm", 1}, {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, "srgb", 3},
                              {DXGI_FORMAT_R16G16B16A16_FLOAT, "float16", 0}, {DXGI_FORMAT_R32G32B32A32_FLOAT, "float32", 0}};
    int ran = 0;
    for (const Target& target : targets) {
        UINT support = 0;
        if (FAILED(rig.av.device->CheckFormatSupport(target.format, &support)) || (support & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RESOLVE) == 0) {
            context.note(std::string(target.name) + " has no multisample resolve support, so it is not tested");
            continue;
        }
        for (UINT samples : {2u, 4u, 8u}) {
            UINT levels = 0;
            if (FAILED(rig.av.device->CheckMultisampleQualityLevels(target.format, samples, &levels)) || levels == 0) {
                continue;
            }
            ++ran;
            const std::string label = std::string(target.name) + " x" + std::to_string(samples);
            auto scene = [&](D3d11Device& d, SceneImage& out, std::string& why) {
                const std::string n = std::to_string(samples);
                Com<ID3D11PixelShader> ps = D3d11Tools::ps(d,
                    "float4 main(float4 p : SV_Position, uint s : SV_SampleIndex) : SV_Target { float v = (s + 1) * 0.1; return float4(v, 1.0 - v, 0.25, 1.0); }", why);
                Com<ID3D11Texture2D> multi = D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, target.format, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, samples));
                Com<ID3D11Texture2D> single = D3d11Tools::target(d, 8, 8, target.format);
                if (!ps || !multi || !single) {
                    why += " (resolve scene objects)";
                    return false;
                }
                Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, multi.get());
                if (!D3d11Tools::draw_fullscreen(d, ps.get(), view.get(), 8, 8, why)) {
                    return false;
                }
                d.context->ResolveSubresource(single.get(), 0, multi.get(), 0, target.format);
                if (!D3d11Tools::read_pixels(d, single.get(), out.data, why)) {
                    return false;
                }
                out.width = 8;
                out.height = 8;
                out.rgba = true;
                return true;
            };
            const bool as_float = target.format == DXGI_FORMAT_R32G32B32A32_FLOAT || target.format == DXGI_FORMAT_R16G16B16A16_FLOAT;
            SceneImage on_av;
            SceneImage on_warp;
            if (!scene(rig.warp, on_warp, error) || !scene(rig.av, on_av, error)) {
                problems.add(label + ": " + error);
                continue;
            }
            const FormatSpec spec = FormatCodec::spec(target.format);
            double mean = 0;
            for (UINT s = 0; s < samples; ++s) {
                mean += (s + 1) * 0.1;
            }
            mean /= samples;
            const double expected[4] = {mean, 1.0 - mean, 0.25, 1.0};
            std::string why;
            if (!FormatCodec::matches(spec, on_av.data.data() + static_cast<std::size_t>(spec.bytes) * 20, expected, why)) {
                problems.add(label + ": resolved value differs from the average of the sample values: " + why);
            }
            std::string detail;
            const Tolerance tolerance = as_float ? Tolerance::float32(2e-3f, 2e-3f) : Tolerance::bytes(target.tolerance + 1);
            if (!OracleRig::judge(context, "resolve_" + label, on_warp, on_av, tolerance, detail)) {
                problems.add(detail + " (against WARP)");
            }
        }
    }
    if (ran == 0) {
        problems.add("no multisample resolve combination is supported");
    }
    return problems.outcome();
}

Outcome TransferChecks::copy_generatemips(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    auto scene = [](D3d11Device& d, SceneImage& out, std::string& why) {
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 0);
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        Com<ID3D11Texture2D> tex = D3d11Tools::texture(d, desc);
        if (!tex) {
            why = "a GENERATE_MIPS texture cannot be created";
            return false;
        }
        std::vector<std::uint8_t> level0(64 * 64 * 4);
        for (UINT y = 0; y < 64; ++y) {
            for (UINT x = 0; x < 64; ++x) {
                std::uint8_t* p = level0.data() + (y * 64 + x) * 4;
                p[0] = static_cast<std::uint8_t>(x * 4);
                p[1] = static_cast<std::uint8_t>(y * 4);
                p[2] = static_cast<std::uint8_t>((x + y) * 2);
                p[3] = 255;
            }
        }
        d.context->UpdateSubresource(tex.get(), 0, nullptr, level0.data(), 256, 0);
        Com<ID3D11ShaderResourceView> srv = D3d11Tools::srv(d, tex.get());
        if (!srv) {
            why = "no shader resource view over the mip chain";
            return false;
        }
        d.context->GenerateMips(srv.get());
        D3D11_TEXTURE2D_DESC actual{};
        tex->GetDesc(&actual);
        out.data.clear();
        for (UINT m = 1; m < actual.MipLevels; ++m) {
            TexelImage image;
            if (!D3d11Tools::read_texture(d, tex.get(), m, image, why)) {
                return false;
            }
            out.data.insert(out.data.end(), image.data.begin(), image.data.end());
        }
        return true;
    };
    SceneImage on_av;
    SceneImage on_warp;
    if (!scene(rig.warp, on_warp, error) || !scene(rig.av, on_av, error)) {
        return Outcome::fail(error);
    }
    std::vector<std::uint8_t> expected;
    for (UINT m = 1; m <= 6; ++m) {
        const UINT dim = 64u >> m;
        const UINT scale = 1u << m;
        for (UINT y = 0; y < dim; ++y) {
            for (UINT x = 0; x < dim; ++x) {
                const double cx = x * scale + (scale - 1) / 2.0;
                const double cy = y * scale + (scale - 1) / 2.0;
                expected.push_back(static_cast<std::uint8_t>(std::lround(cx * 4)));
                expected.push_back(static_cast<std::uint8_t>(std::lround(cy * 4)));
                expected.push_back(static_cast<std::uint8_t>(std::lround((cx + cy) * 2)));
                expected.push_back(255);
            }
        }
    }
    SceneImage want;
    want.data = expected;
    std::string detail;
    if (!OracleRig::judge(context, "mips_vs_box_filter", want, on_av, Tolerance::bytes(3), detail)) {
        problems.add(detail + " (against the CPU box filter)");
    }
    detail.clear();
    if (!OracleRig::judge(context, "mips_vs_warp", on_warp, on_av, Tolerance::bytes(3), detail)) {
        problems.add(detail + " (against WARP)");
    }
    const DXGI_FORMAT candidates[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R8_UINT,
                                      DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R8_UNORM};
    for (DXGI_FORMAT f : candidates) {
        UINT support = 0;
        rig.av.device->CheckFormatSupport(f, &support);
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(16, 16, f, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 0);
        desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        const bool av_created = static_cast<bool>(D3d11Tools::texture(rig.av, desc));
        const bool warp_created = static_cast<bool>(D3d11Tools::texture(rig.warp, desc));
        const bool flagged = (support & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;
        if (flagged && !av_created) {
            problems.add("format " + std::to_string(f) + " reports MIP_AUTOGEN but a GENERATE_MIPS texture cannot be created");
        }
        if (av_created != warp_created) {
            problems.add("format " + std::to_string(f) + ": a GENERATE_MIPS texture is " + (av_created ? "created" : "refused") + " while WARP " +
                         (warp_created ? "creates it" : "refuses it"));
        }
    }
    return problems.outcome();
}

Outcome TransferChecks::copy_clearview_discard(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11DeviceContext1> ctx = Kit::query<ID3D11DeviceContext1>(device.context);
    if (!ctx) {
        return Outcome::fail("ID3D11DeviceContext1 is not available");
    }
    D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
    const bool have_options = SUCCEEDED(device.device->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options)));
    Com<ID3D11Texture2D> a = D3d11Tools::target(device, 64, 64);
    Com<ID3D11Texture2D> b = D3d11Tools::target(device, 64, 64);
    Com<ID3D11RenderTargetView> va = D3d11Tools::rtv(device, a.get());
    Com<ID3D11RenderTargetView> vb = D3d11Tools::rtv(device, b.get());
    if (!va || !vb) {
        return Outcome::fail("render targets cannot be created");
    }
    D3d11Tools::clear(device, va.get(), 0, 0, 1, 1);
    D3d11Tools::clear(device, vb.get(), 0, 1, 0, 1);
    if (!have_options || options.ClearView == FALSE) {
        context.note("D3D11_OPTIONS reports no ClearView support; the rectangle part is not tested");
    } else {
        const D3D11_RECT rects[2] = {{2, 3, 10, 11}, {20, 20, 30, 40}};
        const float red[4] = {1, 0, 0, 1};
        ctx->ClearView(va.get(), red, rects, 2);
        TexelImage image;
        if (!D3d11Tools::read_texture(device, a.get(), 0, image, error)) {
            problems.add(error);
        } else {
            for (UINT y = 0; y < 64 && problems.empty(); ++y) {
                for (UINT x = 0; x < 64; ++x) {
                    bool inside = false;
                    for (const D3D11_RECT& r : rects) {
                        inside = inside || (x >= static_cast<UINT>(r.left) && x < static_cast<UINT>(r.right) && y >= static_cast<UINT>(r.top) && y < static_cast<UINT>(r.bottom));
                    }
                    const std::uint32_t want = inside ? Kit::rgba(255, 0, 0, 255) : Kit::rgba(0, 0, 255, 255);
                    if (image.pixel(x, y) != want) {
                        problems.add("ClearView: pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) + ", expected 0x" + Text::hex(want, 8));
                        break;
                    }
                }
            }
        }
        ctx->ClearView(va.get(), red, nullptr, 0);
        if (D3d11Tools::read_texture(device, a.get(), 0, image, error)) {
            std::string why;
            if (!Kit::uniform(image, Kit::rgba(255, 0, 0, 255), why)) {
                problems.add("ClearView without rectangles did not clear everything: " + why);
            }
        }
    }
    ctx->DiscardView(va.get());
    ctx->DiscardResource(a.get());
    TexelImage image;
    std::string why;
    if (!D3d11Tools::read_texture(device, b.get(), 0, image, error)) {
        problems.add(error);
    } else if (!Kit::uniform(image, Kit::rgba(0, 255, 0, 255), why)) {
        problems.add("discarding one resource changed another: " + why);
    }
    D3d11Tools::clear(device, va.get(), 0.2f, 0.4f, 0.6f, 1.0f);
    if (!D3d11Tools::read_texture(device, a.get(), 0, image, error)) {
        problems.add(error);
    } else if (!Kit::uniform(image, Kit::rgba(51, 102, 153, 255), why)) {
        problems.add("a clear after DiscardResource does not define the resource: " + why);
    }
    Com<ID3D11PixelShader> ps = Kit::color_ps(device, "1, 1, 0, 1", error);
    ctx->DiscardView(vb.get());
    if (!ps || !D3d11Tools::draw_fullscreen(device, ps.get(), vb.get(), 64, 64, error)) {
        problems.add("a draw after DiscardView fails: " + error);
    } else if (!D3d11Tools::read_texture(device, b.get(), 0, image, error) || !Kit::uniform(image, Kit::rgba(255, 255, 0, 255), why)) {
        problems.add("a full draw after DiscardView does not define the resource: " + why + error);
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
