#include "render/d3d10/util.hpp"

namespace lsw::suites::render::d3d10 {
namespace {

Com<ID3D10Query> make_query(Rig& r, D3D10_QUERY type) {
    D3D10_QUERY_DESC qd{};
    qd.Query = type;
    Com<ID3D10Query> q;
    r.check(r.d->CreateQuery(&qd, q.put()), "CreateQuery");
    return q;
}

template <class T>
bool fetch(Rig& r, ID3D10Query* q, T& out) {
    r.d->Flush();
    for (int i = 0; i < 2000000; ++i) {
        const HRESULT h = q->GetData(&out, sizeof(T), 0);
        if (h == S_OK) return true;
        if (FAILED(h)) {
            r.fail("GetData failed");
            return false;
        }
        ::SwitchToThread();
    }
    r.fail("query did not complete");
    return false;
}

void number_image(rr::CaptureSet& o, const std::string& label, const std::vector<double>& values) {
    rr::Image image(static_cast<std::uint32_t>(values.size()), 1, rr::PixelKind::Rgba32Float);
    for (std::size_t i = 0; i < values.size(); ++i) image.set_channel(static_cast<std::uint32_t>(i), 0, 0, values[i]);
    o.add(label, std::move(image));
}

void so_overflow_scene(Rig& r, rr::CaptureSet& o, bool stats_query, bool predicate, bool drawauto) {
    r.begin();
    Com<ID3D10VertexShader> vs = r.vs("struct VI { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
                                      "struct VO { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\nVO main(VI i) { VO o; o.p = i.p; o.c = i.c; o.uv = i.uv; return o; }\n");
    Com<ID3D10InputLayout> layout = r.std_layout();
    Com<ID3D10GeometryShader> gs = r.gs_so(
        "struct VO { float3 p : POSITION; float4 c : COLOR; float2 uv : TEXCOORD0; };\n"
        "[maxvertexcount(6)] void main(triangle VO v[3], inout TriangleStream<VO> s) { [unroll] for (int i = 0; i < 3; ++i) s.Append(v[i]); s.RestartStrip(); [unroll] for (int j = 0; j < 3; ++j) { VO o = v[j]; o.p.x += 0.2; s.Append(o); } }\n",
        {{"POSITION", 0, 0, 3, 0}, {"COLOR", 0, 0, 4, 0}, {"TEXCOORD", 0, 0, 2, 0}}, sizeof(Vtx));
    auto a = Geo::triangle(-0.5f, 0, 0.4f, 0.5f, 0.9f, 0.3f, 0.2f);
    auto b = Geo::triangle(0.3f, 0, 0.4f, 0.5f, 0.2f, 0.4f, 0.9f);
    auto c = Geo::triangle(0, 0.5f, 0.3f, 0.5f, 0.2f, 0.9f, 0.4f);
    a.insert(a.end(), b.begin(), b.end());
    a.insert(a.end(), c.begin(), c.end());
    Com<ID3D10Buffer> input = r.vbuf(a);
    Com<ID3D10Buffer> sob = r.vbuf(nullptr, 5 * 3 * sizeof(Vtx), D3D10_BIND_STREAM_OUTPUT);
    Com<ID3D10Query> stats = make_query(r, D3D10_QUERY_SO_STATISTICS);
    Com<ID3D10Predicate> overflow;
    D3D10_QUERY_DESC qd{};
    qd.Query = D3D10_QUERY_SO_OVERFLOW_PREDICATE;
    r.check(r.d->CreatePredicate(&qd, overflow.put()), "CreatePredicate");
    Com<ID3D10DepthStencilState> no_depth = r.depth(Rig::depth_desc());
    if (!r.ok()) return;
    r.d->OMSetDepthStencilState(no_depth.get(), 0);
    r.d->IASetInputLayout(layout.get());
    r.d->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    r.d->VSSetShader(vs.get());
    r.d->GSSetShader(gs.get());
    r.d->PSSetShader(nullptr);
    r.std_vb(input.get());
    ID3D10Buffer* target = sob.get();
    const UINT offset = 0;
    r.d->SOSetTargets(1, &target, &offset);
    if (stats_query) stats->Begin();
    if (predicate) overflow->Begin();
    r.d->Draw(9, 0);
    if (predicate) overflow->End();
    if (stats_query) stats->End();
    D3D10_QUERY_DATA_SO_STATISTICS data{};
    BOOL over = FALSE;
    if (stats_query && !fetch(r, stats.get(), data)) return;
    if (predicate && !fetch(r, overflow.get(), over)) return;
    r.unbind_all();
    r.d->GSSetShader(nullptr);
    r.d->OMSetDepthStencilState(nullptr, 0);
    r.use_std();
    if (drawauto) {
        r.std_vb(sob.get());
        r.d->DrawAuto();
    } else {
        r.draw_std(Geo::quad(-0.9f, -0.9f, 0.5f, -0.4f, 0.5f, 0.5f, 0.5f, 0.5f));
    }
    if (predicate) {
        r.d->SetPredication(overflow.get(), FALSE);
        r.draw_std(Geo::quad(0.7f, 0.7f, 0.95f, 0.95f, 0.5f, 1, 0.2f, 0.2f));
        r.d->SetPredication(nullptr, FALSE);
    }
    r.cap_main(o);
    if (stats_query || predicate) {
        number_image(o, "stats", {static_cast<double>(data.NumPrimitivesWritten), static_cast<double>(data.PrimitivesStorageNeeded), over ? 1.0 : 0.0});
    }
}

struct OccState {
    Com<ID3D10Query> query;
    UINT64 last = 0;
};

const Registrar kQuery{[] {
    {
        MultiBody<OccState> body;
        body.setup = [](Rig& r, OccState& s) {
            r.begin();
            r.use_std();
            s.query = make_query(r, D3D10_QUERY_OCCLUSION);
            r.d->OMSetDepthStencilState(nullptr, 0);
        };
        body.frame = [](Rig& r, OccState& s, unsigned f) {
            r.clear(r.main, 0.08f, 0.08f, 0.12f, 1);
            const float shift = -0.9f + 0.25f * static_cast<float>(f);
            r.draw_std(Geo::quad(-0.9f, -0.9f, -0.4f, 0.9f, 0.5f, 0.7f, 0.7f, 0.7f));
            if (f > 0) {
                const bool bright = s.last > 600;
                r.draw_std(Geo::quad(-0.2f, 0.4f, 0.9f, 0.9f, 0.5f, bright ? 0.2f : 0.9f, bright ? 0.9f : 0.2f, 0.3f));
            }
            s.query->Begin();
            r.draw_std(Geo::quad(shift, -0.5f, shift + 0.6f, 0.3f, 0.5f, 0.3f, 0.5f, 0.95f));
            s.query->End();
            UINT64 count = 0;
            if (fetch(r, s.query.get(), count)) s.last = count;
        };
        multi<OccState>(spec("query", "occlusion_count_drives_next_frame").frames(8, {0, 3, 7}).tolerance(rr::Tolerance::blend()), body);
    }
    single(spec("query", "occlusion_pixel_count_exact").tolerance(rr::Tolerance::blend()).tolerance_for("count", rr::Tolerance::floats(0.5f, 0.0f)).uniform_ok("count"), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Query> q = make_query(r, D3D10_QUERY_OCCLUSION);
        if (!r.ok()) return;
        auto px = [](float x) { return x / 64.0f - 1.0f; };
        auto py = [](float y) { return 1.0f - y / 64.0f; };
        q->Begin();
        r.draw_std(Geo::quad(px(10), py(60), px(50), py(20), 0.5f, 0.9f, 0.5f, 0.2f));
        r.draw_std(Geo::quad(px(70), py(100), px(80), py(90), 0.5f, 0.2f, 0.5f, 0.9f));
        q->End();
        UINT64 count = 0;
        if (!fetch(r, q.get(), count)) return;
        r.cap_main(o);
        number_image(o, "count", {static_cast<double>(count)});
    });
    single(spec("query", "predicated_draw_hidden_and_visible").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin(128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, true);
        r.use_std();
        auto dd = Rig::depth_desc();
        dd.DepthEnable = TRUE;
        Com<ID3D10DepthStencilState> ds = r.depth(dd);
        auto no_color = Rig::blend_desc();
        for (int i = 0; i < 8; ++i) no_color.RenderTargetWriteMask[i] = 0;
        Com<ID3D10BlendState> mask = r.blend(no_color);
        Com<ID3D10BlendState> normal = r.blend(Rig::blend_desc());
        Com<ID3D10Predicate> hidden;
        Com<ID3D10Predicate> shown;
        D3D10_QUERY_DESC qd{};
        qd.Query = D3D10_QUERY_OCCLUSION_PREDICATE;
        r.check(r.d->CreatePredicate(&qd, hidden.put()), "CreatePredicate");
        r.check(r.d->CreatePredicate(&qd, shown.put()), "CreatePredicate");
        if (!r.ok()) return;
        r.d->OMSetDepthStencilState(ds.get(), 0);
        const float f[4] = {1, 1, 1, 1};
        r.draw_std(Geo::quad(-0.9f, -0.9f, 0.2f, 0.9f, 0.2f, 0.6f, 0.6f, 0.6f));
        r.d->OMSetBlendState(mask.get(), f, 0xffffffffu);
        hidden->Begin();
        r.draw_std(Geo::quad(-0.7f, -0.5f, 0.0f, 0.5f, 0.6f, 1, 1, 1));
        hidden->End();
        shown->Begin();
        r.draw_std(Geo::quad(0.3f, -0.5f, 0.9f, 0.5f, 0.6f, 1, 1, 1));
        shown->End();
        r.d->OMSetBlendState(normal.get(), f, 0xffffffffu);
        r.d->SetPredication(hidden.get(), FALSE);
        r.draw_std(Geo::quad(-0.7f, -0.5f, 0.0f, 0.5f, 0.6f, 0.9f, 0.2f, 0.2f));
        r.d->SetPredication(shown.get(), FALSE);
        r.draw_std(Geo::quad(0.3f, -0.5f, 0.9f, 0.5f, 0.6f, 0.2f, 0.9f, 0.2f));
        r.d->SetPredication(hidden.get(), TRUE);
        r.draw_std(Geo::quad(-0.5f, 0.6f, -0.1f, 0.9f, 0.1f, 0.9f, 0.9f, 0.2f));
        r.d->SetPredication(nullptr, FALSE);
        r.cap_main(o);
    });
    single(spec("query", "so_statistics_written_and_needed").tolerance(rr::Tolerance::blend()).tolerance_for("stats", rr::Tolerance::floats(0.5f, 0.0f)).uniform_ok("stats"),
           [](Rig& r, rr::CaptureSet& o) { so_overflow_scene(r, o, true, false, false); });
    single(spec("query", "so_overflow_predicate_controls_draw").tolerance(rr::Tolerance::blend()).tolerance_for("stats", rr::Tolerance::floats(0.5f, 0.0f)).uniform_ok("stats"),
           [](Rig& r, rr::CaptureSet& o) { so_overflow_scene(r, o, false, true, false); });
    single(spec("query", "so_drawauto_after_overflow").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) { so_overflow_scene(r, o, false, false, true); });
    single(spec("query", "pipeline_statistics_counts").tolerance(rr::Tolerance::blend()).tolerance_for("stats", rr::Tolerance::floats(0.5f, 0.0f)).uniform_ok("stats"), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Query> q = make_query(r, D3D10_QUERY_PIPELINE_STATISTICS);
        if (!r.ok()) return;
        q->Begin();
        r.draw_std(Geo::triangle(-0.4f, 0.3f, 0.4f, 0.5f, 0.9f, 0.3f, 0.2f));
        r.draw_std(Geo::quad(-0.2f, -0.9f, 0.9f, 0.1f, 0.5f, 0.2f, 0.4f, 0.9f));
        q->End();
        D3D10_QUERY_DATA_PIPELINE_STATISTICS stats{};
        if (!fetch(r, q.get(), stats)) return;
        r.cap_main(o);
        number_image(o, "stats", {static_cast<double>(stats.IAVertices), static_cast<double>(stats.IAPrimitives), static_cast<double>(stats.VSInvocations),
                                  static_cast<double>(stats.GSInvocations), static_cast<double>(stats.GSPrimitives)});
    });
    single(spec("query", "event_and_timestamp_disjoint").tolerance(rr::Tolerance::blend()), [](Rig& r, rr::CaptureSet& o) {
        r.begin();
        r.use_std();
        Com<ID3D10Query> disjoint = make_query(r, D3D10_QUERY_TIMESTAMP_DISJOINT);
        Com<ID3D10Query> t0 = make_query(r, D3D10_QUERY_TIMESTAMP);
        Com<ID3D10Query> t1 = make_query(r, D3D10_QUERY_TIMESTAMP);
        Com<ID3D10Query> event = make_query(r, D3D10_QUERY_EVENT);
        if (!r.ok()) return;
        disjoint->Begin();
        t0->End();
        r.draw_std(Geo::triangle(0, 0, 0.8f, 0.5f, 0.9f, 0.3f, 0.2f));
        t1->End();
        disjoint->End();
        event->End();
        BOOL done = FALSE;
        if (!fetch(r, event.get(), done)) return;
        D3D10_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 a = 0, b = 0;
        if (!fetch(r, disjoint.get(), dj) || !fetch(r, t0.get(), a) || !fetch(r, t1.get(), b)) return;
        if (dj.Frequency == 0) {
            r.fail("timestamp frequency is zero");
            return;
        }
        if (!dj.Disjoint && b < a) {
            r.fail("timestamps run backwards");
            return;
        }
        r.cap_main(o);
    });
}};

}  // namespace
}  // namespace lsw::suites::render::d3d10
