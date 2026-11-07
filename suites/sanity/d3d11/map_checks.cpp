#include "sanity/d3d11/map_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/format_codec.hpp"
#include "support/oracle_rig.hpp"
#include "support/pattern.hpp"
#include "support/swap_rig.hpp"

#include <algorithm>
#include <chrono>
#include <functional>

namespace lsw::suites::sanity::d3d11 {

namespace {

bool image_equals(const TexelImage& image, const std::vector<std::uint32_t>& expected, std::string& why) {
    if (image.data.size() != expected.size() * 4) {
        why = "the readback has " + std::to_string(image.data.size()) + " bytes, expected " + std::to_string(expected.size() * 4);
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        std::uint32_t value = 0;
        std::memcpy(&value, image.data.data() + i * 4, 4);
        if (value != expected[i]) {
            why = "texel " + std::to_string(i) + " is 0x" + Text::hex(value, 8) + ", expected 0x" + Text::hex(expected[i], 8);
            return false;
        }
    }
    return true;
}

}  // namespace

Outcome MapChecks::read_last_write(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    constexpr UINT kSize = 64;
    Problems problems;
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R32_UINT,
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE));
    Com<ID3D11Texture2D> source = D3d11Tools::texture(device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R32_UINT, D3D11_BIND_SHADER_RESOURCE));
    if (!texture || !source) {
        return Outcome::fail("R32_UINT render, UAV and shader resource textures cannot be created");
    }
    Com<ID3D11RenderTargetView> rtv = D3d11Tools::rtv(device, texture.get());
    Com<ID3D11UnorderedAccessView> uav = D3d11Tools::uav(device, texture.get());
    Com<ID3D11PixelShader> draw_ps = D3d11Tools::ps(device,
        "uint main(float4 p : SV_Position) : SV_Target { uint2 q = uint2(p.xy); return q.x * 1000 + q.y * 7 + 1; }", error);
    Com<ID3D11ComputeShader> cs = D3d11Tools::cs(device,
        "RWTexture2D<uint> o : register(u0); [numthreads(8,8,1)] void main(uint3 id : SV_DispatchThreadID) { o[id.xy] = id.x * 31 + id.y * 17 + 5; }", error);
    if (!rtv || !uav || !draw_ps || !cs) {
        return Outcome::fail("views or shaders are missing: " + error);
    }
    std::vector<std::uint32_t> pattern(kSize * kSize);
    for (UINT y = 0; y < kSize; ++y) {
        for (UINT x = 0; x < kSize; ++x) {
            pattern[y * kSize + x] = static_cast<std::uint32_t>(Pattern::pixel(5, x, y));
        }
    }
    device.context->UpdateSubresource(source.get(), 0, nullptr, pattern.data(), kSize * 4, 0);
    auto check = [&](const char* name, const std::vector<std::uint32_t>& expected) {
        TexelImage image;
        std::string why;
        if (!D3d11Tools::read_texture(device, texture.get(), 0, image, error)) {
            problems.add(std::string(name) + ": " + error);
        } else if (!image_equals(image, expected, why)) {
            problems.add(std::string(name) + ": " + why);
        }
    };
    std::vector<std::uint32_t> expected(kSize * kSize);
    for (int round = 0; round < 2; ++round) {
        {
            if (!D3d11Tools::draw_fullscreen(device, draw_ps.get(), rtv.get(), kSize, kSize, error)) {
                return Outcome::fail(error);
            }
            for (UINT y = 0; y < kSize; ++y) {
                for (UINT x = 0; x < kSize; ++x) {
                    expected[y * kSize + x] = x * 1000 + y * 7 + 1;
                }
            }
            check("draw", expected);
        }
        {
            ID3D11UnorderedAccessView* views[1] = {uav.get()};
            device.context->OMSetRenderTargets(0, nullptr, nullptr);
            device.context->CSSetShader(cs.get(), nullptr, 0);
            device.context->CSSetUnorderedAccessViews(0, 1, views, nullptr);
            device.context->Dispatch(kSize / 8, kSize / 8, 1);
            ID3D11UnorderedAccessView* none[1] = {nullptr};
            device.context->CSSetUnorderedAccessViews(0, 1, none, nullptr);
            for (UINT y = 0; y < kSize; ++y) {
                for (UINT x = 0; x < kSize; ++x) {
                    expected[y * kSize + x] = x * 31 + y * 17 + 5;
                }
            }
            check("compute", expected);
        }
        {
            device.context->CopyResource(texture.get(), source.get());
            check("copy", pattern);
        }
        {
            const UINT value[4] = {0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF};
            device.context->ClearUnorderedAccessViewUint(uav.get(), value);
            std::fill(expected.begin(), expected.end(), 0xDEADBEEFu);
            check("clear", expected);
        }
    }
    {
        Com<ID3D11Texture2D> multi = D3d11Tools::texture(device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, 4));
        Com<ID3D11Texture2D> resolved = D3d11Tools::target(device, kSize, kSize);
        Com<ID3D11PixelShader> flat = Kit::color_ps(device, "0.2, 0.4, 0.6, 1.0", error);
        if (!multi || !flat) {
            context.note("4x multisampling is unavailable, the resolve writer is skipped");
        } else {
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, multi.get());
            if (!D3d11Tools::draw_fullscreen(device, flat.get(), view.get(), kSize, kSize, error)) {
                return Outcome::fail(error);
            }
            device.context->ResolveSubresource(resolved.get(), 0, multi.get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            TexelImage image;
            std::string why;
            if (!D3d11Tools::read_texture(device, resolved.get(), 0, image, error)) {
                problems.add("resolve: " + error);
            } else if (!Kit::uniform(image, Kit::rgba(51, 102, 153, 255), why)) {
                problems.add("resolve: " + why);
            }
        }
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome MapChecks::discard_old_draws(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    Com<ID3D11Texture2D> targets[2] = {D3d11Tools::target(device, 8, 8), D3d11Tools::target(device, 8, 8)};
    Com<ID3D11RenderTargetView> views[2] = {D3d11Tools::rtv(device, targets[0].get()), D3d11Tools::rtv(device, targets[1].get())};
    if (!views[0] || !views[1]) {
        return Outcome::fail("render targets cannot be created");
    }
    const std::uint32_t red = Kit::rgba(255, 0, 0, 255);
    const std::uint32_t blue = Kit::rgba(0, 0, 255, 255);
    auto verify = [&](const char* name) {
        TexelImage a;
        TexelImage b;
        std::string why;
        if (!D3d11Tools::read_texture(device, targets[0].get(), 0, a, error) || !D3d11Tools::read_texture(device, targets[1].get(), 0, b, error)) {
            problems.add(std::string(name) + ": " + error);
            return;
        }
        if (!Kit::uniform(a, red, why)) {
            problems.add(std::string(name) + ": the draw before the discard rendered the wrong data: " + why);
        }
        if (!Kit::uniform(b, blue, why)) {
            problems.add(std::string(name) + ": the draw after the discard rendered the wrong data: " + why);
        }
    };
    auto start = [&](ID3D11RenderTargetView* view) {
        ID3D11RenderTargetView* list[1] = {view};
        ctx->OMSetRenderTargets(1, list, nullptr);
        D3d11Tools::set_viewport(device, 8, 8);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    };
    Com<ID3D11VertexShader> fullscreen = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    {
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "cbuffer C : register(b0) { float4 col; }\nfloat4 main() : SV_Target { return col; }", error);
        Com<ID3D11Buffer> cb = D3d11Tools::buffer(device, 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        if (!ps || !cb || !fullscreen) {
            return Outcome::fail("constant buffer scene: " + error);
        }
        ID3D11Buffer* raw = cb.get();
        ctx->PSSetConstantBuffers(0, 1, &raw);
        ctx->VSSetShader(fullscreen.get(), nullptr, 0);
        ctx->PSSetShader(ps.get(), nullptr, 0);
        const float colors[2][4] = {{1, 0, 0, 1}, {0, 0, 1, 1}};
        for (int i = 0; i < 2; ++i) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(ctx->Map(cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return Outcome::fail("Map(WRITE_DISCARD) of the constant buffer failed");
            }
            std::memcpy(mapped.pData, colors[i], 16);
            ctx->Unmap(cb.get(), 0);
            start(views[i].get());
            ctx->Draw(3, 0);
        }
        verify("constant buffer");
    }
    {
        struct Vertex {
            float x, y;
            float r, g, b, a;
        };
        Com<ID3D11VertexShader> vs = D3d11Tools::vs(device,
            "struct I { float2 p : POSITION; float4 c : COLOR; }; struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
            "O main(I i) { O o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }", error);
        std::vector<std::uint8_t> code = D3d11Tools::vs_code(device,
            "struct I { float2 p : POSITION; float4 c : COLOR; }; struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
            "O main(I i) { O o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }", error);
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }", error);
        const D3D11_INPUT_ELEMENT_DESC elements[2] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        Com<ID3D11InputLayout> layout;
        if (!vs || !ps || code.empty() || FAILED(device.device->CreateInputLayout(elements, 2, code.data(), code.size(), layout.put()))) {
            return Outcome::fail("vertex scene: " + error);
        }
        Com<ID3D11Buffer> vb = D3d11Tools::buffer(device, sizeof(Vertex) * 6, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        Com<ID3D11Buffer> ib = D3d11Tools::buffer(device, 12, D3D11_USAGE_DYNAMIC, D3D11_BIND_INDEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        if (!vb || !ib) {
            return Outcome::fail("dynamic vertex or index buffers cannot be created");
        }
        ctx->IASetInputLayout(layout.get());
        ctx->VSSetShader(vs.get(), nullptr, 0);
        ctx->PSSetShader(ps.get(), nullptr, 0);
        UINT stride = sizeof(Vertex);
        UINT offset = 0;
        ID3D11Buffer* raw = vb.get();
        ctx->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
        auto triangle = [](float r, float g, float b) {
            return std::vector<Vertex>{{-1, 1, r, g, b, 1}, {3, 1, r, g, b, 1}, {-1, -3, r, g, b, 1}};
        };
        for (int i = 0; i < 2; ++i) {
            const std::vector<Vertex> data = triangle(i == 0 ? 1.0f : 0.0f, 0, i == 0 ? 0.0f : 1.0f);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(ctx->Map(vb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return Outcome::fail("Map(WRITE_DISCARD) of the vertex buffer failed");
            }
            std::memcpy(mapped.pData, data.data(), sizeof(Vertex) * 3);
            ctx->Unmap(vb.get(), 0);
            start(views[i].get());
            ctx->Draw(3, 0);
        }
        verify("vertex buffer");
        {
            const std::vector<Vertex> both = [&] {
                std::vector<Vertex> out = triangle(1, 0, 0);
                const std::vector<Vertex> second = triangle(0, 0, 1);
                out.insert(out.end(), second.begin(), second.end());
                return out;
            }();
            D3D11_MAPPED_SUBRESOURCE mapped{};
            ctx->Map(vb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            std::memcpy(mapped.pData, both.data(), sizeof(Vertex) * 6);
            ctx->Unmap(vb.get(), 0);
            ctx->IASetIndexBuffer(ib.get(), DXGI_FORMAT_R16_UINT, 0);
            const std::uint16_t indices[2][3] = {{0, 1, 2}, {3, 4, 5}};
            for (int i = 0; i < 2; ++i) {
                ctx->Map(ib.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
                std::memcpy(mapped.pData, indices[i], 6);
                ctx->Unmap(ib.get(), 0);
                start(views[i].get());
                ctx->DrawIndexed(3, 0, 0);
            }
            verify("index buffer");
        }
    }
    {
        Com<ID3D11Texture2D> dynamic = D3d11Tools::texture(device, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE));
        Com<ID3D11ShaderResourceView> srv = dynamic ? D3d11Tools::srv(device, dynamic.get()) : Com<ID3D11ShaderResourceView>();
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "Texture2D t : register(t0); float4 main() : SV_Target { return t.Load(int3(1, 1, 0)); }", error);
        if (!srv || !ps || !fullscreen) {
            return Outcome::fail("dynamic texture scene: " + error);
        }
        ID3D11ShaderResourceView* raw = srv.get();
        ctx->PSSetShaderResources(0, 1, &raw);
        ctx->IASetInputLayout(nullptr);
        ctx->VSSetShader(fullscreen.get(), nullptr, 0);
        ctx->PSSetShader(ps.get(), nullptr, 0);
        const std::uint32_t colors[2] = {red, blue};
        for (int i = 0; i < 2; ++i) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(ctx->Map(dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return Outcome::fail("Map(WRITE_DISCARD) of the dynamic texture failed");
            }
            for (UINT y = 0; y < 4; ++y) {
                std::uint32_t* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                std::fill(row, row + 4, colors[i]);
            }
            ctx->Unmap(dynamic.get(), 0);
            start(views[i].get());
            ctx->Draw(3, 0);
        }
        verify("dynamic texture");
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome MapChecks::nooverwrite_inflight(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    constexpr UINT kWidth = 100;
    constexpr UINT kHeight = 10;
    constexpr UINT kCount = 1000;
    constexpr UINT kCapacity = 256;
    struct Vertex {
        float x, y;
        std::uint32_t color;
    };
    const char* vs_source =
        "struct I { float2 p : POSITION; float4 c : COLOR; }; struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
        "O main(I i) { O o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }";
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, vs_source, error);
    const std::vector<std::uint8_t> code = D3d11Tools::vs_code(device, vs_source, error);
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }", error);
    const D3D11_INPUT_ELEMENT_DESC elements[2] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                  {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    Com<ID3D11InputLayout> layout;
    if (!vs || !ps || code.empty() || FAILED(device.device->CreateInputLayout(elements, 2, code.data(), code.size(), layout.put()))) {
        return Outcome::fail("scene shaders: " + error);
    }
    Com<ID3D11Buffer> vb = D3d11Tools::buffer(device, sizeof(Vertex) * kCapacity, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, kWidth, kHeight);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    if (!vb || !view) {
        return Outcome::fail("buffers cannot be created");
    }
    D3d11Tools::clear(device, view.get(), 0, 0, 0, 0);
    ID3D11RenderTargetView* list[1] = {view.get()};
    ctx->OMSetRenderTargets(1, list, nullptr);
    D3d11Tools::set_viewport(device, kWidth, kHeight);
    ctx->IASetInputLayout(layout.get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    ctx->VSSetShader(vs.get(), nullptr, 0);
    ctx->PSSetShader(ps.get(), nullptr, 0);
    ID3D11Buffer* raw = vb.get();
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
    UINT used = kCapacity;
    for (UINT i = 0; i < kCount; ++i) {
        D3D11_MAP mode = D3D11_MAP_WRITE_NO_OVERWRITE;
        if (used == kCapacity) {
            mode = D3D11_MAP_WRITE_DISCARD;
            used = 0;
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(ctx->Map(vb.get(), 0, mode, 0, &mapped))) {
            return Outcome::fail("Map failed in cycle " + std::to_string(i));
        }
        Vertex vertex{};
        vertex.x = ((i % kWidth) + 0.5f) / kWidth * 2.0f - 1.0f;
        vertex.y = 1.0f - ((i / kWidth) + 0.5f) / kHeight * 2.0f;
        vertex.color = Kit::rgba(static_cast<int>(i & 255), static_cast<int>((i >> 8) & 255), 77, 255);
        std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + used * sizeof(Vertex), &vertex, sizeof(Vertex));
        ctx->Unmap(vb.get(), 0);
        ctx->Draw(1, used);
        ++used;
    }
    TexelImage image;
    if (!D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    UINT bad = 0;
    for (UINT i = 0; i < kCount; ++i) {
        const std::uint32_t want = Kit::rgba(static_cast<int>(i & 255), static_cast<int>((i >> 8) & 255), 77, 255);
        const std::uint32_t got = image.pixel(i % kWidth, i / kWidth);
        if (got != want && bad++ < 3) {
            problems.add("append cycle " + std::to_string(i) + " rendered 0x" + Text::hex(got, 8) + ", expected 0x" + Text::hex(want, 8));
        }
    }
    context.measure("bad_cycles", bad);
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome MapChecks::do_not_wait(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    constexpr UINT kSize = 1024;
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, kSize, kSize);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    Com<ID3D11PixelShader> heavy = D3d11Tools::ps(device,
        "float4 main(float4 p : SV_Position) : SV_Target { float a = p.x * 0.001; for (int i = 0; i < 400; ++i) { a = sin(a) * 1.0001 + cos(a * 1.1) * 0.5; } return float4(0, 0.5, 0, 1); }", error);
    if (!view || !staging || !heavy) {
        return Outcome::fail("scene objects: " + error);
    }
    Problems problems;
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT idle = ctx->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (idle != S_OK) {
            problems.add("DO_NOT_WAIT on an idle staging resource returned " + D3d11Tools::hr(idle));
        } else {
            ctx->Unmap(staging.get(), 0);
        }
    }
    D3D11_QUERY_DESC qd{};
    qd.Query = D3D11_QUERY_EVENT;
    Com<ID3D11Query> before_copy;
    device.device->CreateQuery(&qd, before_copy.put());
    auto submit = [&](int draws) {
        D3d11Tools::clear(device, view.get(), 0, 0, 0, 1);
        for (int i = 0; i < draws; ++i) {
            static_cast<void>(D3d11Tools::draw_fullscreen(device, heavy.get(), view.get(), kSize, kSize, error));
        }
        ctx->End(before_copy.get());
        ctx->CopyResource(staging.get(), target.get());
    };
    int draws = 2;
    for (int round = 0; round < 6; ++round) {
        const auto before = std::chrono::steady_clock::now();
        submit(draws);
        static_cast<void>(D3d11Tools::wait_event(device, ctx, error));
        const auto took = std::chrono::steady_clock::now() - before;
        if (took > std::chrono::milliseconds(400) || draws >= 512) {
            break;
        }
        draws *= 3;
    }
    context.measure("draws", draws);
    int still_drawing = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        Com<ID3D11Query> event;
        device.device->CreateQuery(&qd, event.put());
        submit(draws);
        ctx->End(event.get());
        ctx->Flush();
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        bool mapped_ok = false;
        while (!mapped_ok && std::chrono::steady_clock::now() < limit) {
            BOOL done = FALSE;
            const bool finished = ctx->GetData(event.get(), &done, sizeof(done), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto start = std::chrono::steady_clock::now();
            const HRESULT result = ctx->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed > std::chrono::milliseconds(250)) {
                problems.add("a DO_NOT_WAIT map blocked for " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) + " ms");
                return problems.outcome();
            }
            if (result == DXGI_ERROR_WAS_STILL_DRAWING) {
                ++still_drawing;
                if (finished) {
                    problems.add("DO_NOT_WAIT reported WAS_STILL_DRAWING after the event query had completed");
                    return problems.outcome();
                }
                ::Sleep(2);
            } else if (result == S_OK) {
                BOOL done_after = FALSE;
                if (ctx->GetData(before_copy.get(), &done_after, sizeof(done_after), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) {
                    problems.add("DO_NOT_WAIT mapped successfully before the GPU work behind it completed");
                }
                const std::uint32_t* first = static_cast<const std::uint32_t*>(mapped.pData);
                if (first[0] != Kit::rgba(0, 128, 0, 255)) {
                    problems.add("the mapped result holds 0x" + Text::hex(first[0], 8));
                }
                ctx->Unmap(staging.get(), 0);
                mapped_ok = true;
            } else {
                problems.add("DO_NOT_WAIT returned " + D3d11Tools::hr(result));
                return problems.outcome();
            }
        }
        if (!mapped_ok) {
            problems.add("DO_NOT_WAIT never succeeded within 60 s");
            return problems.outcome();
        }
    }
    context.measure("still_drawing_polls", still_drawing);
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome MapChecks::pitches(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    struct Case {
        const char* name;
        DXGI_FORMAT format;
        UINT width, height, depth, array_size, mips;
    };
    const Case cases[] = {{"rgba8 odd", DXGI_FORMAT_R8G8B8A8_UNORM, 13, 7, 1, 1, 1},
                          {"rgba8 array", DXGI_FORMAT_R8G8B8A8_UNORM, 20, 9, 1, 3, 2},
                          {"r8 odd", DXGI_FORMAT_R8_UNORM, 17, 5, 1, 1, 1},
                          {"rgba16f", DXGI_FORMAT_R16G16B16A16_FLOAT, 9, 9, 1, 1, 1},
                          {"volume", DXGI_FORMAT_R8G8B8A8_UNORM, 5, 6, 4, 1, 1},
                          {"volume r32", DXGI_FORMAT_R32_UINT, 7, 3, 5, 1, 1},
                          {"bc1", DXGI_FORMAT_BC1_UNORM, 20, 12, 1, 1, 1},
                          {"bc3", DXGI_FORMAT_BC3_UNORM, 24, 8, 1, 2, 2},
                          {"bc5", DXGI_FORMAT_BC5_UNORM, 16, 16, 1, 1, 1},
                          {"bc7", DXGI_FORMAT_BC7_UNORM, 12, 8, 1, 1, 1}};
    for (const Case& c : cases) {
        const bool volume = c.depth > 1;
        const bool block = FormatCodec::is_block(c.format);
        const int element = FormatCodec::element_bytes(c.format);
        auto make = [&](D3D11_USAGE usage, UINT cpu, Com<ID3D11Texture2D>& t2, Com<ID3D11Texture3D>& t3) {
            if (volume) {
                D3D11_TEXTURE3D_DESC desc{};
                desc.Width = c.width;
                desc.Height = c.height;
                desc.Depth = c.depth;
                desc.MipLevels = c.mips;
                desc.Format = c.format;
                desc.Usage = usage;
                desc.CPUAccessFlags = cpu;
                return device.device->CreateTexture3D(&desc, nullptr, t3.put());
            }
            return device.device->CreateTexture2D(&(const D3D11_TEXTURE2D_DESC&)D3d11Tools::desc2d(c.width, c.height, c.format, 0, usage, cpu, c.mips, c.array_size), nullptr, t2.put());
        };
        Com<ID3D11Texture2D> write2;
        Com<ID3D11Texture3D> write3;
        Com<ID3D11Texture2D> device2;
        Com<ID3D11Texture3D> device3;
        if (FAILED(make(D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE, write2, write3)) || FAILED(make(D3D11_USAGE_DEFAULT, 0, device2, device3))) {
            problems.add(std::string(c.name) + ": the textures cannot be created");
            continue;
        }
        ID3D11Resource* write_resource = volume ? static_cast<ID3D11Resource*>(write3.get()) : static_cast<ID3D11Resource*>(write2.get());
        ID3D11Resource* device_resource = volume ? static_cast<ID3D11Resource*>(device3.get()) : static_cast<ID3D11Resource*>(device2.get());
        const UINT subresources = volume ? c.mips : c.mips * c.array_size;
        std::vector<std::vector<std::uint8_t>> expected(subresources);
        for (UINT s = 0; s < subresources; ++s) {
            const UINT mip = s % c.mips;
            const UINT w = std::max(1u, c.width >> mip);
            const UINT h = std::max(1u, c.height >> mip);
            const UINT d = volume ? std::max(1u, c.depth >> mip) : 1;
            const UINT row_bytes = block ? ((w + 3) / 4) * static_cast<UINT>(element) : w * static_cast<UINT>(element);
            const UINT rows = block ? (h + 3) / 4 : h;
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(device.context->Map(write_resource, s, D3D11_MAP_WRITE, 0, &mapped))) {
                problems.add(std::string(c.name) + ": mapping subresource " + std::to_string(s) + " failed");
                break;
            }
            if (mapped.RowPitch < row_bytes) {
                problems.add(std::string(c.name) + ": RowPitch " + std::to_string(mapped.RowPitch) + " is below the row size " + std::to_string(row_bytes));
            }
            if (volume && d > 1 && mapped.DepthPitch < mapped.RowPitch * rows) {
                problems.add(std::string(c.name) + ": DepthPitch " + std::to_string(mapped.DepthPitch) + " is below RowPitch times rows");
            }
            expected[s].resize(static_cast<std::size_t>(row_bytes) * rows * d);
            Pattern::fill(1000 + s, 0, expected[s].data(), expected[s].size());
            for (UINT z = 0; z < d; ++z) {
                for (UINT r = 0; r < rows; ++r) {
                    std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + static_cast<std::size_t>(z) * mapped.DepthPitch + static_cast<std::size_t>(r) * mapped.RowPitch,
                                expected[s].data() + (static_cast<std::size_t>(z) * rows + r) * row_bytes, row_bytes);
                }
            }
            device.context->Unmap(write_resource, s);
        }
        device.context->CopyResource(device_resource, write_resource);
        for (UINT s = 0; s < subresources; ++s) {
            TexelImage image;
            const bool ok = volume ? D3d11Tools::read_volume(device, device3.get(), s, image, error) : D3d11Tools::read_texture(device, device2.get(), s, image, error);
            if (!ok) {
                problems.add(std::string(c.name) + ": " + error);
                break;
            }
            if (block) {
                for (std::size_t i = 0; i < image.data.size() && i < expected[s].size(); ++i) {
                    if (image.data[i] != expected[s][i]) {
                        problems.add(std::string(c.name) + ": subresource " + std::to_string(s) + " byte " + std::to_string(i) + " differs after the round trip");
                        break;
                    }
                }
            } else if (image.data != expected[s]) {
                problems.add(std::string(c.name) + ": subresource " + std::to_string(s) + " differs after the round trip");
            }
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome MapChecks::errors(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    struct Case {
        std::string name;
        bool must_fail;
        std::function<HRESULT(D3d11Device&)> run;
    };
    auto staging = [](D3d11Device& d, UINT cpu) {
        return D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, cpu));
    };
    std::vector<Case> cases;
    cases.push_back({"double map", true, [=](D3d11Device& d) {
        auto t = staging(d, D3D11_CPU_ACCESS_READ);
        D3D11_MAPPED_SUBRESOURCE m{};
        d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &m);
        const HRESULT second = d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &m);
        d.context->Unmap(t.get(), 0);
        D3D11_MAPPED_SUBRESOURCE again{};
        if (FAILED(d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &again))) {
            return E_FAIL;
        }
        d.context->Unmap(t.get(), 0);
        return second;
    }});
    cases.push_back({"map read on default", true, [=](D3d11Device& d) {
        auto t = D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE));
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &m);
    }});
    cases.push_back({"map write on default buffer", true, [=](D3d11Device& d) {
        auto b = D3d11Tools::buffer(d, 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(b.get(), 0, D3D11_MAP_WRITE, 0, &m);
    }});
    cases.push_back({"map write on immutable", true, [=](D3d11Device& d) {
        std::vector<std::uint8_t> data(64, 1);
        auto b = D3d11Tools::buffer(d, 64, D3D11_USAGE_IMMUTABLE, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, data.data());
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(b.get(), 0, D3D11_MAP_WRITE, 0, &m);
    }});
    cases.push_back({"map of a multisampled texture", true, [=](D3d11Device& d) {
        auto t = D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, 4));
        if (!t) {
            return E_INVALIDARG;
        }
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &m);
    }});
    cases.push_back({"write map on a read-only staging texture", true, [=](D3d11Device& d) {
        auto t = staging(d, D3D11_CPU_ACCESS_READ);
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(t.get(), 0, D3D11_MAP_WRITE, 0, &m);
    }});
    cases.push_back({"read map on a write-only staging texture", true, [=](D3d11Device& d) {
        auto t = staging(d, D3D11_CPU_ACCESS_WRITE);
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &m);
    }});
    cases.push_back({"read map on a dynamic buffer", true, [=](D3d11Device& d) {
        auto b = D3d11Tools::buffer(d, 64, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(b.get(), 0, D3D11_MAP_READ, 0, &m);
    }});
    cases.push_back({"plain write map on a dynamic buffer", true, [=](D3d11Device& d) {
        auto b = D3d11Tools::buffer(d, 64, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(b.get(), 0, D3D11_MAP_WRITE, 0, &m);
    }});
    cases.push_back({"no-overwrite on a dynamic texture", true, [=](D3d11Device& d) {
        auto t = D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE));
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(t.get(), 0, D3D11_MAP_WRITE_NO_OVERWRITE, 0, &m);
    }});
    cases.push_back({"map of a subresource that does not exist", true, [=](D3d11Device& d) {
        auto t = staging(d, D3D11_CPU_ACCESS_READ);
        D3D11_MAPPED_SUBRESOURCE m{};
        return d.context->Map(t.get(), 5, D3D11_MAP_READ, 0, &m);
    }});
    cases.push_back({"unmap without map", false, [=](D3d11Device& d) {
        auto t = staging(d, D3D11_CPU_ACCESS_READ);
        d.context->Unmap(t.get(), 0);
        D3D11_MAPPED_SUBRESOURCE m{};
        const HRESULT later = d.context->Map(t.get(), 0, D3D11_MAP_READ, 0, &m);
        if (SUCCEEDED(later)) {
            d.context->Unmap(t.get(), 0);
        }
        return later;
    }});
    for (const Case& c : cases) {
        const HRESULT a = c.run(rig.av);
        const HRESULT w = c.run(rig.warp);
        if (c.must_fail && SUCCEEDED(a)) {
            problems.add(c.name + " succeeded");
        } else if (!c.must_fail && FAILED(a)) {
            problems.add(c.name + " left the device unable to map: " + D3d11Tools::hr(a));
        } else if (a != w) {
            problems.add(c.name + " returned " + D3d11Tools::hr(a) + " where WARP returns " + D3d11Tools::hr(w));
        }
    }
    context.measure("cases", static_cast<long long>(cases.size()));
    Com<ID3D11Texture2D> t = staging(rig.av, D3D11_CPU_ACCESS_WRITE | D3D11_CPU_ACCESS_READ);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (!t || FAILED(rig.av.context->Map(t.get(), 0, D3D11_MAP_READ_WRITE, 0, &m))) {
        problems.add("mapping does not work after the error cases");
    } else {
        rig.av.context->Unmap(t.get(), 0);
    }
    return problems.outcome();
}

