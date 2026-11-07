#include "sanity/d3d11/query_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/oracle_rig.hpp"

#include <chrono>
#include <functional>

namespace lsw::suites::sanity::d3d11 {

namespace {

template <typename T>
bool fetch(D3d11Device& d, ID3D11Query* query, T& out, bool flush_once, std::string& error) {
    if (flush_once) {
        d.context->Flush();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    HRESULT result = S_FALSE;
    while ((result = d.context->GetData(query, &out, sizeof(T), D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            error = "a query did not complete within 30 s after Flush";
            return false;
        }
        ::SwitchToThread();
    }
    if (FAILED(result)) {
        error = "GetData failed: " + D3d11Tools::hr(result);
        return false;
    }
    return true;
}

Com<ID3D11Query> make_query(D3d11Device& d, D3D11_QUERY type) {
    D3D11_QUERY_DESC desc{};
    desc.Query = type;
    Com<ID3D11Query> query;
    d.device->CreateQuery(&desc, query.put());
    return query;
}

}  // namespace

Outcome QueryChecks::occlusion_counts(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, 16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11Texture2D> depth = D3d11Tools::texture(device, D3d11Tools::desc2d(16, 16, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL));
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    Com<ID3D11DepthStencilView> dsv;
    if (depth) {
        device.device->CreateDepthStencilView(depth.get(), nullptr, dsv.put());
    }
    const char* vs_source = "cbuffer C : register(b0) { float z; float3 pad; }\nfloat4 main(uint id : SV_VertexID) : SV_Position { float2 p = float2((id << 1) & 2, id & 2); return float4(p * float2(2, -2) + float2(-1, 1), z, 1); }";
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, vs_source, error);
    Com<ID3D11PixelShader> ps = Kit::color_ps(device, "1, 1, 1, 1", error);
    Com<ID3D11Buffer> constants = D3d11Tools::buffer(device, 16, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr);
    Com<ID3D11Query> samples = make_query(device, D3D11_QUERY_OCCLUSION);
    Com<ID3D11Query> predicate = make_query(device, D3D11_QUERY_OCCLUSION_PREDICATE);
    if (!view || !dsv || !vs || !ps || !constants || !samples || !predicate) {
        return Outcome::fail("scene objects: " + error);
    }
    D3D11_DEPTH_STENCIL_DESC ds{};
    ds.DepthEnable = TRUE;
    ds.DepthFunc = D3D11_COMPARISON_LESS;
    ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    Com<ID3D11DepthStencilState> state;
    device.device->CreateDepthStencilState(&ds, state.put());
    ID3D11Buffer* raw = constants.get();
    ctx->VSSetConstantBuffers(0, 1, &raw);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(vs.get(), nullptr, 0);
    ctx->PSSetShader(ps.get(), nullptr, 0);
    ID3D11RenderTargetView* list[1] = {view.get()};
    ctx->OMSetRenderTargets(1, list, dsv.get());
    ctx->OMSetDepthStencilState(state.get(), 0);
    auto draw = [&](float z, UINT width, ID3D11Query* query) {
        const float data[4] = {z, 0, 0, 0};
        ctx->UpdateSubresource(constants.get(), 0, nullptr, data, 0, 0);
        D3D11_VIEWPORT vp{0, 0, static_cast<float>(width), 16, 0, 1};
        ctx->RSSetViewports(1, &vp);
        ctx->Begin(query);
        ctx->Draw(3, 0);
        ctx->End(query);
    };
    auto count = [&](float z, UINT width, UINT64& out) {
        draw(z, width, samples.get());
        return fetch(device, samples.get(), out, true, error);
    };
    ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    UINT64 value = 0;
    if (!count(0.2f, 16, value)) {
        return Outcome::fail(error);
    }
    if (value != 256) {
        problems.add("a 16x16 quad counted " + std::to_string(value) + " samples, expected 256");
    }
    if (!count(0.8f, 16, value)) {
        return Outcome::fail(error);
    }
    if (value != 0) {
        problems.add("a quad behind the depth buffer counted " + std::to_string(value) + " samples, expected 0");
    }
    ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    if (!count(0.5f, 8, value)) {
        return Outcome::fail(error);
    }
    if (value != 128) {
        problems.add("an 8x16 area counted " + std::to_string(value) + " samples, expected 128");
    }
    BOOL answer = TRUE;
    ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    draw(0.5f, 16, predicate.get());
    if (!fetch(device, predicate.get(), answer, true, error)) {
        return Outcome::fail(error);
    }
    if (answer == FALSE) {
        problems.add("OCCLUSION_PREDICATE is false for a visible quad");
    }
    draw(0.9f, 16, predicate.get());
    if (!fetch(device, predicate.get(), answer, true, error)) {
        return Outcome::fail(error);
    }
    if (answer != FALSE) {
        problems.add("OCCLUSION_PREDICATE is true for a hidden quad");
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome QueryChecks::timestamp_disjoint(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("the compute workloads of this row need feature level 11_0");
    }
    ID3D11DeviceContext* ctx = device.context;
    Com<ID3D11ComputeShader> trivial = D3d11Tools::cs(device, "RWByteAddressBuffer o : register(u0);\n[numthreads(1,1,1)] void main() { o.Store(0, 1); }", error);
    Com<ID3D11ComputeShader> heavy = D3d11Tools::cs(device,
        "RWByteAddressBuffer o : register(u0);\n[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  uint x = o.Load(0) + id.x; [loop] for (uint i = 0; i < 60000; ++i) { x = x * 1664525u + 1013904223u + (x >> 7); }\n  o.Store(4 + id.x * 4, x);\n}\n", error);
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
    Com<ID3D11UnorderedAccessView> uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
    if (!trivial || !heavy || !uav) {
        return Outcome::fail("scene objects: " + error);
    }
    Problems problems;
    ID3D11UnorderedAccessView* list[1] = {uav.get()};
    ctx->CSSetUnorderedAccessViews(0, 1, list, nullptr);
    bool measured = false;
    for (int attempt = 0; attempt < 4 && !measured; ++attempt) {
        Com<ID3D11Query> disjoint = make_query(device, D3D11_QUERY_TIMESTAMP_DISJOINT);
        Com<ID3D11Query> stamps[4] = {make_query(device, D3D11_QUERY_TIMESTAMP), make_query(device, D3D11_QUERY_TIMESTAMP), make_query(device, D3D11_QUERY_TIMESTAMP), make_query(device, D3D11_QUERY_TIMESTAMP)};
        if (!disjoint || !stamps[3]) {
            return Outcome::fail("timestamp queries cannot be created");
        }
        ctx->Begin(disjoint.get());
        ctx->End(stamps[0].get());
        ctx->CSSetShader(trivial.get(), nullptr, 0);
        ctx->Dispatch(1, 1, 1);
        ctx->End(stamps[1].get());
        ctx->CSSetShader(heavy.get(), nullptr, 0);
        ctx->Dispatch(256, 1, 1);
        ctx->End(stamps[2].get());
        ctx->End(disjoint.get());
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT info{};
        UINT64 t[3] = {};
        if (!fetch(device, disjoint.get(), info, true, error)) {
            return Outcome::fail(error);
        }
        for (int i = 0; i < 3; ++i) {
            if (!fetch(device, stamps[i].get(), t[i], false, error)) {
                return Outcome::fail(error);
            }
        }
        if (info.Disjoint) {
            continue;
        }
        measured = true;
        if (info.Frequency == 0) {
            problems.add("the timestamp frequency is zero");
        }
        if (t[1] < t[0] || t[2] < t[1]) {
            problems.add("timestamps inside one interval decrease: " + std::to_string(t[0]) + ", " + std::to_string(t[1]) + ", " + std::to_string(t[2]));
        }
        const UINT64 quick = t[1] - t[0];
        const UINT64 slow = t[2] - t[1];
        context.measure("trivial_ticks", static_cast<long long>(quick));
        context.measure("heavy_ticks", static_cast<long long>(slow));
        context.measure("frequency", static_cast<long long>(info.Frequency));
        if (slow <= quick) {
            problems.add("a long dispatch (" + std::to_string(slow) + " ticks) does not measure longer than a trivial one (" + std::to_string(quick) + " ticks)");
        }
    }
    if (!measured) {
        problems.add("every interval was reported disjoint");
    }
    D3d11Tools::reset_state(device);
    return problems.outcome();
}

Outcome QueryChecks::pipeline_stats(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    ID3D11DeviceContext* ctx = device.context;
    Problems problems;
    const char* vs_source = "float4 main(uint id : SV_VertexID) : SV_Position { uint tri = id / 3; uint k = id % 3; float x = -0.9 + (tri % 10) * 0.18; float y = -0.9 + (tri / 10) * 0.18;"
                            " float2 o[3] = {float2(0, 0), float2(0.15, 0), float2(0, 0.15)}; return float4(x + o[k].x, y + o[k].y, 0.5, 1); }";
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, vs_source, error);
    Com<ID3D11VertexShader> fullscreen = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    Com<ID3D11PixelShader> ps = Kit::color_ps(device, "1, 1, 1, 1", error);
    Com<ID3D11Texture2D> target = D3d11Tools::target(device, 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(device, target.get());
    Com<ID3D11Query> stats = make_query(device, D3D11_QUERY_PIPELINE_STATISTICS);
    if (!vs || !fullscreen || !ps || !view || !stats) {
        return Outcome::fail("scene objects: " + error);
    }
    ID3D11RenderTargetView* list[1] = {view.get()};
    ctx->OMSetRenderTargets(1, list, nullptr);
    D3d11Tools::set_viewport(device, 64, 64);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->PSSetShader(ps.get(), nullptr, 0);
    D3D11_QUERY_DATA_PIPELINE_STATISTICS data{};
    auto expect = [&](const char* name, UINT64 got, UINT64 want) {
        if (got != want) {
            problems.add(std::string(name) + " is " + std::to_string(got) + ", expected " + std::to_string(want));
        }
    };
    ctx->VSSetShader(vs.get(), nullptr, 0);
    ctx->Begin(stats.get());
    ctx->Draw(300, 0);
    ctx->End(stats.get());
    if (!fetch(device, stats.get(), data, true, error)) {
        return Outcome::fail(error);
    }
    expect("IAVertices", data.IAVertices, 300);
    expect("IAPrimitives", data.IAPrimitives, 100);
    expect("VSInvocations", data.VSInvocations, 300);
    expect("CInvocations", data.CInvocations, 100);
    expect("CPrimitives", data.CPrimitives, 100);
    if (data.PSInvocations < 100) {
        problems.add("PSInvocations is " + std::to_string(data.PSInvocations) + " for 100 visible triangles");
    }
    const char* gs_source = "struct I { float4 p : SV_Position; };\n[maxvertexcount(3)] void main(triangle I i[3], inout TriangleStream<I> s) { for (uint k = 0; k < 3; ++k) { s.Append(i[k]); } }";
    Com<ID3D11GeometryShader> gs = D3d11Tools::gs(device, gs_source, error);
    if (gs) {
        ctx->GSSetShader(gs.get(), nullptr, 0);
        ctx->Begin(stats.get());
        ctx->Draw(300, 0);
        ctx->End(stats.get());
        if (!fetch(device, stats.get(), data, true, error)) {
            return Outcome::fail(error);
        }
        expect("GSInvocations", data.GSInvocations, 100);
        expect("GSPrimitives", data.GSPrimitives, 100);
        ctx->GSSetShader(nullptr, nullptr, 0);
    } else {
        problems.add("the geometry shader cannot be built: " + error);
    }
    ctx->VSSetShader(fullscreen.get(), nullptr, 0);
    ctx->Begin(stats.get());
    ctx->Draw(3, 0);
    ctx->End(stats.get());
    if (!fetch(device, stats.get(), data, true, error)) {
        return Outcome::fail(error);
    }
    if (data.PSInvocations < 64 * 64) {
        problems.add("PSInvocations is " + std::to_string(data.PSInvocations) + " for a fullscreen triangle of 4096 pixels");
    }
    if (device.level >= D3D_FEATURE_LEVEL_11_0) {
        Com<ID3D11ComputeShader> cs = D3d11Tools::cs(device, "RWByteAddressBuffer o : register(u0);\n[numthreads(8,4,1)] void main(uint3 id : SV_DispatchThreadID) { uint o0; o.InterlockedAdd(0, 1, o0); }", error);
        Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 64, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
        Com<ID3D11UnorderedAccessView> uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
        if (cs && uav) {
            ID3D11UnorderedAccessView* uavs[1] = {uav.get()};
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ctx->CSSetShader(cs.get(), nullptr, 0);
            ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
            ctx->Begin(stats.get());
            ctx->Dispatch(3, 2, 1);
            ctx->End(stats.get());
            if (!fetch(device, stats.get(), data, true, error)) {
                return Outcome::fail(error);
            }
            expect("CSInvocations", data.CSInvocations, 192);
        } else {
            problems.add("the compute scene cannot be built: " + error);
        }
    }
    D3d11Tools::reset_state(device);
    static_cast<void>(context);
    return problems.outcome();
}

Outcome QueryChecks::event_and_predication(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    {
        D3d11Device& d = rig.av;
        Com<ID3D11Texture2D> target = D3d11Tools::target(d, 32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, target.get());
        Com<ID3D11PixelShader> ps = Kit::color_ps(d, "0, 1, 0, 1", error);
        Com<ID3D11Query> event = make_query(d, D3D11_QUERY_EVENT);
        Com<ID3D11Texture2D> staging = D3d11Tools::texture(d, D3d11Tools::desc2d(32, 32, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        if (!view || !ps || !event || !staging) {
            return Outcome::fail("event scene: " + error);
        }
        D3d11Tools::clear(d, view.get(), 1, 0, 0, 1);
        static_cast<void>(D3d11Tools::draw_fullscreen(d, ps.get(), view.get(), 32, 32, error));
        d.context->CopyResource(staging.get(), target.get());
        d.context->End(event.get());
        BOOL done = FALSE;
        if (!fetch(d, event.get(), done, true, error)) {
            return Outcome::fail(error);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(d.context->Map(staging.get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
            problems.add("after the EVENT query signalled, a DO_NOT_WAIT map of the copy still fails");
        } else {
            std::uint32_t first = 0;
            std::memcpy(&first, mapped.pData, 4);
            if (first != Kit::rgba(0, 255, 0, 255)) {
                problems.add("after the EVENT query the earlier work is not visible");
            }
            d.context->Unmap(staging.get(), 0);
        }
        D3d11Tools::reset_state(d);
    }
    enum Op { Draw, Dispatch, CopyResource, CopyRegion, Update, ClearRtv, ClearDsv, Resolve, ClearUav, kOps };
    const char* names[] = {"Draw", "Dispatch", "CopyResource", "CopySubresourceRegion", "UpdateSubresource", "ClearRenderTargetView", "ClearDepthStencilView", "ResolveSubresource", "ClearUnorderedAccessView"};
    const bool level11 = rig.av.level >= D3D_FEATURE_LEVEL_11_0;
    auto executed = [&](D3d11Device& d, int op, bool predicate_true, BOOL predicate_value, bool& usable, std::string& why) -> bool {
        ID3D11DeviceContext* ctx = d.context;
        Com<ID3D11Texture2D> subject = D3d11Tools::texture(d, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R32_UINT, D3D11_BIND_RENDER_TARGET | (level11 ? D3D11_BIND_UNORDERED_ACCESS : 0)));
        Com<ID3D11Texture2D> source = D3d11Tools::texture(d, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R32_UINT, 0));
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(d, subject.get());
        Com<ID3D11UnorderedAccessView> uav = level11 ? D3d11Tools::uav(d, subject.get()) : Com<ID3D11UnorderedAccessView>();
        const std::uint32_t one[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        const std::uint32_t two[16] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
        ctx->UpdateSubresource(subject.get(), 0, nullptr, one, 16, 0);
        ctx->UpdateSubresource(source.get(), 0, nullptr, two, 16, 0);
        Com<ID3D11Predicate> occlusion;
        D3D11_QUERY_DESC predicate_desc{};
        predicate_desc.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
        d.device->CreatePredicate(&predicate_desc, occlusion.put());
        Com<ID3D11PixelShader> white = Kit::color_ps(d, "1, 1, 1, 1", why);
        Com<ID3D11PixelShader> write_two = D3d11Tools::ps(d, "uint main() : SV_Target { return 2; }", why);
        Com<ID3D11VertexShader> vs = D3d11Tools::vs(d, D3d11Tools::kFullscreenVs, why);
        Com<ID3D11Texture2D> probe = D3d11Tools::target(d, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        Com<ID3D11RenderTargetView> probe_view = D3d11Tools::rtv(d, probe.get());
        if (!occlusion || !white || !write_two || !vs || !probe_view) {
            usable = false;
            return false;
        }
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.ScissorEnable = TRUE;
        raster.DepthClipEnable = TRUE;
        Com<ID3D11RasterizerState> rs;
        d.device->CreateRasterizerState(&raster, rs.put());
        ctx->RSSetState(rs.get());
        const D3D11_RECT area = predicate_true ? D3D11_RECT{0, 0, 4, 4} : D3D11_RECT{0, 0, 0, 0};
        ctx->RSSetScissorRects(1, &area);
        ID3D11RenderTargetView* probe_list[1] = {probe_view.get()};
        ctx->OMSetRenderTargets(1, probe_list, nullptr);
        D3d11Tools::set_viewport(d, 4, 4);
        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs.get(), nullptr, 0);
        ctx->PSSetShader(white.get(), nullptr, 0);
        ctx->Begin(occlusion.get());
        ctx->Draw(3, 0);
        ctx->End(occlusion.get());
        BOOL truth = FALSE;
        if (!fetch(d, occlusion.get(), truth, true, why) || (truth != FALSE) != predicate_true) {
            usable = false;
            return false;
        }
        Com<ID3D11Texture2D> multi = D3d11Tools::texture(d, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, 4));
        Com<ID3D11Texture2D> resolved = D3d11Tools::target(d, 4, 4, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        Com<ID3D11Texture2D> depth = D3d11Tools::texture(d, D3d11Tools::desc2d(4, 4, DXGI_FORMAT_D32_FLOAT, D3D11_BIND_DEPTH_STENCIL));
        Com<ID3D11DepthStencilView> dsv;
        if (depth) {
            d.device->CreateDepthStencilView(depth.get(), nullptr, dsv.put());
        }
        if (op == Resolve && multi) {
            Com<ID3D11RenderTargetView> mv = D3d11Tools::rtv(d, multi.get());
            D3d11Tools::clear(d, mv.get(), 1, 1, 1, 1);
            D3d11Tools::clear(d, D3d11Tools::rtv(d, resolved.get()).get(), 0, 0, 0, 0);
        }
        if (op == ClearDsv && dsv) {
            ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
        }
        const D3D11_RECT whole{0, 0, 4, 4};
        ctx->RSSetScissorRects(1, &whole);
        ID3D11RenderTargetView* subject_list[1] = {view.get()};
        ctx->OMSetRenderTargets(1, subject_list, nullptr);
        ctx->SetPredication(occlusion.get(), predicate_value);
        switch (op) {
        case Draw:
            ctx->PSSetShader(write_two.get(), nullptr, 0);
            ctx->Draw(3, 0);
            break;
        case Dispatch: {
            if (!uav) {
                usable = false;
                break;
            }
            Com<ID3D11ComputeShader> cs = D3d11Tools::cs(d, "RWTexture2D<uint> t : register(u0);\n[numthreads(4,4,1)] void main(uint3 id : SV_DispatchThreadID) { t[id.xy] = 2; }", why);
            if (!cs) {
                usable = false;
                break;
            }
            ctx->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11UnorderedAccessView* list[1] = {uav.get()};
            ctx->CSSetShader(cs.get(), nullptr, 0);
            ctx->CSSetUnorderedAccessViews(0, 1, list, nullptr);
            ctx->Dispatch(1, 1, 1);
            break;
        }
        case CopyResource:
            ctx->CopyResource(subject.get(), source.get());
            break;
        case CopyRegion:
            ctx->CopySubresourceRegion(subject.get(), 0, 0, 0, 0, source.get(), 0, nullptr);
            break;
        case Update:
            ctx->UpdateSubresource(subject.get(), 0, nullptr, two, 16, 0);
            break;
        case ClearRtv: {
            const float color[4] = {2, 2, 2, 2};
            ctx->ClearRenderTargetView(view.get(), color);
            break;
        }
        case ClearUav: {
            if (!uav) {
                usable = false;
                break;
            }
            const UINT value[4] = {2, 2, 2, 2};
            ctx->ClearUnorderedAccessViewUint(uav.get(), value);
            break;
        }
        case ClearDsv:
            if (!dsv) {
                usable = false;
                break;
            }
            ctx->ClearDepthStencilView(dsv.get(), D3D11_CLEAR_DEPTH, 0.0f, 0);
            break;
        case Resolve:
            if (!multi) {
                usable = false;
                break;
            }
            ctx->ResolveSubresource(resolved.get(), 0, multi.get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
            break;
        default:
            break;
        }
        ctx->SetPredication(nullptr, FALSE);
        if (!usable) {
            return false;
        }
        if (op == Resolve) {
            TexelImage image;
            return D3d11Tools::read_texture(d, resolved.get(), 0, image, why) && image.pixel(1, 1) == Kit::rgba(255, 255, 255, 255);
        }
        if (op == ClearDsv) {
            D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(4, 4, DXGI_FORMAT_D32_FLOAT, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
            Com<ID3D11Texture2D> staging = D3d11Tools::texture(d, desc);
            if (!staging) {
                usable = false;
                return false;
            }
            ctx->CopyResource(staging.get(), depth.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                usable = false;
                return false;
            }
            float value = 1.0f;
            std::memcpy(&value, mapped.pData, 4);
            ctx->Unmap(staging.get(), 0);
            return value == 0.0f;
        }
        TexelImage image;
        if (!D3d11Tools::read_texture(d, subject.get(), 0, image, why)) {
            usable = false;
            return false;
        }
        return image.pixel(1, 1) == 2;
    };
    for (int op = 0; op < kOps; ++op) {
        bool usable_av = true;
        bool usable_warp = true;
        std::string why;
        const bool av_true_false = executed(rig.av, op, true, FALSE, usable_av, why);
        const bool av_true_true = executed(rig.av, op, true, TRUE, usable_av, why);
        const bool av_false_false = executed(rig.av, op, false, FALSE, usable_av, why);
        const bool av_false_true = executed(rig.av, op, false, TRUE, usable_av, why);
        if (!usable_av) {
            context.note(std::string(names[op]) + " is not testable on this device: " + why);
            continue;
        }
        const bool w_true_false = executed(rig.warp, op, true, FALSE, usable_warp, why);
        const bool w_true_true = executed(rig.warp, op, true, TRUE, usable_warp, why);
        const bool w_false_false = executed(rig.warp, op, false, FALSE, usable_warp, why);
        const bool w_false_true = executed(rig.warp, op, false, TRUE, usable_warp, why);
        if (av_true_false == av_true_true || av_false_false == av_false_true) {
            problems.add(std::string(names[op]) + " is not affected by predication: the same outcome under both predicate values");
        }
        if (av_true_false != w_true_false || av_true_true != w_true_true || av_false_false != w_false_false || av_false_true != w_false_true) {
            problems.add(std::string(names[op]) + " under predication differs from WARP");
        }
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
