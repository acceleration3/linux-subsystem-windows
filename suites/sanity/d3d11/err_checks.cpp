#include "sanity/d3d11/err_checks.hpp"

#include "sanity/d3d11/kit.hpp"
#include "support/leak_loop.hpp"
#include "support/oracle_rig.hpp"

#include <array>
#include <chrono>
#include <functional>

namespace lsw::suites::sanity::d3d11 {

Outcome ErrChecks::bad_parameters(Context& context) {
    OracleRig rig;
    std::string error;
    if (!rig.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    struct Case {
        std::string name;
        std::function<HRESULT(D3d11Device&)> run;
    };
    std::vector<Case> cases;
    cases.push_back({"null texture description", [](D3d11Device& d) { Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(nullptr, nullptr, t.put()); }});
    cases.push_back({"null buffer description", [](D3d11Device& d) { Com<ID3D11Buffer> b; return d.device->CreateBuffer(nullptr, nullptr, b.put()); }});
    cases.push_back({"invalid usage", [](D3d11Device& d) { auto desc = D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, static_cast<D3D11_USAGE>(99)); Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(&desc, nullptr, t.put()); }});
    cases.push_back({"invalid format", [](D3d11Device& d) { auto desc = D3d11Tools::desc2d(8, 8, static_cast<DXGI_FORMAT>(9999), D3D11_BIND_SHADER_RESOURCE); Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(&desc, nullptr, t.put()); }});
    cases.push_back({"invalid bind flags", [](D3d11Device& d) { auto desc = D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, 0x80000000u); Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(&desc, nullptr, t.put()); }});
    cases.push_back({"zero array size", [](D3d11Device& d) { auto desc = D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 1, 0); Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(&desc, nullptr, t.put()); }});
    cases.push_back({"sample count 3", [](D3d11Device& d) { auto desc = D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, 3); Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(&desc, nullptr, t.put()); }});
    cases.push_back({"too many mip levels", [](D3d11Device& d) { auto desc = D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0, 12); Com<ID3D11Texture2D> t; return d.device->CreateTexture2D(&desc, nullptr, t.put()); }});
    cases.push_back({"buffer size overflow", [](D3d11Device& d) { HRESULT r = S_OK; auto b = D3d11Tools::buffer(d, 0xFFFFFFFFu, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, 0, nullptr, &r); return r; }});
    cases.push_back({"constant buffer not a multiple of 16", [](D3d11Device& d) { HRESULT r = S_OK; auto b = D3d11Tools::buffer(d, 20, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0, nullptr, &r); return r; }});
    cases.push_back({"structured buffer without stride", [](D3d11Device& d) { HRESULT r = S_OK; auto b = D3d11Tools::buffer(d, 64, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 0, nullptr, &r); return r; }});
    cases.push_back({"render target view without the bind flag", [](D3d11Device& d) { auto t = D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE)); Com<ID3D11RenderTargetView> v; return d.device->CreateRenderTargetView(t.get(), nullptr, v.put()); }});
    cases.push_back({"shader resource view of a mismatched format", [](D3d11Device& d) { auto t = D3d11Tools::texture(d, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE)); D3D11_SHADER_RESOURCE_VIEW_DESC vd{}; vd.Format = DXGI_FORMAT_R32G32B32A32_FLOAT; vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; vd.Texture2D.MipLevels = 1; Com<ID3D11ShaderResourceView> v; return d.device->CreateShaderResourceView(t.get(), &vd, v.put()); }});
    cases.push_back({"view of a null resource", [](D3d11Device& d) { Com<ID3D11ShaderResourceView> v; return d.device->CreateShaderResourceView(nullptr, nullptr, v.put()); }});
    cases.push_back({"garbage shader bytecode", [](D3d11Device& d) { const std::uint8_t junk[64] = {1, 2, 3}; Com<ID3D11PixelShader> s; return d.device->CreatePixelShader(junk, sizeof(junk), nullptr, s.put()); }});
    cases.push_back({"empty shader bytecode", [](D3d11Device& d) { Com<ID3D11VertexShader> s; return d.device->CreateVertexShader(nullptr, 0, nullptr, s.put()); }});
    cases.push_back({"invalid sampler filter", [](D3d11Device& d) { D3D11_SAMPLER_DESC sd{}; sd.Filter = static_cast<D3D11_FILTER>(0x7777); sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP; sd.ComparisonFunc = D3D11_COMPARISON_NEVER; Com<ID3D11SamplerState> s; return d.device->CreateSamplerState(&sd, s.put()); }});
    cases.push_back({"invalid blend factor", [](D3d11Device& d) { D3D11_BLEND_DESC bd{}; bd.RenderTarget[0].BlendEnable = TRUE; bd.RenderTarget[0].SrcBlend = static_cast<D3D11_BLEND>(77); bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD; bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE; bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD; bd.RenderTarget[0].RenderTargetWriteMask = 0xf; Com<ID3D11BlendState> s; return d.device->CreateBlendState(&bd, s.put()); }});
    cases.push_back({"invalid fill mode", [](D3d11Device& d) { D3D11_RASTERIZER_DESC rd{}; rd.FillMode = static_cast<D3D11_FILL_MODE>(9); rd.CullMode = D3D11_CULL_NONE; Com<ID3D11RasterizerState> s; return d.device->CreateRasterizerState(&rd, s.put()); }});
    cases.push_back({"invalid query type", [](D3d11Device& d) { D3D11_QUERY_DESC qd{}; qd.Query = static_cast<D3D11_QUERY>(999); Com<ID3D11Query> q; return d.device->CreateQuery(&qd, q.put()); }});
    cases.push_back({"input layout without signature", [](D3d11Device& d) { const D3D11_INPUT_ELEMENT_DESC e = {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}; Com<ID3D11InputLayout> l; return d.device->CreateInputLayout(&e, 1, nullptr, 0, l.put()); }});
    int refused = 0;
    for (const Case& c : cases) {
        const HRESULT a = c.run(rig.av);
        const HRESULT w = c.run(rig.warp);
        if (SUCCEEDED(a)) {
            problems.add(c.name + " succeeded");
        } else {
            ++refused;
            if (a != E_INVALIDARG && a != E_OUTOFMEMORY && a != E_FAIL) {
                problems.add(c.name + " returned " + D3d11Tools::hr(a));
            }
            if (a != w) {
                problems.add(c.name + " returned " + D3d11Tools::hr(a) + " where WARP returns " + D3d11Tools::hr(w));
            }
        }
    }
    context.measure("refused", refused);
    {
        D3d11Device debug;
        std::string debug_error;
        if (debug.create_ex(rig.adapter.luid, nullptr, 0, D3D11_CREATE_DEVICE_DEBUG, debug_error)) {
            Com<ID3D11InfoQueue> queue = Kit::query<ID3D11InfoQueue>(debug.device);
            if (queue) {
                queue->ClearStoredMessages();
                for (const Case& c : cases) {
                    static_cast<void>(c.run(debug));
                }
                const UINT64 messages = queue->GetNumStoredMessagesAllowedByRetrievalFilter();
                context.measure("debug_messages", static_cast<long long>(messages));
                if (messages == 0) {
                    problems.add("the debug layer recorded no message for " + std::to_string(cases.size()) + " invalid calls");
                }
            }
        } else {
            context.note("the D3D11 debug layer is not installed (" + debug_error + "); the info queue is not checked");
        }
    }
    Com<ID3D11Texture2D> target = D3d11Tools::target(rig.av, 16, 16);
    Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(rig.av, target.get());
    if (!view) {
        problems.add("the device cannot create a render target after the invalid calls");
    } else {
        D3d11Tools::clear(rig.av, view.get(), 1, 0, 0, 1);
        TexelImage image;
        if (!D3d11Tools::read_texture(rig.av, target.get(), 0, image, error) || image.pixel(3, 3) != Kit::rgba(255, 0, 0, 255)) {
            problems.add("the device does not render correctly after the invalid calls");
        }
    }
    return problems.outcome();
}

