#include "render/d3d11/draw2d.hpp"

#include <thread>

namespace lsw::suites::render::d3d11 {

namespace {

struct Scene3d {
    Target target;
    Depth depth;
    CubePipe cubes;
    Com<ID3D11DepthStencilState> ds;
    bool make(Rig& r) {
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        return r.color(target) && r.depth(depth) && cubes.make(r) && ds;
    }
    void state(ID3D11DeviceContext* c) {
        ID3D11RenderTargetView* rtv = target.view.get();
        c->OMSetRenderTargets(1, &rtv, depth.view.get());
        D3D11_VIEWPORT vp{0, 0, 128, 128, 0, 1};
        c->RSSetViewports(1, &vp);
        const UINT stride = sizeof(rr::ColoredVertex), offset = 0;
        ID3D11Buffer* vb = cubes.vb_.get();
        ID3D11Buffer* cb = cubes.cb_.get();
        c->IASetInputLayout(cubes.il_.get());
        c->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        c->IASetIndexBuffer(cubes.ib_.get(), DXGI_FORMAT_R16_UINT, 0);
        c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        c->VSSetShader(cubes.vs_.get(), nullptr, 0);
        c->VSSetConstantBuffers(0, 1, &cb);
        c->PSSetShader(cubes.ps_.get(), nullptr, 0);
        c->RSSetState(cubes.rs_.get());
        c->OMSetDepthStencilState(ds.get(), 0);
    }
    void clear(Rig& r) {
        const float color[4] = {0.06f, 0.07f, 0.12f, 1};
        r.ctx->ClearRenderTargetView(target.view.get(), color);
        r.ctx->ClearDepthStencilView(depth.view.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    }
};

rr::Mat4 placement(int k) {
    const float x = -0.75f + 1.5f * static_cast<float>(k % 2);
    const float y = 0.55f - 1.1f * static_cast<float>(k / 2);
    return CubePipe::camera() * rr::Mat4::translate(x, y, 0.2f * static_cast<float>(k)) * rr::Mat4::rotate_y(0.4f + 0.5f * static_cast<float>(k)) * rr::Mat4::rotate_x(0.3f) * rr::Mat4::scale(0.6f, 0.6f, 0.6f);
}

bool deferred_state(Rig& r, Cap& out, bool restore) {
    Scene3d s;
    if (!s.make(r)) return false;
    Com<ID3D11DeviceContext> deferred;
    if (!r.ok(r.dev->CreateDeferredContext(0, deferred.put()), "CreateDeferredContext")) return false;
    s.clear(r);
    s.state(r.ctx);
    r.ctx->UpdateSubresource(s.cubes.cb_.get(), 0, nullptr, placement(0).m, 0, 0);
    r.ctx->DrawIndexed(s.cubes.count_, 0, 0);
    Com<ID3D11CommandList> list;
    s.state(deferred.get());
    const rr::Mat4 m1 = placement(1);
    deferred->UpdateSubresource(s.cubes.cb_.get(), 0, nullptr, m1.m, 0, 0);
    deferred->DrawIndexed(s.cubes.count_, 0, 0);
    if (!r.ok(deferred->FinishCommandList(restore, list.put()), "FinishCommandList")) return false;
    r.ctx->ExecuteCommandList(list.get(), restore);
    r.ctx->UpdateSubresource(s.cubes.cb_.get(), 0, nullptr, placement(2).m, 0, 0);
    r.ctx->DrawIndexed(s.cubes.count_, 0, 0);
    return r.shot(out, s.target);
}
const Add kDeferredRestore(spec("ctx", "deferred_command_list_restore_state_true").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) { return deferred_state(r, out, true); });
const Add kDeferredNoRestore(spec("ctx", "deferred_command_list_restore_state_false").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) { return deferred_state(r, out, false); });

const Add kTwice(spec("ctx", "deferred_command_list_executed_twice_with_new_constants").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    Scene3d s;
    if (!s.make(r)) return false;
    Com<ID3D11DeviceContext> deferred;
    Com<ID3D11CommandList> list;
    if (!r.ok(r.dev->CreateDeferredContext(0, deferred.put()), "CreateDeferredContext")) return false;
    s.state(deferred.get());
    deferred->DrawIndexed(s.cubes.count_, 0, 0);
    if (!r.ok(deferred->FinishCommandList(FALSE, list.put()), "FinishCommandList")) return false;
    s.clear(r);
    for (int k = 0; k < 4; ++k) {
        r.ctx->UpdateSubresource(s.cubes.cb_.get(), 0, nullptr, placement(k).m, 0, 0);
        r.ctx->ExecuteCommandList(list.get(), FALSE);
    }
    return r.shot(out, s.target);
});