Outcome MapChecks::persistent_across_frames(Context& context) {
    SwapRig rig;
    std::string error;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    if (!rig.open(options, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = rig.device.context;
    constexpr UINT kSize = 32;
    Problems problems;
    Com<ID3D11Texture2D> held = D3d11Tools::texture(rig.device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_WRITE));
    Com<ID3D11Texture2D> device_copy = D3d11Tools::texture(rig.device, D3d11Tools::desc2d(kSize, kSize, DXGI_FORMAT_R8G8B8A8_UNORM, 0));
    if (!held || !device_copy) {
        return Outcome::fail("staging or default textures cannot be created");
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(ctx->Map(held.get(), 0, D3D11_MAP_WRITE, 0, &mapped))) {
        return Outcome::fail("the staging texture cannot be mapped for writing");
    }
    std::vector<std::uint32_t> pattern(kSize * kSize);
    for (UINT y = 0; y < kSize; ++y) {
        for (UINT x = 0; x < kSize; ++x) {
            pattern[y * kSize + x] = static_cast<std::uint32_t>(Pattern::pixel(8, x, y));
            std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch + x * 4, &pattern[y * kSize + x], 4);
        }
    }
    Com<ID3D11PixelShader> flat = Kit::color_ps(rig.device, "0.1, 0.2, 0.3, 1.0", error);
    for (int frame = 0; frame < 12; ++frame) {
        Com<ID3D11RenderTargetView> view = rig.back_view(frame % 2);
        if (!view || !flat) {
            problems.add("a back buffer view is missing");
            break;
        }
        D3d11Tools::clear(rig.device, view.get(), static_cast<float>(frame) / 12.0f, 0, 0, 1);
        static_cast<void>(D3d11Tools::draw_fullscreen(rig.device, flat.get(), view.get(), 128, 128, error));
        const HRESULT presented = rig.chain->Present(1, 0);
        if (FAILED(presented)) {
            problems.add("Present failed while the staging texture was mapped: " + D3d11Tools::hr(presented));
            break;
        }
        for (UINT y = 0; y < kSize; y += 7) {
            if (std::memcmp(static_cast<std::uint8_t*>(mapped.pData) + y * mapped.RowPitch, &pattern[y * kSize], kSize * 4) != 0) {
                problems.add("the mapped memory changed during frame " + std::to_string(frame));
                frame = 12;
                break;
            }
        }
    }
    ctx->Unmap(held.get(), 0);
    ctx->CopyResource(device_copy.get(), held.get());
    TexelImage image;
    if (!D3d11Tools::read_texture(rig.device, device_copy.get(), 0, image, error)) {
        problems.add(error);
    } else {
        std::string why;
        if (!image_equals(image, pattern, why)) {
            problems.add("after Unmap the data copied to the GPU differs: " + why);
        }
    }
    static_cast<void>(context);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