Outcome ErrChecks::device_removed(Context& context) {
    AvAdapter adapter;
    D3d11Device device;
    std::string error;
    if (!Kit::open(adapter, device, error)) {
        return Outcome::fail(error);
    }
    if (device.level < D3D_FEATURE_LEVEL_11_0) {
        return Context::skip("the hang shader needs compute shader 5");
    }
    Problems problems;
    Com<ID3D11Fence> removal_fence;
    Com<ID3D11Device5> device5 = Kit::query<ID3D11Device5>(device.device);
    HANDLE removal_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (device5 && SUCCEEDED(device5->CreateFence(0, D3D11_FENCE_FLAG_NONE, __uuidof(ID3D11Fence), removal_fence.put_void()))) {
        removal_fence->SetEventOnCompletion(UINT64_MAX, removal_event);
    }
    Com<ID3D11ComputeShader> hang = D3d11Tools::cs(device,
        "RWByteAddressBuffer o : register(u0);\n[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
        "  uint n = o.Load(0); uint x = id.x; [loop] for (uint i = 0; i < n; ++i) { x = x * 1664525u + 1013904223u + (x >> 5); }\n  o.Store(4 + id.x * 4, x);\n}\n", error);
    const std::uint32_t limit[1] = {0xFFFFFFFFu};
    Com<ID3D11Buffer> buffer = D3d11Tools::buffer(device, 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_UNORDERED_ACCESS, 0, D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0, nullptr);
    Com<ID3D11UnorderedAccessView> uav = buffer ? D3d11Tools::raw_uav(device, buffer.get()) : Com<ID3D11UnorderedAccessView>();
    if (!hang || !uav) {
        return Outcome::fail("the hang scene cannot be built: " + error);
    }
    device.context->UpdateSubresource(buffer.get(), 0, nullptr, limit, 0, 0);
    ID3D11UnorderedAccessView* list[1] = {uav.get()};
    device.context->CSSetShader(hang.get(), nullptr, 0);
    device.context->CSSetUnorderedAccessViews(0, 1, list, nullptr);
    device.context->Dispatch(64, 1, 1);
    device.context->Flush();
    HRESULT reason = S_OK;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < deadline) {
        reason = device.device->GetDeviceRemovedReason();
        if (reason != S_OK) {
            break;
        }
        ::Sleep(100);
    }
    context.measure("removed_reason", D3d11Tools::hr(reason));
    if (reason == S_OK) {
        problems.add("a shader running far past the watchdog time did not remove the device within 45 s");
    } else {
        if (reason != DXGI_ERROR_DEVICE_HUNG && reason != DXGI_ERROR_DEVICE_REMOVED && reason != DXGI_ERROR_DEVICE_RESET && reason != DXGI_ERROR_DRIVER_INTERNAL_ERROR && reason != DXGI_ERROR_INVALID_CALL) {
            problems.add("GetDeviceRemovedReason returned the undocumented " + D3d11Tools::hr(reason));
        }
        if (reason != device.device->GetDeviceRemovedReason()) {
            problems.add("GetDeviceRemovedReason is not stable");
        }
        HRESULT create_result = S_OK;
        Com<ID3D11Texture2D> t = D3d11Tools::texture(device, D3d11Tools::desc2d(8, 8, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE), nullptr, &create_result);
        if (t) {
            problems.add("a texture is created on a removed device");
        } else if (create_result != DXGI_ERROR_DEVICE_REMOVED && create_result != DXGI_ERROR_DEVICE_RESET && create_result != DXGI_ERROR_DEVICE_HUNG && create_result != E_FAIL && create_result != DXGI_ERROR_INVALID_CALL) {
            problems.add("creating a texture on a removed device returned " + D3d11Tools::hr(create_result));
        }
        if (removal_fence && ::WaitForSingleObject(removal_event, 5000) != WAIT_OBJECT_0) {
            problems.add("the device removed event of the fence did not fire after the removal");
        }
    }
    ::CloseHandle(removal_event);
    removal_fence.reset();
    device5.reset();
    uav.reset();
    buffer.reset();
    hang.reset();
    device.destroy();
    ::Sleep(500);
    D3d11Device again;
    if (!again.create(adapter.luid, error)) {
        problems.add("a new device cannot be created after the removal: " + error);
    } else {
        Com<ID3D11Texture2D> target = D3d11Tools::target(again, 16, 16);
        Com<ID3D11RenderTargetView> view = D3d11Tools::rtv(again, target.get());
        D3d11Tools::clear(again, view.get(), 0, 1, 0, 1);
        TexelImage image;
        if (!D3d11Tools::read_texture(again, target.get(), 0, image, error) || image.pixel(2, 2) != Kit::rgba(0, 255, 0, 255)) {
            problems.add("the device created after the removal does not render");
        }
    }
    return problems.outcome();
}

