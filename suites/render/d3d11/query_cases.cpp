#include "render/d3d11/draw2d.hpp"

namespace lsw::suites::render::d3d11 {

namespace {

template <class T>
bool fetch(Rig& r, ID3D11Query* query, T& value) {
    for (int spin = 0; spin < 200000; ++spin) {
        const HRESULT result = r.ctx->GetData(query, &value, sizeof(T), 0);
        if (result == S_OK) return true;
        if (FAILED(result)) return r.fail("GetData failed: " + D3d11Tools::hr(result));
        if (spin % 64 == 0) r.ctx->Flush();
    }
    return r.fail("GetData never completed");
}

Com<ID3D11Query> make_query(Rig& r, D3D11_QUERY type) {
    D3D11_QUERY_DESC d{type, 0};
    Com<ID3D11Query> q;
    r.ok(r.dev->CreateQuery(&d, q.put()), "CreateQuery");
    return q;
}

Com<ID3D11Predicate> make_predicate(Rig& r, D3D11_QUERY type) {
    D3D11_QUERY_DESC d{type, 0};
    Com<ID3D11Predicate> q;
    r.ok(r.dev->CreatePredicate(&d, q.put()), "CreatePredicate");
    return q;
}

struct Wall {
    Com<ID3D11VertexShader> vs;
    Com<ID3D11PixelShader> ps;
    Com<ID3D11Buffer> cb;
    Com<ID3D11DepthStencilState> ds;
    bool make(Rig& r) {
        vs = r.vs(
            "cbuffer C : register(b0) { float4 rect; float4 color; };\nstruct VO { float4 p : SV_Position; float4 c : COLOR; };\n"
            "VO main(uint id : SV_VertexID) { VO o; float2 t = float2(id & 1, id >> 1); o.p = float4(lerp(rect.xy, rect.zw, t), color.w, 1); o.c = float4(color.rgb, 1); return o; }\n");
        ps = r.ps("struct VO { float4 p : SV_Position; float4 c : COLOR; };\nfloat4 main(VO i) : SV_Target { return i.c; }\n");
        cb = r.buf(32, D3D11_BIND_CONSTANT_BUFFER);
        ds = r.dss(ddesc(true, D3D11_COMPARISON_LESS, true));
        return vs && ps && cb && ds;
    }
    void bind(Rig& r) {
        ID3D11Buffer* b = cb.get();
        r.ctx->IASetInputLayout(nullptr);
        r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        r.ctx->VSSetShader(vs.get(), nullptr, 0);
        r.ctx->VSSetConstantBuffers(0, 1, &b);
        r.ctx->PSSetShader(ps.get(), nullptr, 0);
        r.ctx->OMSetDepthStencilState(ds.get(), 0);
    }
    void draw(Rig& r, float x0, float y0, float x1, float y1, float z, float cr, float cg, float cb_) {
        const float data[8] = {x0, y0, x1, y1, cr, cg, cb_, z};
        r.ctx->UpdateSubresource(cb.get(), 0, nullptr, data, 0, 0);
        r.ctx->Draw(4, 0);
    }
};

struct OcclusionState {
    Target target;
    Depth depth;
    Wall wall;
    Com<ID3D11Query> query;
    UINT64 last = 1;

