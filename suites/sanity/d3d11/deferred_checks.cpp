#include "sanity/d3d11/deferred_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/oracle_rig.hpp"
#include "support/pattern.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace lsw::suites::sanity::d3d11 {

namespace {

struct TileScene {
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11Buffer> constants;
    Com<ID3D11Texture2D> target;
    Com<ID3D11RenderTargetView> view;

    [[nodiscard]] bool init(D3d11Device& d, UINT width, UINT height, std::string& error) {
        vs = D3d11Tools::vs(d, D3d11Tools::kFullscreenVs, error);
        ps = D3d11Tools::ps(d, "cbuffer C : register(b0) { float4 col; }\nfloat4 main() : SV_Target { return col; }", error);
        constants = D3d11Tools::buffer(d, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
        target = D3d11Tools::target(d, width, height);
        view = D3d11Tools::rtv(d, target.get());
        return vs && ps && constants && view;
    }

    void bind(ID3D11DeviceContext* c) const {
        ID3D11RenderTargetView* list[1] = {view.get()};
        c->OMSetRenderTargets(1, list, nullptr);
        ID3D11Buffer* raw = constants.get();
        c->PSSetConstantBuffers(0, 1, &raw);
        c->IASetInputLayout(nullptr);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(vs.get(), nullptr, 0);
        c->PSSetShader(ps.get(), nullptr, 0);
    }

    static void tile(ID3D11DeviceContext* c, int i) {
        D3D11_VIEWPORT vp{static_cast<float>(i % 4) * 8.0f, static_cast<float>(i / 4) * 8.0f, 8.0f, 8.0f, 0, 1};
        c->RSSetViewports(1, &vp);
    }

    static std::uint32_t expected(int salt, int i) { return Kit::rgba(16 * (i + 1) + salt, 255 - 8 * i, (i * 29 + salt) & 255, 255); }

    static void color(int salt, int i, float out[4]) {
        out[0] = static_cast<float>(16 * (i + 1) + salt) / 255.0f;
        out[1] = static_cast<float>(255 - 8 * i) / 255.0f;
        out[2] = static_cast<float>((i * 29 + salt) & 255) / 255.0f;
        out[3] = 1.0f;
    }
};

}  // namespace

Outcome DeferredChecks::equivalence(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    TileScene scene;
    if (!scene.init(device, 32, 16, error)) {
        return Outcome::fail(error);
    }
    auto record = [&](ID3D11DeviceContext* c, int first, int last) {
        scene.bind(c);
        for (int i = first; i < last; ++i) {
            float color[4];
            TileScene::color(0, i, color);
            c->UpdateSubresource(scene.constants.get(), 0, nullptr, color, 0, 0);
            TileScene::tile(c, i);
            c->Draw(3, 0);
        }
    };
    D3d11Tools::clear(device, scene.view.get(), 0, 0, 0, 1);
    record(device.context, 0, 8);
    TexelImage immediate;
    if (!D3d11Tools::read_texture(device, scene.target.get(), 0, immediate, error)) {
        return Outcome::fail(error);
    }
    D3d11Tools::clear(device, scene.view.get(), 0, 0, 0, 1);
    Com<ID3D11DeviceContext> first;
    Com<ID3D11DeviceContext> second;
    if (FAILED(device.device->CreateDeferredContext(0, first.put())) || FAILED(device.device->CreateDeferredContext(0, second.put()))) {
        return Outcome::fail("CreateDeferredContext failed");
    }
    record(first.get(), 0, 4);
    record(second.get(), 4, 8);
    Com<ID3D11CommandList> list_a;
    Com<ID3D11CommandList> list_b;
    if (FAILED(first->FinishCommandList(FALSE, list_a.put())) || FAILED(second->FinishCommandList(FALSE, list_b.put()))) {
        return Outcome::fail("FinishCommandList failed");
    }
    device.context->ExecuteCommandList(list_a.get(), FALSE);
    device.context->ExecuteCommandList(list_b.get(), FALSE);
    TexelImage deferred;
    if (!D3d11Tools::read_texture(device, scene.target.get(), 0, deferred, error)) {
        return Outcome::fail(error);
    }
    if (deferred.data != immediate.data) {
        SceneImage a;
        SceneImage b;
        a.width = b.width = 32;
        a.height = b.height = 16;
        a.rgba = b.rgba = true;
        a.data = immediate.data;
        b.data = deferred.data;
        std::string detail;
        static_cast<void>(OracleRig::judge(context, "deferred_equivalence", a, b, Tolerance::exact(), detail));
        problems.add("a scene recorded on two deferred contexts differs from the same calls on the immediate context: " + detail);
    }
    for (int i = 0; i < 8; ++i) {
        if (deferred.pixel(static_cast<UINT>(i % 4) * 8 + 3, static_cast<UINT>(i / 4) * 8 + 3) != TileScene::expected(0, i)) {
            problems.add("tile " + std::to_string(i) + " of the deferred result is wrong");
            break;
        }
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome DeferredChecks::maps(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    TileScene scenes[2];
    Com<ID3D11Buffer> dynamic = D3d11Tools::buffer(device, 16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
    for (TileScene& s : scenes) {
        if (!s.init(device, 32, 16, error)) {
            return Outcome::fail(error);
        }
        s.constants = Com<ID3D11Buffer>(dynamic.get());
        dynamic.get()->AddRef();
    }
    Com<ID3D11CommandList> lists[2];
    Com<ID3D11DeviceContext> deferred[2];
    for (int l = 0; l < 2; ++l) {
        if (FAILED(device.device->CreateDeferredContext(0, deferred[l].put()))) {
            return Outcome::fail("CreateDeferredContext failed");
        }
        scenes[l].bind(deferred[l].get());
        for (int i = 0; i < 8; ++i) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(deferred[l]->Map(dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                return Outcome::fail("Map(WRITE_DISCARD) on a deferred context failed");
            }
            float color[4];
            TileScene::color(l == 0 ? 0 : 5, i, color);
            std::memcpy(mapped.pData, color, 16);
            deferred[l]->Unmap(dynamic.get(), 0);
            TileScene::tile(deferred[l].get(), i);
            deferred[l]->Draw(3, 0);
        }
        if (FAILED(deferred[l]->FinishCommandList(FALSE, lists[l].put()))) {
            return Outcome::fail("FinishCommandList failed");
        }
    }
    for (int s = 0; s < 2; ++s) {
        D3d11Tools::clear(device, scenes[s].view.get(), 0, 0, 0, 1);
    }
    device.context->ExecuteCommandList(lists[0].get(), FALSE);
    device.context->ExecuteCommandList(lists[1].get(), FALSE);
    device.context->ExecuteCommandList(lists[0].get(), FALSE);
    for (int s = 0; s < 2; ++s) {
        TexelImage image;
        if (!D3d11Tools::read_texture(device, scenes[s].target.get(), 0, image, error)) {
            return Outcome::fail(error);
        }
        for (int i = 0; i < 8; ++i) {
            const std::uint32_t got = image.pixel(static_cast<UINT>(i % 4) * 8 + 3, static_cast<UINT>(i / 4) * 8 + 3);
            const std::uint32_t want = TileScene::expected(s == 0 ? 0 : 5, i);
            if (got != want) {
                problems.add("constant buffer updates of list " + std::to_string(s) + " alias: tile " + std::to_string(i) + " is 0x" + Text::hex(got, 8) + ", expected 0x" + Text::hex(want, 8));
                break;
            }
        }
    }
    {
        const char* vs_source =
            "struct I { float2 p : POSITION; float4 c : COLOR; }; struct O { float4 p : SV_Position; float4 c : COLOR; };\n"
            "O main(I i) { O o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }";
        Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, vs_source, error);
        const std::vector<std::uint8_t> code = D3d11Tools::vs_code(device, vs_source, error);
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "float4 main(float4 p : SV_Position, float4 c : COLOR) : SV_Target { return c; }", error);
        const D3D11_INPUT_ELEMENT_DESC elements[2] = {{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
                                                      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0}};
        Com<ID3D11InputLayout> layout;
        Com<ID3D11Texture2D> target = D3d11Tools::target(device, 16, 1);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
        Com<ID3D11Buffer> vb = D3d11Tools::buffer(device, 12 * 64, D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0, nullptr);
        if (!vs || !ps || code.empty() || !view || !vb || FAILED(device.device->CreateInputLayout(elements, 2, code.data(), code.size(), layout.put()))) {
            return Outcome::fail("vertex scene: " + error);
        }
        Com<ID3D11DeviceContext> c;
        device.device->CreateDeferredContext(0, c.put());
        ID3D11RenderTargetView* list[1] = {view.get()};
        c->OMSetRenderTargets(1, list, nullptr);
        D3D11_VIEWPORT vp{0, 0, 16, 1, 0, 1};
        c->RSSetViewports(1, &vp);
        c->IASetInputLayout(layout.get());
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        c->VSSetShader(vs.get(), nullptr, 0);
        c->PSSetShader(ps.get(), nullptr, 0);
        ID3D11Buffer* raw = vb.get();
        UINT stride = 12;
        UINT offset = 0;
        c->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
        for (UINT i = 0; i < 16; ++i) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(c->Map(vb.get(), 0, i == 0 ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped))) {
                return Outcome::fail("a deferred Map of the vertex buffer failed at append " + std::to_string(i));
            }
            struct Vertex {
                float x, y;
                std::uint32_t color;
            } v{(static_cast<float>(i) + 0.5f) / 16.0f * 2.0f - 1.0f, 0.0f, Kit::rgba(static_cast<int>(i * 10), static_cast<int>(i), 200, 255)};
            std::memcpy(static_cast<std::uint8_t*>(mapped.pData) + i * 12, &v, 12);
            c->Unmap(vb.get(), 0);
            c->Draw(1, i);
        }
        Com<ID3D11CommandList> command_list;
        c->FinishCommandList(FALSE, command_list.put());
        D3d11Tools::clear(device, view.get(), 0, 0, 0, 1);
        device.context->ExecuteCommandList(command_list.get(), FALSE);
        TexelImage image;
        if (!D3d11Tools::read_texture(device, target.get(), 0, image, error)) {
            return Outcome::fail(error);
        }
        for (UINT i = 0; i < 16; ++i) {
            if (image.pixel(i, 0) != Kit::rgba(static_cast<int>(i * 10), static_cast<int>(i), 200, 255)) {
                problems.add("deferred WRITE_NO_OVERWRITE append " + std::to_string(i) + " rendered wrongly");
                break;
            }
        }
    }
    {
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        Com<ID3D11DeviceContext> c;
        device.device->CreateDeferredContext(0, c.put());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = c->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (SUCCEEDED(result)) {
            problems.add("Map(READ) on a deferred context succeeded");
            c->Unmap(staging.get(), 0);
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome DeferredChecks::list_reuse(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    TileScene scene;
    if (!scene.init(device, 32, 16, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11DeviceContext> deferred;
    device.device->CreateDeferredContext(0, deferred.put());
    scene.bind(deferred.get());
    deferred->ClearRenderTargetView(scene.view.get(), std::array<float, 4>{0, 0, 1, 1}.data());
    for (int i = 0; i < 8; ++i) {
        float color[4];
        TileScene::color(3, i, color);
        deferred->UpdateSubresource(scene.constants.get(), 0, nullptr, color, 0, 0);
        TileScene::tile(deferred.get(), i);
        deferred->Draw(3, 0);
    }
    Com<ID3D11CommandList> list;
    if (FAILED(deferred->FinishCommandList(FALSE, list.put()))) {
        return Outcome::fail("FinishCommandList failed");
    }
    Com<ID3D11Texture2D> other = D3d11Tools::target(device, 32, 16);
    Com<ID3D11RenderTargetView> other_view = D3d11Tools::rtv(device, other.get());
    TexelImage first;
    for (int round = 0; round < 5; ++round) {
        if (round == 3) {
            ID3D11RenderTargetView* list_other[1] = {other_view.get()};
            device.context->OMSetRenderTargets(1, list_other, nullptr);
            D3D11_BLEND_DESC blend{};
            blend.RenderTarget[0].RenderTargetWriteMask = 0;
            Com<ID3D11BlendState> mask;
            device.device->CreateBlendState(&blend, mask.put());
            device.context->OMSetBlendState(mask.get(), nullptr, 0xffffffff);
        }
        D3d11Tools::clear(device, scene.view.get(), 1, 0, 0, 1);
        device.context->ExecuteCommandList(list.get(), TRUE);
        if (round == 1) {
            device.context->ExecuteCommandList(list.get(), TRUE);
        }
        TexelImage image;
        if (!D3d11Tools::read_texture(device, scene.target.get(), 0, image, error)) {
            return Outcome::fail(error);
        }
        if (round == 0) {
            first = image;
            for (int i = 0; i < 8; ++i) {
                if (image.pixel(static_cast<UINT>(i % 4) * 8 + 3, static_cast<UINT>(i / 4) * 8 + 3) != TileScene::expected(3, i)) {
                    problems.add("tile " + std::to_string(i) + " of the first execution is wrong");
                    break;
                }
            }
        } else if (image.data != first.data) {
            problems.add("execution " + std::to_string(round) + " of the same command list differs from the first");
        }
    }
    device.context->ClearState();
    ID3D11RenderTargetView* list_other[1] = {other_view.get()};
    device.context->OMSetRenderTargets(1, list_other, nullptr);
    device.context->ExecuteCommandList(list.get(), TRUE);
    ID3D11RenderTargetView* bound = nullptr;
    device.context->OMGetRenderTargets(1, &bound, nullptr);
    if (bound != other_view.get()) {
        problems.add("ExecuteCommandList(TRUE) did not restore the render target of the immediate context");
    }
    if (bound != nullptr) {
        bound->Release();
    }
    device.context->ExecuteCommandList(list.get(), FALSE);
    bound = nullptr;
    device.context->OMGetRenderTargets(1, &bound, nullptr);
    if (bound != nullptr) {
        problems.add("ExecuteCommandList(FALSE) left a render target bound on the immediate context");
        bound->Release();
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome DeferredChecks::clearstate(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    TileScene scene;
    if (!scene.init(device, 32, 16, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11DeviceContext> deferred;
    device.device->CreateDeferredContext(0, deferred.put());
    auto bound_target = [&](ID3D11DeviceContext* c) {
        ID3D11RenderTargetView* bound = nullptr;
        c->OMGetRenderTargets(1, &bound, nullptr);
        const bool result = bound != nullptr;
        if (bound != nullptr) {
            bound->Release();
        }
        return result;
    };
    scene.bind(deferred.get());
    TileScene::tile(deferred.get(), 2);
    float color[4];
    TileScene::color(9, 2, color);
    deferred->UpdateSubresource(scene.constants.get(), 0, nullptr, color, 0, 0);
    deferred->Draw(3, 0);
    Com<ID3D11CommandList> first;
    if (FAILED(deferred->FinishCommandList(FALSE, first.put()))) {
        return Outcome::fail("FinishCommandList(FALSE) failed");
    }
    if (bound_target(deferred.get())) {
        problems.add("a deferred context keeps its render target after FinishCommandList(FALSE)");
    }
    scene.bind(deferred.get());
    Com<ID3D11CommandList> kept;
    if (FAILED(deferred->FinishCommandList(TRUE, kept.put()))) {
        return Outcome::fail("FinishCommandList(TRUE) failed");
    }
    if (!bound_target(deferred.get())) {
        problems.add("a deferred context loses its render target after FinishCommandList(TRUE)");
    }
    deferred->ClearState();
    if (bound_target(deferred.get())) {
        problems.add("ClearState does not unbind the render target of a deferred context");
    }
    Com<ID3D11CommandList> empty;
    if (FAILED(deferred->FinishCommandList(FALSE, empty.put()))) {
        return Outcome::fail("FinishCommandList after ClearState failed");
    }
    D3d11Tools::clear(device, scene.view.get(), 0, 0, 0, 1);
    deferred.reset();
    device.context->ExecuteCommandList(first.get(), FALSE);
    device.context->ExecuteCommandList(empty.get(), FALSE);
    TexelImage image;
    if (!D3d11Tools::read_texture(device, scene.target.get(), 0, image, error)) {
        return Outcome::fail(error);
    }
    if (image.pixel(2 * 8 + 3, 3) != TileScene::expected(9, 2)) {
        problems.add("a command list recorded before its deferred context was released renders wrongly");
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome DeferredChecks::free_threaded(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::mutex problems_mutex;
    std::vector<std::uint8_t> code;
    if (!D3d11Tools::compile("float4 main(float4 p : SV_Position) : SV_Target { return float4(0.25, 0.5, 0.75, 1); }", "main", D3d11Tools::profile(device, "ps"), code, error)) {
        return Outcome::fail(error);
    }
    constexpr int kThreads = 8;
    constexpr int kEach = 25;
    struct Made {
        std::vector<Com<ID3D11Texture2D>> textures;
        std::vector<Com<ID3D11Buffer>> buffers;
        std::vector<Com<ID3D11ShaderResourceView>> views;
        std::vector<Com<ID3D11PixelShader>> shaders;
    };
    std::vector<Made> made(kThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kEach; ++i) {
                Com<ID3D11Texture2D> tex = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE));
                Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 256, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr);
                Com<ID3D11ShaderResourceView> view = D3d11Tools::srv(device, tex.get());
                Com<ID3D11PixelShader> shader;
                device.device->CreatePixelShader(code.data(), code.size(), nullptr, shader.put());
                if (!tex || !buffer || !view || !shader) {
                    std::lock_guard lock(problems_mutex);
                    problems.add("thread " + std::to_string(t) + " creation " + std::to_string(i) + " failed");
                    return;
                }
                made[static_cast<std::size_t>(t)].textures.push_back(std::move(tex));
                made[static_cast<std::size_t>(t)].buffers.push_back(std::move(buffer));
                made[static_cast<std::size_t>(t)].views.push_back(std::move(view));
                made[static_cast<std::size_t>(t)].shaders.push_back(std::move(shader));
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    for (int t = 0; t < kThreads && problems.empty(); ++t) {
        const Made& m = made[static_cast<std::size_t>(t)];
        for (int i = 0; i < kEach && static_cast<std::size_t>(i) < m.textures.size(); ++i) {
            std::vector<std::uint8_t> pattern(16 * 16 * 4);
            Pattern::fill(static_cast<std::uint64_t>(t * 100 + i), 0, pattern.data(), pattern.size());
            device.context->UpdateSubresource(m.textures[static_cast<std::size_t>(i)].get(), 0, nullptr, pattern.data(), 64, 0);
            TexelImage image;
            if (!D3d11Tools::read_texture(device, m.textures[static_cast<std::size_t>(i)].get(), 0, image, error) || image.data != pattern) {
                problems.add("a texture created on thread " + std::to_string(t) + " does not round trip data");
                break;
            }
            std::vector<std::uint8_t> bytes(256);
            Pattern::fill(static_cast<std::uint64_t>(t * 100 + i + 7), 0, bytes.data(), 256);
            device.context->UpdateSubresource(m.buffers[static_cast<std::size_t>(i)].get(), 0, nullptr, bytes.data(), 0, 0);
            std::vector<std::uint8_t> back;
            if (!D3d11Tools::read_buffer(device, m.buffers[static_cast<std::size_t>(i)].get(), back, error) || back != bytes) {
                problems.add("a buffer created on thread " + std::to_string(t) + " does not round trip data");
                break;
            }
        }
        if (!m.shaders.empty()) {
            D3d11Tools::clear(device, view.get(), 0, 0, 0, 0);
            if (!D3d11Tools::draw_fullscreen(device, m.shaders.back().get(), view.get(), 4, 4, error)) {
                return Outcome::fail(error);
            }
            TexelImage image;
            if (!D3d11Tools::read_texture(device, target.get(), 0, image, error) || image.pixel(1, 1) != Kit::rgba(64, 128, 191, 255)) {
                problems.add("a shader created on thread " + std::to_string(t) + " renders wrongly");
            }
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome DeferredChecks::multithread_protect(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    Com<ID3D11Multithread> multithread = Kit::query<ID3D11Multithread>(device.context);
    if (!multithread) {
        return Outcome::fail("ID3D11Multithread is not available on the immediate context");
    }
    const BOOL initial = multithread->GetMultithreadProtected();
    const BOOL previous = multithread->SetMultithreadProtected(TRUE);
    if (previous != initial) {
        problems.add("SetMultithreadProtected returned " + std::to_string(previous) + " but the setting was " + std::to_string(initial));
    }
    if (multithread->GetMultithreadProtected() != TRUE) {
        problems.add("GetMultithreadProtected is not TRUE after enabling protection");
    }
    const BOOL again = multithread->SetMultithreadProtected(FALSE);
    if (again != TRUE) {
        problems.add("SetMultithreadProtected(FALSE) returned " + std::to_string(again) + " instead of the previous TRUE");
    }
    if (multithread->GetMultithreadProtected() != FALSE) {
        problems.add("GetMultithreadProtected is not FALSE after disabling protection");
    }
    multithread->SetMultithreadProtected(TRUE);
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(device, "cbuffer C : register(b0) { float4 col; }\nfloat4 main() : SV_Target { return col; }", error);
    if (!vs || !ps) {
        return Outcome::fail(error);
    }
    std::mutex problems_mutex;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            std::string local;
            Com<ID3D11Texture2D> target = D3d11Tools::target(device, 8, 8);
            Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
            Com<ID3D11Buffer> cb = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
            for (int i = 0; i < 100; ++i) {
                const int r = (t * 60 + i) & 255;
                const float color[4] = {static_cast<float>(r) / 255.0f, static_cast<float>(255 - r) / 255.0f, static_cast<float>(t * 50) / 255.0f, 1.0f};
                multithread->Enter();
                device.context->UpdateSubresource(cb.get(), 0, nullptr, color, 0, 0);
                ID3D11Buffer* raw = cb.get();
                device.context->PSSetConstantBuffers(0, 1, &raw);
                static_cast<void>(D3d11Tools::draw_fullscreen(device, ps.get(), view.get(), 8, 8, local));
                TexelImage image;
                const bool read = D3d11Tools::read_texture(device, target.get(), 0, image, local);
                multithread->Leave();
                if (!read || image.pixel(3, 3) != Kit::rgba(r, 255 - r, t * 50, 255)) {
                    std::lock_guard lock(problems_mutex);
                    problems.add("thread " + std::to_string(t) + " iteration " + std::to_string(i) + " rendered or read back wrongly " + local);
                    return;
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome DeferredChecks::two_devices(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::mutex problems_mutex;
    std::atomic<bool> stop_first{false};
    std::atomic<bool> stop_second{false};
    std::atomic<long> second_after_first_gone{0};
    std::atomic<bool> first_gone{false};
    auto worker = [&](int id, std::atomic<bool>& stop) {
        D3d11Device* device = new D3d11Device();
        std::string local;
        if (!device->create(adapter.luid, local)) {
            std::lock_guard lock(problems_mutex);
            problems.add("device " + std::to_string(id) + ": " + local);
            delete device;
            return;
        }
        Com<ID3D11Texture2D> target = D3d11Tools::target(*device, 16, 16);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(*device, target.get());
        Com<ID3D11PixelShader> ps = D3d11Tools::ps(*device, "float4 main() : SV_Target { return float4(0, 1, 0, 1); }", local);
        long iteration = 0;
        while (!stop.load()) {
            const float v = static_cast<float>(iteration % 200) / 255.0f;
            D3d11Tools::clear(*device, view.get(), v, 0, 0, 1);
            TexelImage image;
            if (!D3d11Tools::read_texture(*device, target.get(), 0, image, local) || image.pixel(5, 5) != Kit::rgba(static_cast<int>(iteration % 200), 0, 0, 255)) {
                std::lock_guard lock(problems_mutex);
                problems.add("device " + std::to_string(id) + " rendered wrongly at iteration " + std::to_string(iteration) + " " + local);
                break;
            }
            if (ps && D3d11Tools::draw_fullscreen(*device, ps.get(), view.get(), 16, 16, local)) {
                if (D3d11Tools::read_texture(*device, target.get(), 0, image, local) && image.pixel(2, 2) != Kit::rgba(0, 255, 0, 255)) {
                    std::lock_guard lock(problems_mutex);
                    problems.add("device " + std::to_string(id) + " drew wrongly at iteration " + std::to_string(iteration));
                    break;
                }
            }
            ++iteration;
            if (id == 2 && first_gone.load()) {
                ++second_after_first_gone;
            }
        }
        view.reset();
        target.reset();
        ps.reset();
        delete device;
        if (id == 1) {
            first_gone.store(true);
        }
    };
    std::thread first(worker, 1, std::ref(stop_first));
    std::thread second(worker, 2, std::ref(stop_second));
    ::Sleep(400);
    stop_first.store(true);
    first.join();
    ::Sleep(800);
    stop_second.store(true);
    second.join();
    context.measure("second_iterations_after_first_destroyed", second_after_first_gone.load());
    if (second_after_first_gone.load() < 3) {
        problems.add("the second device made no progress after the first one was destroyed");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
