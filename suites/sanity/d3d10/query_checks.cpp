#include "sanity/d3d10/query_checks.hpp"

#include "sanity/d3d10/kit.hpp"

#include <array>
#include <chrono>
#include <cstring>

namespace lsw::suites::sanity::d3d10 {

namespace {

constexpr const char* kPointVs =
    "struct I { float3 p : POSITION; };\n"
    "float4 main(I i) : SV_Position { return float4(i.p, 1); }\n";

constexpr const char* kFlatPs = "float4 main(float4 p : SV_Position) : SV_Target { return float4(1, 1, 1, 1); }\n";

bool fetch(ID3D10Query* query, void* data, UINT size, std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    HRESULT result = S_FALSE;
    while ((result = query->GetData(data, size, 0)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            error = "a query did not complete in 10 s";
            return false;
        }
        ::SwitchToThread();
    }
    if (result != S_OK) {
        error = "GetData failed: " + Kit::hr(result);
        return false;
    }
    return true;
}

Com<ID3D10Query> make_query(ID3D10Device* device, D3D10_QUERY type) {
    D3D10_QUERY_DESC desc{};
    desc.Query = type;
    Com<ID3D10Query> query;
    if (type == D3D10_QUERY_OCCLUSION_PREDICATE) {
        Com<ID3D10Predicate> predicate;
        device->CreatePredicate(&desc, predicate.put());
        return predicate.as<ID3D10Query>();
    }
    device->CreateQuery(&desc, query.put());
    return query;
}

struct Pipeline {
    Com<ID3D10VertexShader> vs;
    Com<ID3D10PixelShader> ps;
    Com<ID3D10InputLayout> layout;
    Com<ID3D10Buffer> buffer;
    Com<ID3D10DepthStencilState> depth_less;
    Com<ID3D10Texture2D> target;
    Com<ID3D10Texture2D> depth;
    Com<ID3D10RasterizerState> raster;
    Com<ID3D10RenderTargetView> view;
    Com<ID3D10DepthStencilView> depth_view;
    UINT size = 64;

    [[nodiscard]] bool init(D3d10Device& d, std::string& error) {
        vs = D3d10Tools::vs(d, kPointVs, error);
        ps = D3d10Tools::ps(d, kFlatPs, error);
        const std::vector<std::uint8_t> code = D3d10Tools::vs_code(kPointVs, error);
        const D3D10_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0};
        if (!vs || !ps || code.empty() || FAILED(d.device->CreateInputLayout(&element, 1, code.data(), code.size(), layout.put()))) {
            return false;
        }
        target = D3d10Tools::target(d, size, size);
        depth = D3d10Tools::texture(d, D3d10Tools::desc2d(size, size, DXGI_FORMAT_D32_FLOAT, D3D10_BIND_DEPTH_STENCIL));
        if (!target || !depth) {
            error = "the render target or depth buffer cannot be created";
            return false;
        }
        view = D3d10Tools::rtv(d, target.get());
        d.device->CreateDepthStencilView(depth.get(), nullptr, depth_view.put());
        D3D10_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = TRUE;
        desc.DepthWriteMask = D3D10_DEPTH_WRITE_MASK_ALL;
        desc.DepthFunc = D3D10_COMPARISON_LESS;
        d.device->CreateDepthStencilState(&desc, depth_less.put());
        D3D10_RASTERIZER_DESC raster_desc{};
        raster_desc.FillMode = D3D10_FILL_SOLID;
        raster_desc.CullMode = D3D10_CULL_NONE;
        raster_desc.DepthClipEnable = TRUE;
        d.device->CreateRasterizerState(&raster_desc, raster.put());
        return view && depth_view && depth_less && raster;
    }

    void bind(D3d10Device& d) {
        ID3D10Device* device = d.device;
        ID3D10RenderTargetView* targets[1] = {view.get()};
        device->OMSetRenderTargets(1, targets, depth_view.get());
        device->OMSetDepthStencilState(depth_less.get(), 0);
        device->RSSetState(raster.get());
        D3d10Tools::set_viewport(d, size, size);
        device->IASetInputLayout(layout.get());
        device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        device->VSSetShader(vs.get());
        device->PSSetShader(ps.get());
    }

