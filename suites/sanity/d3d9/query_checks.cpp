#include "sanity/d3d9/query_checks.hpp"

#include "sanity/d3d9/kit.hpp"

#include <chrono>

namespace lsw::suites::sanity::d3d9 {

namespace {

bool poll(IDirect3DQuery9* query, void* data, DWORD size, int timeout_ms, HRESULT& last) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    DWORD flags = D3DGETDATA_FLUSH;
    while ((last = query->GetData(data, size, flags)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        flags = 0;
        ::SwitchToThread();
    }
    return SUCCEEDED(last);
}

}  // namespace

Outcome QueryChecks::occlusion(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    const HRESULT support = device->CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr);
    Com<IDirect3DQuery9> probe;
    const HRESULT creation = device->CreateQuery(D3DQUERYTYPE_OCCLUSION, probe.put());
    if (support == D3DERR_NOTAVAILABLE) {
        Kit::expect(problems, "CreateQuery(OCCLUSION) after CreateQuery(NULL) reported it unavailable", creation, D3DERR_NOTAVAILABLE);
        return problems.empty() ? Context::skip("occlusion queries are consistently reported unavailable") : problems.outcome();
    }
    Kit::expect(problems, "CreateQuery(OCCLUSION, NULL)", support, D3D_OK);
    if (FAILED(creation) || !probe) {
        return Outcome::fail("occlusion queries are reported available but cannot be created: " + Kit::hr(creation));
    }
    if (probe->GetType() != D3DQUERYTYPE_OCCLUSION || probe->GetDataSize() != sizeof(DWORD)) {
        problems.add("the occlusion query reports type " + std::to_string(probe->GetType()) + " and data size " + std::to_string(probe->GetDataSize()));
    }
    D3DFORMAT depth_format = D3DFMT_D24S8;
    if (!Kit::has_depth(rig, depth_format)) {
        depth_format = D3DFMT_D16;
    }
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, 64, 64);
    Com<IDirect3DSurface9> depth;
    device->CreateDepthStencilSurface(64, 64, depth_format, D3DMULTISAMPLE_NONE, 0, FALSE, depth.put(), nullptr);
    if (!target || !depth) {
        return Outcome::fail("the render target or depth buffer cannot be created");
    }
    Com<IDirect3DSurface9> saved_target;
    Com<IDirect3DSurface9> saved_depth;
    device->GetRenderTarget(0, saved_target.put());
    device->GetDepthStencilSurface(saved_depth.put());
    device->SetRenderTarget(0, target.get());
    device->SetDepthStencilSurface(depth.get());
    D3d9Tools::prepare(device, false);
    device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFF000000u, 1.0f, 0);
    device->SetRenderState(D3DRS_ZENABLE, TRUE);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 64, 32, 0xFF404040u, 0.2f));
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    struct Probe {
        const char* name;
        float l, t, r, b, z;
        DWORD expected;
    };
    const Probe probes[] = {{"a quad half hidden behind an occluder", 16, 16, 48, 48, 0.8f, 32 * 16},
                            {"a quad fully hidden", 8, 8, 24, 24, 0.8f, 0},
                            {"an unobstructed quad", 0, 40, 64, 64, 0.5f, 64 * 24},
                            {"a one pixel quad", 33, 50, 34, 51, 0.5f, 1}};
    for (const Probe& p : probes) {
        Com<IDirect3DQuery9> query;
        if (FAILED(device->CreateQuery(D3DQUERYTYPE_OCCLUSION, query.put()))) {
            problems.add("a second occlusion query cannot be created");
            break;
        }
        query->Issue(D3DISSUE_BEGIN);
        static_cast<void>(D3d9Tools::draw_quad(device, p.l, p.t, p.r, p.b, 0xFFFFFFFFu, p.z));
        query->Issue(D3DISSUE_END);
        DWORD count = 0xFFFFFFFFu;
        HRESULT last = S_OK;
        const auto start = std::chrono::steady_clock::now();
        if (!poll(query.get(), &count, sizeof(count), 5000, last)) {
            problems.add(std::string(p.name) + ": GetData(FLUSH) did not complete in 5 s (" + Kit::hr(last) + ")");
            continue;
        }
        context.measure(std::string("poll_ms.") + std::to_string(&p - probes), std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        if (count != p.expected) {
            problems.add(std::string(p.name) + " reports " + std::to_string(count) + " passing samples, expected " + std::to_string(p.expected));
        }
    }
    device->SetRenderTarget(0, saved_target.get());
    device->SetDepthStencilSurface(saved_depth.get());
    Kit::check_usable(problems, "after the occlusion queries", device);
    return problems.outcome();
}