    std::string needs(Rig&) { return {}; }
    bool init(Rig& r) {
        query = make_query(r, D3D11_QUERY_OCCLUSION);
        return query && r.color(target) && r.depth(depth) && wall.make(r);
    }
    bool step(Rig& r, unsigned frame) {
        r.bind(target, &depth);
        wall.bind(r);
        const float edge = -0.55f + 0.22f * static_cast<float>(frame);
        wall.draw(r, -1.0f, -1.0f, edge, 1.0f, 0.3f, 0.5f, 0.55f, 0.6f);
        r.ctx->Begin(query.get());
        const float tint = last > 0 ? 1.0f : 0.0f;
        wall.draw(r, -0.2f, -0.35f, 0.2f, 0.35f, 0.6f, tint, 1.0f - tint, 0.2f);
        r.ctx->End(query.get());
        wall.draw(r, -0.9f, -0.95f, 0.9f, -0.8f, 0.4f, 0.2f, 0.3f + 0.1f * static_cast<float>(frame), 0.9f);
        UINT64 samples = 0;
        if (!fetch(r, query.get(), samples)) return false;
        last = samples;
        return true;
    }
    bool shot(Rig& r, unsigned, Cap& out) { return r.shot(out, target); }
};
const AddState<OcclusionState> kOcclusion(spec("query", "occlusion_result_drives_next_frame").frames(7, {0, 2, 3, 4, 6}).tolerance(rr::Tolerance::blend(3).with_edge(1)));

bool predicate_scene(Rig& r, Cap& out, bool predicate_value, bool occluded) {
    Target target;
    Depth depth;
    Wall wall;
    Com<ID3D11Predicate> query = make_predicate(r, D3D11_QUERY_OCCLUSION_PREDICATE);
    if (!query || !r.color(target) || !r.depth(depth) || !wall.make(r)) return false;
    r.bind(target, &depth);
    wall.bind(r);
    wall.draw(r, -1.0f, -1.0f, occluded ? 0.0f : -0.8f, 1.0f, 0.3f, 0.5f, 0.55f, 0.6f);
    r.ctx->Begin(query.get());
    wall.draw(r, -0.5f, -0.3f, -0.1f, 0.3f, 0.6f, 0.0f, 0.0f, 0.0f);
    r.ctx->End(query.get());
    r.ctx->SetPredication(query.get(), predicate_value);
    wall.draw(r, 0.15f, -0.6f, 0.85f, 0.6f, 0.5f, 0.9f, 0.6f, 0.1f);
    r.ctx->SetPredication(nullptr, FALSE);
    wall.draw(r, -0.9f, -0.95f, 0.9f, -0.8f, 0.4f, 0.2f, 0.3f, 0.9f);
    return r.shot(out, target);
}
const Add kPredVisible(spec("query", "predicated_draw_visible_object_value_false").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) { return predicate_scene(r, out, FALSE, false); });
const Add kPredHidden(spec("query", "predicated_draw_hidden_object_value_false").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) { return predicate_scene(r, out, FALSE, true); });
const Add kPredHiddenTrue(spec("query", "predicated_draw_hidden_object_value_true").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) { return predicate_scene(r, out, TRUE, true); });

const Add kSoQueries(spec("query", "stream_output_statistics_and_overflow_predicate").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic, pass;
    const char* so_vs = "struct VI { float2 p : POSITION; float3 c : COLOR; };\nstruct VO { float4 p : SV_Position; float3 c : COLOR; };\nVO main(VI i) { VO o; o.p = float4(i.p, 0, 1); o.c = i.c; return o; }\n";
    Bytes code;
    Com<ID3D11GeometryShader> geometry = r.vs_so(so_vs, {{0, "SV_Position", 0, 0, 4, 0}, {0, "COLOR", 0, 0, 3, 0}}, {28}, D3D11_SO_NO_RASTERIZED_STREAM, &code);
    Com<ID3D11VertexShader> vs = r.vs(so_vs);
    Com<ID3D11InputLayout> il = r.layout(layout2d(), code);
    Com<ID3D11Buffer> small = r.buf(28 * 9, D3D11_BIND_STREAM_OUTPUT);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(5, 0.8f));
    Com<ID3D11Query> stats = make_query(r, D3D11_QUERY_SO_STATISTICS);
    Com<ID3D11Predicate> overflow = make_predicate(r, D3D11_QUERY_SO_OVERFLOW_PREDICATE);
    Wall wall;
    if (!geometry || !vs || !il || !small || !vb || !stats || !overflow || !wall.make(r) || !r.color(target)) return false;
    r.bind(target);
    const UINT stride = sizeof(Vtx), offset = 0;
    ID3D11Buffer* b = vb.get();
    r.ctx->IASetInputLayout(il.get());
    r.ctx->IASetVertexBuffers(0, 1, &b, &stride, &offset);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.ctx->VSSetShader(vs.get(), nullptr, 0);
    r.ctx->GSSetShader(geometry.get(), nullptr, 0);
    r.ctx->PSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* t = small.get();
    const UINT zero = 0;
    r.ctx->SOSetTargets(1, &t, &zero);
    r.ctx->Begin(stats.get());
    r.ctx->Begin(overflow.get());
    r.ctx->Draw(15, 0);
    r.ctx->End(overflow.get());
    r.ctx->End(stats.get());
    r.ctx->SOSetTargets(0, nullptr, nullptr);
    r.ctx->GSSetShader(nullptr, nullptr, 0);
    D3D11_QUERY_DATA_SO_STATISTICS so{};
    BOOL over = FALSE;
    if (!fetch(r, stats.get(), so) || !fetch(r, overflow.get(), over)) return false;
    if (so.NumPrimitivesWritten != 3 || so.PrimitivesStorageNeeded != 5 || !over) {
        return r.fail("stream output statistics: written " + std::to_string(so.NumPrimitivesWritten) + " needed " + std::to_string(so.PrimitivesStorageNeeded) + " overflow " + std::to_string(over));
    }
    wall.bind(r);
    r.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    wall.draw(r, -0.9f, -0.2f, -0.1f, 0.6f, 0.5f, 0.2f, 0.8f, 0.3f);
    r.ctx->SetPredication(overflow.get(), TRUE);
    wall.draw(r, 0.1f, -0.2f, 0.9f, 0.6f, 0.5f, 0.9f, 0.2f, 0.2f);
    r.ctx->SetPredication(nullptr, FALSE);
    return r.shot(out, target);
});