Outcome ErrChecks::stress_destroy(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    D3d11Device device;
    if (!device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    Com<ID3D11VertexShader> vs = D3d11Tools::vs(device, D3d11Tools::kFullscreenVs, error);
    if (!vs) {
        return Outcome::fail(error);
    }
    LeakLoop::run(context, problems, "d3d11.churn", adapter.luid, 2000, [&](int i, std::string& detail) {
        {
            Com<ID3D11Texture2D> t = D3d11Tools::texture(device, D3d11Tools::desc2d(64, 64, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE));
            Com<ID3D11Buffer> b = D3d11Tools::buffer(device, 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, nullptr);
            Com<ID3D11RenderTargetView> rtv = t ? D3d11Tools::rtv(device, t.get()) : Com<ID3D11RenderTargetView>();
            Com<ID3D11ShaderResourceView> srv = t ? D3d11Tools::srv(device, t.get()) : Com<ID3D11ShaderResourceView>();
            D3D11_BLEND_DESC bd{};
            bd.RenderTarget[0].RenderTargetWriteMask = 0xf;
            bd.RenderTarget[0].BlendEnable = (i & 1) != 0;
            bd.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            Com<ID3D11BlendState> blend;
            device.device->CreateBlendState(&bd, blend.put());
            D3D11_SAMPLER_DESC sd{};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sd.MaxLOD = D3D11_FLOAT32_MAX;
            Com<ID3D11SamplerState> sampler;
            device.device->CreateSamplerState(&sd, sampler.put());
            D3D11_QUERY_DESC qd{};
            qd.Query = D3D11_QUERY_EVENT;
            Com<ID3D11Query> query;
            device.device->CreateQuery(&qd, query.put());
            Com<ID3D11DeviceContext> deferred;
            device.device->CreateDeferredContext(0, deferred.put());
            if (!t || !b || !rtv || !srv || !blend || !sampler || !query || !deferred) {
                detail = "creating an object of the churn set failed";
                return false;
            }
            deferred->ClearRenderTargetView(rtv.get(), std::array<float, 4>{0, 0, 0, 1}.data());
            Com<ID3D11CommandList> commands;
            deferred->FinishCommandList(FALSE, commands.put());
            device.context->ExecuteCommandList(commands.get(), FALSE);
            if (i % 100 == 99) {
                device.context->Flush();
            }
        }
        if (i % 50 == 49) {
            D3d11Device extra;
            std::string local;
            if (!extra.create(adapter.luid, local)) {
                detail = "creating a device failed: " + local;
                return false;
            }
        }
        return true;
    });
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d11