const Add kThreads(spec("ctx", "four_deferred_contexts_recorded_on_threads").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    Scene3d s;
    if (!s.make(r)) return false;
    Com<ID3D11Buffer> constants[4];
    Com<ID3D11DeviceContext> contexts[4];
    Com<ID3D11CommandList> lists[4];
    bool failed[4] = {};
    for (int k = 0; k < 4; ++k) {
        constants[k] = r.cbuf(placement(k).m, 64);
        if (!constants[k] || !r.ok(r.dev->CreateDeferredContext(0, contexts[k].put()), "CreateDeferredContext")) return false;
    }
    std::vector<std::thread> workers;
    for (int k = 0; k < 4; ++k) {
        workers.emplace_back([&, k] {
            ID3D11DeviceContext* c = contexts[k].get();
            s.state(c);
            ID3D11Buffer* cb = constants[k].get();
            c->VSSetConstantBuffers(0, 1, &cb);
            c->DrawIndexed(s.cubes.count_, 0, 0);
            failed[k] = FAILED(c->FinishCommandList(FALSE, lists[k].put()));
        });
    }
    for (std::thread& t : workers) t.join();
    for (int k = 0; k < 4; ++k)
        if (failed[k]) return r.fail("FinishCommandList failed on a worker thread");
    s.clear(r);
    for (int k = 0; k < 4; ++k) r.ctx->ExecuteCommandList(lists[k].get(), FALSE);
    return r.shot(out, s.target);
});

const Add kDeferredMap(spec("ctx", "deferred_context_map_discard_dynamic_buffers").tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    Scene3d s;
    Com<ID3D11Buffer> dynamic = r.buf(64, D3D11_BIND_CONSTANT_BUFFER, nullptr, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE);
    if (!s.make(r) || !dynamic) return false;
    Com<ID3D11DeviceContext> deferred;
    Com<ID3D11CommandList> list;
    if (!r.ok(r.dev->CreateDeferredContext(0, deferred.put()), "CreateDeferredContext")) return false;
    s.state(deferred.get());
    ID3D11Buffer* cb = dynamic.get();
    deferred->VSSetConstantBuffers(0, 1, &cb);
    for (int k = 0; k < 4; ++k) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (!r.ok(deferred->Map(dynamic.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m), "deferred Map")) return false;
        const rr::Mat4 mat = placement(k);
        std::memcpy(m.pData, mat.m, 64);
        deferred->Unmap(dynamic.get(), 0);
        deferred->DrawIndexed(s.cubes.count_, 0, 0);
    }
    if (!r.ok(deferred->FinishCommandList(FALSE, list.put()), "FinishCommandList")) return false;
    s.clear(r);
    r.ctx->ExecuteCommandList(list.get(), FALSE);
    return r.shot(out, s.target);
});

const Add kOffsets(spec("ctx", "vs_set_constant_buffers1_offsets").variant(1).tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    Scene3d s;
    if (!s.make(r)) return false;
    ID3D11DeviceContext1* raw = nullptr;
    r.ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&raw));
    Com<ID3D11DeviceContext1> c1(raw);
    if (!c1) return r.fail("ID3D11DeviceContext1 is unavailable");
    Bytes data(4 * 256, 0);
    for (int k = 0; k < 4; ++k) std::memcpy(data.data() + k * 256, placement(k).m, 64);
    Com<ID3D11Buffer> big = r.buf(1024, D3D11_BIND_CONSTANT_BUFFER, data.data());
    if (!big) return false;
    s.clear(r);
    s.state(r.ctx);
    ID3D11Buffer* b = big.get();
    for (int k = 0; k < 4; ++k) {
        const UINT first = static_cast<UINT>(k * 16);
        const UINT count = 16;
        c1->VSSetConstantBuffers1(0, 1, &b, &first, &count);
        r.ctx->DrawIndexed(s.cubes.count_, 0, 0);
    }
    return r.shot(out, s.target);
});