const Add kTimestamps(spec("query", "timestamp_disjoint_does_not_alter_image").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    Com<ID3D11Query> disjoint = make_query(r, D3D11_QUERY_TIMESTAMP_DISJOINT);
    Com<ID3D11Query> t0 = make_query(r, D3D11_QUERY_TIMESTAMP);
    Com<ID3D11Query> t1 = make_query(r, D3D11_QUERY_TIMESTAMP);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(8, 0.85f));
    if (!disjoint || !t0 || !t1 || !vb || !basic.make(r) || !r.color(target)) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Begin(disjoint.get());
    r.ctx->End(t0.get());
    r.ctx->Draw(24, 0);
    r.ctx->End(t1.get());
    r.ctx->End(disjoint.get());
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT d{};
    UINT64 a = 0, b = 0;
    if (!fetch(r, disjoint.get(), d) || !fetch(r, t0.get(), a) || !fetch(r, t1.get(), b)) return false;
    if (d.Frequency == 0) return r.fail("timestamp frequency is zero");
    if (!d.Disjoint && b < a) return r.fail("timestamps run backwards inside a non-disjoint interval");
    return r.shot(out, target);
});

const Add kPipelineStats(spec("query", "pipeline_statistics_vertex_and_primitive_counts").tolerance(rr::Tolerance::blend(2)), [](Rig& r, Cap& out) {
    Target target;
    Basic basic;
    Com<ID3D11Query> q = make_query(r, D3D11_QUERY_PIPELINE_STATISTICS);
    Com<ID3D11Buffer> vb = r.vbuf(ring_fan(6, 0.85f));
    if (!q || !vb || !basic.make(r) || !r.color(target)) return false;
    r.bind(target);
    basic.bind(r, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    basic.vertices(r, vb.get());
    r.ctx->Begin(q.get());
    r.ctx->Draw(18, 0);
    r.ctx->DrawInstanced(9, 2, 0, 0);
    r.ctx->End(q.get());
    D3D11_QUERY_DATA_PIPELINE_STATISTICS s{};
    if (!fetch(r, q.get(), s)) return false;
    if (s.IAVertices != 18 + 18 || s.IAPrimitives != 6 + 6 || s.PSInvocations == 0 || s.CInvocations < 12) {
        return r.fail("pipeline statistics: IAVertices " + std::to_string(s.IAVertices) + " IAPrimitives " + std::to_string(s.IAPrimitives) + " PS " + std::to_string(s.PSInvocations) + " C " + std::to_string(s.CInvocations));
    }
    return r.shot(out, target);
});

}  // namespace

}  // namespace lsw::suites::render::d3d11