    void draw_rect(D3d10Device& d, float x0, float y0, float x1, float y1, float z) {
        const float quad[18] = {x0, y1, z, x0, y0, z, x1, y1, z, x1, y1, z, x0, y0, z, x1, y0, z};
        Com<ID3D10Buffer> vb = D3d10Tools::buffer(d, sizeof(quad), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, quad);
        const UINT stride = 12;
        const UINT offset = 0;
        ID3D10Buffer* raw = vb.get();
        d.device->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
        d.device->Draw(6, 0);
    }
};

float ndc_x(float pixel) {
    return pixel / 32.0f - 1.0f;
}

float ndc_y(float pixel) {
    return 1.0f - pixel / 32.0f;
}

}  // namespace

Outcome QueryChecks::predicate(Context& context) {
    AvAdapter adapter;
    D3d10Device d;
    std::string error;
    if (!Kit::open(adapter, d, false, error)) {
        return Outcome::fail(error);
    }
    ID3D10Device* device = d.device;
    Pipeline pipeline;
    if (!pipeline.init(d, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    pipeline.bind(d);
    const float black[4] = {0, 0, 0, 1};
    device->ClearRenderTargetView(pipeline.view.get(), black);
    device->ClearDepthStencilView(pipeline.depth_view.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
    pipeline.draw_rect(d, ndc_x(0), ndc_y(0), ndc_x(64), ndc_y(32), 0.2f);
    struct Probe {
        const char* name;
        float l, t, r, b, z;
        UINT64 expected;
    };
    const Probe probes[] = {{"a quad half hidden behind an occluder", 16, 16, 48, 48, 0.8f, 32 * 16}, {"a fully hidden quad", 8, 8, 24, 24, 0.8f, 0}, {"an unobstructed quad", 0, 40, 64, 64, 0.5f, 64 * 24}};
    for (const Probe& p : probes) {
        Com<ID3D10Query> occlusion = make_query(device, D3D10_QUERY_OCCLUSION);
        Com<ID3D10Query> predicate = make_query(device, D3D10_QUERY_OCCLUSION_PREDICATE);
        if (!occlusion || !predicate) {
            problems.add("occlusion queries cannot be created");
            break;
        }
        occlusion->Begin();
        predicate->Begin();
        pipeline.draw_rect(d, ndc_x(p.l), ndc_y(p.t), ndc_x(p.r), ndc_y(p.b), p.z);
        occlusion->End();
        predicate->End();
        UINT64 samples = 0xFFFFFFFFull;
        BOOL visible = FALSE;
        if (!fetch(occlusion.get(), &samples, sizeof(samples), error) || !fetch(predicate.get(), &visible, sizeof(visible), error)) {
            problems.add(std::string(p.name) + ": " + error);
            continue;
        }
        if (samples != p.expected) {
            problems.add(std::string(p.name) + ": OCCLUSION counts " + std::to_string(samples) + " samples, expected " + std::to_string(p.expected));
        }
        if ((visible != FALSE) != (p.expected != 0)) {
            problems.add(std::string(p.name) + ": OCCLUSION_PREDICATE is " + std::to_string(visible) + " for " + std::to_string(p.expected) + " expected samples");
        }
    }
    {
        Com<ID3D10Query> statistics = make_query(device, D3D10_QUERY_PIPELINE_STATISTICS);
        pipeline.bind(d);
        device->ClearDepthStencilView(pipeline.depth_view.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
        statistics->Begin();
        pipeline.draw_rect(d, ndc_x(8), ndc_y(8), ndc_x(40), ndc_y(40), 0.5f);
        statistics->End();
        D3D10_QUERY_DATA_PIPELINE_STATISTICS stats{};
        if (!fetch(statistics.get(), &stats, sizeof(stats), error)) {
            problems.add("PIPELINE_STATISTICS: " + error);
        } else {
            context.measure("stats.ps_invocations", static_cast<long long>(stats.PSInvocations));
            if (stats.IAVertices != 6 || stats.IAPrimitives != 2) {
                problems.add("PIPELINE_STATISTICS reports " + std::to_string(stats.IAVertices) + " input vertices and " + std::to_string(stats.IAPrimitives) + " primitives for a draw of 6 vertices");
            }
            if (stats.VSInvocations < 4 || stats.VSInvocations > 6) {
                problems.add("PIPELINE_STATISTICS reports " + std::to_string(stats.VSInvocations) + " vertex shader invocations for 6 distinct vertices");
            }
            if (stats.GSInvocations != 0 || stats.GSPrimitives != 0) {
                problems.add("PIPELINE_STATISTICS reports geometry shader work without a geometry shader");
            }
            if (stats.CInvocations != 2 || stats.CPrimitives != 2) {
                problems.add("PIPELINE_STATISTICS reports " + std::to_string(stats.CInvocations) + " clipper invocations and " + std::to_string(stats.CPrimitives) + " primitives for 2 visible triangles");
            }
            if (stats.PSInvocations < 32 * 32) {
                problems.add("PIPELINE_STATISTICS reports " + std::to_string(stats.PSInvocations) + " pixel shader invocations for 1024 covered pixels");
            }
        }
        const std::string gs_source = "struct V { float4 p : SV_Position; };\n[maxvertexcount(3)] void main(triangle V v[3], inout TriangleStream<V> s) { s.Append(v[0]); s.Append(v[1]); s.Append(v[2]); }\n";
        Com<ID3D10GeometryShader> gs = D3d10Tools::gs(d, gs_source, error);
        if (gs) {
            device->GSSetShader(gs.get());
            statistics = make_query(device, D3D10_QUERY_PIPELINE_STATISTICS);
            statistics->Begin();
            pipeline.draw_rect(d, ndc_x(8), ndc_y(8), ndc_x(40), ndc_y(40), 0.4f);
            statistics->End();
            device->GSSetShader(nullptr);
            if (fetch(statistics.get(), &stats, sizeof(stats), error)) {
                if (stats.GSInvocations != 2 || stats.GSPrimitives != 2) {
                    problems.add("with a geometry shader PIPELINE_STATISTICS reports " + std::to_string(stats.GSInvocations) + " invocations and " + std::to_string(stats.GSPrimitives) + " primitives, expected 2 and 2");
                }
            } else {
                problems.add("PIPELINE_STATISTICS with a geometry shader: " + error);
            }
        } else {
            problems.add("the geometry shader cannot be created: " + error);
        }
    }
    {
        Com<ID3D10Query> disjoint = make_query(device, D3D10_QUERY_TIMESTAMP_DISJOINT);
        Com<ID3D10Query> first = make_query(device, D3D10_QUERY_TIMESTAMP);
        Com<ID3D10Query> second = make_query(device, D3D10_QUERY_TIMESTAMP);
        if (!disjoint || !first || !second) {
            problems.add("timestamp queries cannot be created");
        } else {
            disjoint->Begin();
            first->End();
            pipeline.bind(d);
            for (int i = 0; i < 40; ++i) {
                pipeline.draw_rect(d, -1, -1, 1, 1, 0.9f - static_cast<float>(i) * 0.01f);
            }
            second->End();
            disjoint->End();
            D3D10_QUERY_DATA_TIMESTAMP_DISJOINT info{};
            UINT64 a = 0;
            UINT64 b = 0;
            if (!fetch(disjoint.get(), &info, sizeof(info), error) || !fetch(first.get(), &a, sizeof(a), error) || !fetch(second.get(), &b, sizeof(b), error)) {
                problems.add("timestamp queries: " + error);
            } else {
                if (info.Frequency == 0) {
                    problems.add("TIMESTAMP_DISJOINT reports a zero frequency");
                }
                if (b < a) {
                    problems.add("the second timestamp " + std::to_string(b) + " is below the first " + std::to_string(a));
                }
                context.measure("timestamp_frequency_hz", static_cast<long long>(info.Frequency));
                context.measure("timestamp_disjoint", info.Disjoint != FALSE);
            }
        }
    }
    {
        using Matrix = std::array<std::array<bool, 2>, 2>;
        auto run_matrix = [&](D3d10Device& dev, Matrix& out, std::string& why) {
            Pipeline local;
            if (!local.init(dev, why)) {
                return false;
            }
            struct Combo {
                bool occluded;
                BOOL value;
            };
            const Combo combos[4] = {{false, FALSE}, {false, TRUE}, {true, FALSE}, {true, TRUE}};
            for (const Combo& c : combos) {
                local.bind(dev);
                const float clear[4] = {0, 0, 0, 1};
                dev.device->ClearRenderTargetView(local.view.get(), clear);
                dev.device->ClearDepthStencilView(local.depth_view.get(), D3D10_CLEAR_DEPTH, 1.0f, 0);
                Com<ID3D10Predicate> predicate;
                D3D10_QUERY_DESC desc{};
                desc.Query = D3D10_QUERY_OCCLUSION_PREDICATE;
                dev.device->CreatePredicate(&desc, predicate.put());
                if (!predicate) {
                    why = "a predicate cannot be created";
                    return false;
                }
                if (c.occluded) {
                    local.draw_rect(dev, -1, -1, 1, 1, 0.1f);
                }
                predicate->Begin();
                local.draw_rect(dev, ndc_x(8), ndc_y(8), ndc_x(24), ndc_y(24), 0.5f);
                predicate->End();
                dev.device->SetPredication(predicate.get(), c.value);
                local.draw_rect(dev, ndc_x(40), ndc_y(40), ndc_x(56), ndc_y(56), 0.05f);
                dev.device->SetPredication(nullptr, FALSE);
                TexelImage image;
                if (!D3d10Tools::read_texture(dev, local.target.get(), image, why)) {
                    return false;
                }
                out[c.occluded ? 1 : 0][c.value != FALSE ? 1 : 0] = (image.pixel(48, 48) & 0xFFFFFFu) == 0xFFFFFFu;
            }
            dev.device->ClearState();
            return true;
        };
        Matrix mine{};
        Matrix reference{};
        std::string why;
        if (!run_matrix(d, mine, why)) {
            problems.add("predication: " + why);
        } else {
            for (int occluded = 0; occluded < 2; ++occluded) {
                const auto row = static_cast<std::size_t>(occluded);
                if (mine[row][0] == mine[row][1]) {
                    problems.add(std::string("with a predicate that is ") + (occluded != 0 ? "false (occluded)" : "true (visible)") + " the draw is " + (mine[row][0] ? "executed" : "skipped") + " for both values of PredicateValue");
                }
            }
            if (mine[0][0] == mine[1][0] && mine[0][1] == mine[1][1]) {
                problems.add("predication does not depend on the predicate result");
            }
            D3d10Device warp;
            if (!warp.create_warp(0, why)) {
                context.note("the WARP oracle for the predication orientation is unavailable: " + why);
            } else if (!run_matrix(warp, reference, why)) {
                problems.add("predication on WARP: " + why);
            } else if (mine != reference) {
                problems.add("draws under SetPredication differ from WARP for the predicate results and PredicateValue combinations");
            }
            context.measure("predication.visible_false_executes", mine[0][0]);
            context.measure("predication.visible_true_executes", mine[0][1]);
            context.measure("predication.occluded_false_executes", mine[1][0]);
            context.measure("predication.occluded_true_executes", mine[1][1]);
        }
    }
    device->ClearState();
    return problems.outcome();
}

Outcome QueryChecks::clearstate_and_stale(Context& context) {
    AvAdapter adapter;
    D3d10Device d;
    std::string error;
    if (!Kit::open(adapter, d, false, error)) {
        return Outcome::fail(error);
    }
    ID3D10Device* device = d.device;
    Problems problems;
    Pipeline pipeline;
    if (!pipeline.init(d, error)) {
        return Outcome::fail(error);
    }
    auto bound = [&](const char* when, bool expect) {
        ID3D10RenderTargetView* target = nullptr;
        ID3D10DepthStencilView* depth = nullptr;
        device->OMGetRenderTargets(1, &target, &depth);
        ID3D10VertexShader* vs = nullptr;
        ID3D10PixelShader* ps = nullptr;
        ID3D10InputLayout* layout = nullptr;
        ID3D10Buffer* buffer = nullptr;
        UINT stride = 0;
        UINT offset = 0;
        device->VSGetShader(&vs);
        device->PSGetShader(&ps);
        device->IAGetInputLayout(&layout);
        device->IAGetVertexBuffers(0, 1, &buffer, &stride, &offset);
        const bool all = target != nullptr && depth != nullptr && vs != nullptr && ps != nullptr && layout != nullptr && buffer != nullptr;
        const bool none = target == nullptr && depth == nullptr && vs == nullptr && ps == nullptr && layout == nullptr && buffer == nullptr;
        for (IUnknown* object : std::initializer_list<IUnknown*>{target, depth, vs, ps, layout, buffer}) {
            if (object != nullptr) {
                object->Release();
            }
        }
        if (expect && !all) {
            problems.add(std::string(when) + ": some pipeline state that should still be bound is unbound");
        }
        if (!expect && !none) {
            problems.add(std::string(when) + ": some pipeline state is still bound");
        }
    };
    pipeline.bind(d);
    const float quad[18] = {-1, -1, 0.5f, -1, 1, 0.5f, 1, -1, 0.5f, 1, -1, 0.5f, -1, 1, 0.5f, 1, 1, 0.5f};
    Com<ID3D10Buffer> vb = D3d10Tools::buffer(d, sizeof(quad), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, quad);
    const UINT stride = 12;
    const UINT offset = 0;
    ID3D10Buffer* raw = vb.get();
    device->IASetVertexBuffers(0, 1, &raw, &stride, &offset);
    const float marker[4] = {0.2f, 0.6f, 1.0f, 1.0f};
    device->ClearRenderTargetView(pipeline.view.get(), marker);
    device->Flush();
    bound("after Flush", true);
    device->ClearState();
    bound("after ClearState", false);
    device->Draw(6, 0);
    device->Flush();
    TexelImage image;
    std::string where;
    if (!D3d10Tools::read_texture(d, pipeline.target.get(), image, error) || !Kit::uniform(image, D3d10Tools::bgra(51, 153, 255, 255), where)) {
        problems.add("a draw with nothing bound changed the render target: " + where + error);
    }
    {
        Com<ID3D10VertexShader> vs = D3d10Tools::vs(d, kPointVs, error);
        Com<ID3D10PixelShader> ps = D3d10Tools::ps(d, kFlatPs, error);
        Com<ID3D10Texture2D> texture = D3d10Tools::target(d, 16, 16);
        Com<ID3D10RenderTargetView> view = D3d10Tools::rtv(d, texture.get());
        Com<ID3D10Buffer> buffer = D3d10Tools::buffer(d, sizeof(quad), D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, quad);
        const std::vector<std::uint8_t> code = D3d10Tools::vs_code(kPointVs, error);
        const D3D10_INPUT_ELEMENT_DESC element = {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0};
        Com<ID3D10InputLayout> layout;
        device->CreateInputLayout(&element, 1, code.data(), code.size(), layout.put());
        if (!vs || !ps || !view || !buffer || !layout) {
            return Outcome::fail("the stale-object resources cannot be created");
        }
        ID3D10RenderTargetView* targets[1] = {view.get()};
        device->OMSetRenderTargets(1, targets, nullptr);
        D3d10Tools::set_viewport(d, 16, 16);
        device->IASetInputLayout(layout.get());
        device->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D10Buffer* vertex_buffer = buffer.get();
        device->IASetVertexBuffers(0, 1, &vertex_buffer, &stride, &offset);
        device->VSSetShader(vs.get());
        device->PSSetShader(ps.get());
        D3d10Tools::clear(d, view.get(), 0, 0, 0, 1);
        vs.reset();
        ps.reset();
        layout.reset();
        buffer.reset();
        view.reset();
        device->Draw(6, 0);
        device->Flush();
        ID3D10RenderTargetView* current = nullptr;
        device->OMGetRenderTargets(1, &current, nullptr);
        context.measure("released_bound_view_still_bound", current != nullptr);
        if (current != nullptr) {
            current->Release();
        }
        device->ClearState();
        bound("after releasing and clearing", false);
        Com<ID3D10Texture2D> fresh = D3d10Tools::target(d, 16, 16);
        Com<ID3D10RenderTargetView> fresh_view = D3d10Tools::rtv(d, fresh.get());
        if (!fresh || !fresh_view) {
            problems.add("a fresh render target cannot be created after ClearState");
        } else {
            D3d10Tools::clear(d, fresh_view.get(), 0, 1, 0, 1);
            device->Flush();
            if (!D3d10Tools::read_texture(d, fresh.get(), image, error) || !Kit::uniform(image, D3d10Tools::bgra(0, 255, 0, 255), where)) {
                problems.add("the device does not clear and read back a fresh target after ClearState: " + where + error);
            }
        }
    }
    context.measure("checked", 1);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d10