Outcome QueryChecks::event_timestamp(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    const D3DQUERYTYPE types[] = {D3DQUERYTYPE_VCACHE, D3DQUERYTYPE_RESOURCEMANAGER, D3DQUERYTYPE_VERTEXSTATS, D3DQUERYTYPE_EVENT, D3DQUERYTYPE_OCCLUSION,
                                  D3DQUERYTYPE_TIMESTAMP, D3DQUERYTYPE_TIMESTAMPDISJOINT, D3DQUERYTYPE_TIMESTAMPFREQ, D3DQUERYTYPE_PIPELINETIMINGS,
                                  D3DQUERYTYPE_INTERFACETIMINGS, D3DQUERYTYPE_VERTEXTIMINGS, D3DQUERYTYPE_PIXELTIMINGS, D3DQUERYTYPE_BANDWIDTHTIMINGS,
                                  D3DQUERYTYPE_CACHEUTILIZATION, static_cast<D3DQUERYTYPE>(31)};
    int available = 0;
    for (const D3DQUERYTYPE type : types) {
        const std::string name = "query type " + std::to_string(static_cast<int>(type));
        const HRESULT support = device->CreateQuery(type, nullptr);
        Com<IDirect3DQuery9> query;
        const HRESULT creation = device->CreateQuery(type, query.put());
        if (support == D3D_OK) {
            ++available;
            if (FAILED(creation) || !query) {
                problems.add(name + " is reported available but CreateQuery fails with " + Kit::hr(creation));
            } else if (query->GetType() != type) {
                problems.add(name + " reports the type " + std::to_string(query->GetType()));
            }
        } else if (support == D3DERR_NOTAVAILABLE) {
            Kit::expect(problems, name + " CreateQuery after the NULL probe reported it unavailable", creation, D3DERR_NOTAVAILABLE);
            if (query) {
                problems.add(name + " returned a query object although it is reported unavailable");
            }
        } else {
            problems.add(name + ": CreateQuery(NULL) returned " + Kit::hr(support));
        }
    }
    context.measure("query_types_available", available);
    if (device->CreateQuery(D3DQUERYTYPE_EVENT, nullptr) != D3D_OK) {
        return Outcome::fail("EVENT queries are not available");
    }
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, 256, 256);
    Com<IDirect3DSurface9> saved;
    device->GetRenderTarget(0, saved.put());
    device->SetRenderTarget(0, target.get());
    D3d9Tools::prepare(device, false);
    D3d9Tools::clear(device, 0xFF000000u);
    for (int k = 0; k < 300; ++k) {
        static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 256, 256, Kit::frame_color(k)));
    }
    Com<IDirect3DQuery9> event;
    device->CreateQuery(D3DQUERYTYPE_EVENT, event.put());
    if (event->GetDataSize() != sizeof(BOOL)) {
        problems.add("the EVENT query data size is " + std::to_string(event->GetDataSize()));
    }
    event->Issue(D3DISSUE_END);
    BOOL done = FALSE;
    HRESULT last = S_OK;
    const HRESULT first_poll = event->GetData(&done, sizeof(done), 0);
    if (first_poll != S_OK && first_poll != S_FALSE) {
        problems.add("the first GetData(EVENT) returned " + Kit::hr(first_poll));
    }
    context.measure("event_complete_on_first_poll", first_poll == S_OK);
    if (first_poll != S_OK && !poll(event.get(), &done, sizeof(done), 20000, last)) {
        problems.add("the EVENT query never signalled: " + Kit::hr(last));
    } else {
        SceneImage image;
        std::string where;
        if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
            problems.add(error);
        } else if (!D3d9Tools::uniform(image, Kit::frame_color(299), 0, where)) {
            problems.add("the EVENT query signalled before the last draw completed: " + where);
        }
        if (done != TRUE) {
            problems.add("GetData(EVENT) succeeded with the value " + std::to_string(done));
        }
    }
    Com<IDirect3DQuery9> frequency;
    Com<IDirect3DQuery9> stamp_a;
    Com<IDirect3DQuery9> stamp_b;
    Com<IDirect3DQuery9> disjoint;
    if (device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, nullptr) == D3D_OK) {
        if (FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, frequency.put())) || FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, stamp_a.put())) ||
            FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP, stamp_b.put())) || FAILED(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, disjoint.put()))) {
            problems.add("TIMESTAMP is available but its companion queries cannot be created");
        } else {
            disjoint->Issue(D3DISSUE_BEGIN);
            stamp_a->Issue(D3DISSUE_END);
            for (int k = 0; k < 50; ++k) {
                static_cast<void>(D3d9Tools::draw_quad(device, 0, 0, 256, 256, Kit::frame_color(k)));
            }
            static_cast<void>(D3d9Tools::wait_event(device, error));
            stamp_b->Issue(D3DISSUE_END);
            disjoint->Issue(D3DISSUE_END);
            frequency->Issue(D3DISSUE_END);
            UINT64 a = 0;
            UINT64 b = 0;
            UINT64 hz = 0;
            BOOL jump = TRUE;
            if (!poll(stamp_a.get(), &a, sizeof(a), 5000, last) || !poll(stamp_b.get(), &b, sizeof(b), 5000, last) || !poll(frequency.get(), &hz, sizeof(hz), 5000, last) || !poll(disjoint.get(), &jump, sizeof(jump), 5000, last)) {
                problems.add("a timestamp query did not complete: " + Kit::hr(last));
            } else {
                if (hz == 0) {
                    problems.add("TIMESTAMPFREQ reports a zero frequency");
                }
                if (b < a) {
                    problems.add("the second timestamp " + std::to_string(b) + " is below the first " + std::to_string(a));
                }
                context.measure("timestamp_frequency_hz", static_cast<long long>(hz));
                if (hz != 0 && jump == FALSE) {
                    const double seconds = static_cast<double>(b - a) / static_cast<double>(hz);
                    context.measure("interval_ms", seconds * 1000.0);
                    if (seconds > 30.0) {
                        problems.add("two timestamps around a few draws are " + std::to_string(seconds) + " s apart");
                    }
                }
            }
        }
    } else {
        Kit::expect(problems, "CreateQuery(TIMESTAMPFREQ) when TIMESTAMP is unavailable", device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, nullptr), device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, nullptr));
    }
    device->SetRenderTarget(0, saved.get());
    Kit::check_usable(problems, "after the queries", device);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