const Add kTexture2D1(spec("ctx", "device3_texture2d1_and_rtv1_plane_slice").variant(1).tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    ID3D11Device3* raw = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&raw));
    Com<ID3D11Device3> dev3(raw);
    if (!dev3) return r.fail("ID3D11Device3 is unavailable");
    D3D11_TEXTURE2D_DESC1 d{};
    d.Width = d.Height = 128;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.TextureLayout = D3D11_TEXTURE_LAYOUT_UNDEFINED;
    Com<ID3D11Texture2D1> tex;
    if (!r.ok(dev3->CreateTexture2D1(&d, nullptr, tex.put()), "CreateTexture2D1")) return false;
    D3D11_RENDER_TARGET_VIEW_DESC1 rd{};
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    rd.Texture2D.MipSlice = 0;
    rd.Texture2D.PlaneSlice = 0;
    Com<ID3D11RenderTargetView1> rtv;
    if (!r.ok(dev3->CreateRenderTargetView1(tex.get(), &rd, rtv.put()), "CreateRenderTargetView1")) return false;
    Basic basic;
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(9, 0.85f));
    if (!basic.make(r) || !vb) return false;
    const float clear[4] = {0.1f, 0.15f, 0.2f, 1};
    ID3D11RenderTargetView* v = rtv.get();
    r.ctx->OMSetRenderTargets(1, &v, nullptr);
    r.ctx->ClearRenderTargetView(v, clear);
    r.viewport(0, 0, 128, 128);
    r.ctx->RSSetState(r.rast(rdesc()).get());
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Draw(27, 0);
    rr::Image image;
    if (!r.read_image(tex.get(), DXGI_FORMAT_R8G8B8A8_UNORM, 0, image)) return false;
    out.add("", std::move(image));
    return true;
}, [](Rig& r) -> std::string {
    ID3D11Device3* raw = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&raw));
    Com<ID3D11Device3> dev3(raw);
    return dev3 ? std::string() : std::string("ID3D11Device3");
});

const Add kFence(spec("ctx", "fence_signal_wait_between_passes").variant(1).tolerance(rr::Tolerance::raster()), [](Rig& r, Cap& out) {
    ID3D11Device5* raw5 = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&raw5));
    Com<ID3D11Device5> dev5(raw5);
    ID3D11DeviceContext4* raw4 = nullptr;
    r.ctx->QueryInterface(__uuidof(ID3D11DeviceContext4), reinterpret_cast<void**>(&raw4));
    Com<ID3D11DeviceContext4> ctx4(raw4);
    if (!dev5 || !ctx4) return r.fail("ID3D11Device5 is unavailable");
    Com<ID3D11Fence> fence;
    if (!r.ok(dev5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), fence.put_void()), "CreateFence")) return false;
    Scene3d s;
    if (!s.make(r)) return false;
    s.clear(r);
    s.state(r.ctx);
    for (int k = 0; k < 2; ++k) {
        r.ctx->UpdateSubresource(s.cubes.cb_.get(), 0, nullptr, placement(k).m, 0, 0);
        r.ctx->DrawIndexed(s.cubes.count_, 0, 0);
    }
    if (!r.ok(ctx4->Signal(fence.get(), 1), "Signal")) return false;
    if (!r.ok(ctx4->Wait(fence.get(), 1), "Wait")) return false;
    for (int k = 2; k < 4; ++k) {
        r.ctx->UpdateSubresource(s.cubes.cb_.get(), 0, nullptr, placement(k).m, 0, 0);
        r.ctx->DrawIndexed(s.cubes.count_, 0, 0);
    }
    r.ctx->Flush();
    if (!r.shot(out, s.target)) return false;
    if (fence->GetCompletedValue() < 1) return r.fail("the fence value did not reach 1 after the work completed");
    return true;
}, [](Rig& r) -> std::string {
    ID3D11Device5* raw = nullptr;
    r.dev->QueryInterface(__uuidof(ID3D11Device5), reinterpret_cast<void**>(&raw));
    Com<ID3D11Device5> dev5(raw);
    return dev5 ? std::string() : std::string("ID3D11Device5 fences");
});

bool tiled(Rig& r, Cap& out, bool leave_unmapped) {
    ID3D11DeviceContext2* raw = nullptr;
    r.ctx->QueryInterface(__uuidof(ID3D11DeviceContext2), reinterpret_cast<void**>(&raw));
    Com<ID3D11DeviceContext2> c2(raw);
    if (!c2) return r.fail("ID3D11DeviceContext2 is unavailable");
    Com<ID3D11Buffer> pool = r.buf(65536 * 4, 0, nullptr, D3D11_USAGE_DEFAULT, 0, D3D11_RESOURCE_MISC_TILE_POOL);
    D3D11_TEXTURE2D_DESC d = D3d11Tools::desc2d(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE);
    d.MiscFlags = D3D11_RESOURCE_MISC_TILED;
    Com<ID3D11Texture2D> tex;
    if (!pool || !r.ok(r.dev->CreateTexture2D(&d, nullptr, tex.put()), "tiled texture")) return false;
    const UINT tile_count = leave_unmapped ? 3 : 4;
    D3D11_TILED_RESOURCE_COORDINATE coords[4];
    D3D11_TILE_REGION_SIZE regions[4];
    UINT range_flags[4] = {};
    UINT offsets[4];
    UINT counts[4];
    for (UINT i = 0; i < tile_count; ++i) {
        coords[i] = {i % 2, i / 2, 0, 0};
        regions[i] = {1, FALSE, 0, 0, 0};
        offsets[i] = i;
        counts[i] = 1;
    }
    for (UINT i = 0; i < tile_count; ++i) {
        if (!r.ok(c2->UpdateTileMappings(tex.get(), 1, &coords[i], &regions[i], pool.get(), 1, &range_flags[i], &offsets[i], &counts[i], 0), "UpdateTileMappings")) return false;
    }
    for (UINT i = 0; i < tile_count; ++i) {
        Bytes tile(65536);
        for (int y = 0; y < 128; ++y)
            for (int x = 0; x < 128; ++x) {
                std::uint8_t* p = &tile[(y * 128 + x) * 4];
                p[0] = static_cast<std::uint8_t>(60 + 50 * i + (x & 16 ? 60 : 0));
                p[1] = static_cast<std::uint8_t>(x * 2);
                p[2] = static_cast<std::uint8_t>(y * 2);
                p[3] = 255;
            }
        c2->UpdateTiles(tex.get(), &coords[i], &regions[i], tile.data(), 0);
    }
    Target target;
    Com<ID3D11PixelShader> ps = r.ps("Texture2D t : register(t0);\nSamplerState s : register(s0);\nstruct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; };\nfloat4 main(VO i) : SV_Target { return t.SampleLevel(s, i.uv, 0); }\n");
    Com<ID3D11SamplerState> smp = r.smp(sdesc(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP));
    Com<ID3D11ShaderResourceView> srv = r.srv(tex.get());
    if (!ps || !smp || !srv || !r.color(target)) return false;
    r.bind(target);
    ID3D11ShaderResourceView* v = srv.get();
    ID3D11SamplerState* sp = smp.get();
    r.ctx->PSSetShaderResources(0, 1, &v);
    r.ctx->PSSetSamplers(0, 1, &sp);
    r.draw_fs(ps.get());
    return r.shot(out, target);
}

std::string tiled_needs(Rig& r, int tier) {
    D3D11_FEATURE_DATA_D3D11_OPTIONS1 o{};
    if (FAILED(r.dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS1, &o, sizeof o)) || static_cast<int>(o.TiledResourcesTier) < tier) return "tiled resources";
    return {};
}
const Add kTiled(spec("ctx", "tiled_texture_update_tile_mappings_and_update_tiles").variant(1).tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) { return tiled(r, out, false); },
                 [](Rig& r) { return tiled_needs(r, 1); });
const Add kTiledUnmapped(spec("ctx", "tiled_texture_unmapped_tile_reads_zero").variant(1).tolerance(rr::Tolerance::tight(1)), [](Rig& r, Cap& out) { return tiled(r, out, true); },
                         [](Rig& r) { return tiled_needs(r, 2); });

}  // namespace

}  // namespace lsw::suites::render::d3d11
