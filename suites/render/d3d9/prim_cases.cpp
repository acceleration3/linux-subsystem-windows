#include "render/d3d9/harness.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

struct P {
    float x, y, z, rhw;
    DWORD color;
};
constexpr DWORD kPFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

DWORD hue(unsigned i, unsigned n) {
    const float t = static_cast<float>(i) / static_cast<float>(n);
    const auto ch = [&](float o) { return static_cast<unsigned>(127.5f + 127.5f * std::sin(6.2831853f * (t + o))); };
    return Dx::kColor(ch(0.0f), ch(0.333f), ch(0.667f));
}

std::vector<P> ring(float cx, float cy, float r, unsigned n, bool with_center) {
    std::vector<P> v;
    if (with_center) {
        v.push_back({cx, cy, 0.5f, 1, 0xFFFFFFFF});
    }
    for (unsigned i = 0; i < n; ++i) {
        const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
        v.push_back({cx + r * std::cos(a), cy + r * std::sin(a), 0.5f, 1, hue(i, n)});
    }
    return v;
}

void primitive_cases() {
    struct Type {
        const char* name;
        D3DPRIMITIVETYPE type;
    };
    const Type types[] = {{"pointlist", D3DPT_POINTLIST}, {"linelist", D3DPT_LINELIST}, {"linestrip", D3DPT_LINESTRIP}, {"trianglelist", D3DPT_TRIANGLELIST},
                          {"trianglestrip", D3DPT_TRIANGLESTRIP}, {"trianglefan", D3DPT_TRIANGLEFAN}};
    for (const Type& t : types) {
        Cases::single(rr::CaseSpec("prim", std::string("draw_vb_") + t.name).tolerance(rr::Tolerance::raster(4, 1)), [t](Ctx& c) {
            c.clear(Dx::kColor(15, 15, 40));
            std::vector<P> v;
            UINT count = 0;
            switch (t.type) {
                case D3DPT_POINTLIST:
                    v = ring(64, 64, 48, 16, true);
                    count = static_cast<UINT>(v.size());
                    c.rsf(D3DRS_POINTSIZE, 5.0f);
                    break;
                case D3DPT_LINELIST:
                    v = ring(64, 64, 52, 12, false);
                    count = 6;
                    break;
                case D3DPT_LINESTRIP:
                    v = ring(64, 64, 52, 12, false);
                    count = 11;
                    break;
                case D3DPT_TRIANGLELIST:
                    v = ring(64, 64, 52, 12, false);
                    count = 4;
                    break;
                case D3DPT_TRIANGLESTRIP:
                    v = ring(64, 64, 52, 12, false);
                    count = 10;
                    break;
                default:
                    v = ring(64, 64, 52, 12, true);
                    v.push_back(v[1]);
                    count = 12;
                    break;
            }
            Com<IDirect3DVertexBuffer9> vb = c.vbuffer(v.data(), static_cast<UINT>(v.size() * sizeof(P)));
            if (!vb) {
                return;
            }
            c.fvf(kPFvf);
            c.chk(c.d->SetStreamSource(0, vb.get(), 0, sizeof(P)), "SetStreamSource");
            c.chk(c.d->DrawPrimitive(t.type, 0, t.type == D3DPT_POINTLIST ? count : t.type == D3DPT_LINELIST ? 6 : count), "DrawPrimitive");
        });
    }

    const auto indexed_mesh = [](Ctx& c, bool wide, UINT base, UINT min_index) {
        c.clear(Dx::kColor(15, 15, 40));
        std::vector<P> v(base, P{0, 0, 0.5f, 1, 0xFF000000});
        const std::vector<P> r = ring(64, 64, 54, 10, true);
        v.insert(v.end(), r.begin(), r.end());
        std::vector<std::uint32_t> idx32;
        for (unsigned i = 0; i < 10; ++i) {
            idx32.push_back(min_index);
            idx32.push_back(min_index + 1 + i);
            idx32.push_back(min_index + 1 + (i + 1) % 10);
        }
        std::vector<std::uint16_t> idx16(idx32.begin(), idx32.end());
        Com<IDirect3DVertexBuffer9> vb = c.vbuffer(v.data(), static_cast<UINT>(v.size() * sizeof(P)));
        Com<IDirect3DIndexBuffer9> ib = wide ? c.ibuffer(idx32.data(), static_cast<UINT>(idx32.size() * 4), D3DFMT_INDEX32)
                                             : c.ibuffer(idx16.data(), static_cast<UINT>(idx16.size() * 2), D3DFMT_INDEX16);
        if (!vb || !ib) {
            return;
        }
        c.fvf(kPFvf);
        c.chk(c.d->SetStreamSource(0, vb.get(), 0, sizeof(P)), "SetStreamSource");
        c.chk(c.d->SetIndices(ib.get()), "SetIndices");
        c.chk(c.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(base) - static_cast<INT>(min_index), min_index, 11, 0, 10), "DrawIndexedPrimitive");
    };
    Cases::single(rr::CaseSpec("prim", "draw_indexed_16bit"), [indexed_mesh](Ctx& c) { indexed_mesh(c, false, 0, 0); });
    Cases::single(rr::CaseSpec("prim", "draw_indexed_16bit_base_vertex"), [indexed_mesh](Ctx& c) { indexed_mesh(c, false, 7, 0); });
    Cases::single(rr::CaseSpec("prim", "draw_indexed_16bit_min_index"), [indexed_mesh](Ctx& c) { indexed_mesh(c, false, 5, 3); });
    Cases::single(rr::CaseSpec("prim", "draw_indexed_32bit"), [indexed_mesh](Ctx& c) { indexed_mesh(c, true, 3, 0); }, {},
                  Needs::caps([](const D3DCAPS9& caps) { return caps.MaxVertexIndex > 0xFFFF; }, "32-bit indices"));

    Cases::single(rr::CaseSpec("prim", "draw_primitive_up_fan"), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        std::vector<P> v = ring(64, 64, 52, 14, true);
        v.push_back(v[1]);
        c.fvf(kPFvf);
        c.draw(D3DPT_TRIANGLEFAN, 14, v.data(), sizeof(P));
    });
    Cases::single(rr::CaseSpec("prim", "draw_indexed_up_32bit"), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        const std::vector<P> v = ring(64, 64, 54, 9, true);
        std::vector<std::uint32_t> idx;
        for (unsigned i = 0; i < 9; ++i) {
            idx.push_back(0);
            idx.push_back(1 + i);
            idx.push_back(1 + (i + 1) % 9);
        }
        c.fvf(kPFvf);
        c.draw_idx(D3DPT_TRIANGLELIST, static_cast<UINT>(v.size()), 9, idx.data(), D3DFMT_INDEX32, v.data(), sizeof(P));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return caps.MaxVertexIndex > 0xFFFF; }, "32-bit indices"));
    Cases::single(rr::CaseSpec("prim", "draw_indexed_up_strip_16bit"), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        const std::vector<P> v = ring(64, 64, 54, 12, false);
        std::vector<std::uint16_t> idx;
        for (unsigned i = 0; i < 6; ++i) {
            idx.push_back(static_cast<std::uint16_t>(i));
            idx.push_back(static_cast<std::uint16_t>(11 - i));
        }
        c.fvf(kPFvf);
        c.draw_idx(D3DPT_TRIANGLESTRIP, static_cast<UINT>(v.size()), 10, idx.data(), D3DFMT_INDEX16, v.data(), sizeof(P));
    });

    Cases::single(rr::CaseSpec("prim", "process_vertices_to_buffer").tolerance(rr::Tolerance::blend(3)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        struct In { float x, y, z; DWORD color; };
        const In v[3] = {{-0.7f, -0.7f, 0.5f, 0xFFFF4040}, {0.0f, 0.8f, 0.5f, 0xFF40FF40}, {0.7f, -0.7f, 0.5f, 0xFF4040FF}};
        c.xf(D3DTS_WORLD, Xf());
        c.xf(D3DTS_VIEW, Xf());
        c.xf(D3DTS_PROJECTION, Xf());
        Com<IDirect3DVertexBuffer9> src = c.vbuffer(v, sizeof v, 0, D3DPOOL_SYSTEMMEM);
        Com<IDirect3DVertexBuffer9> dst;
        c.chk(c.d->CreateVertexBuffer(sizeof(P) * 3, 0, kPFvf, D3DPOOL_SYSTEMMEM, dst.put(), nullptr), "CreateVertexBuffer dest");
        if (!src || !dst) {
            return;
        }
        c.fvf(D3DFVF_XYZ | D3DFVF_DIFFUSE);
        c.chk(c.d->SetStreamSource(0, src.get(), 0, sizeof(In)), "SetStreamSource");
        Com<IDirect3DVertexDeclaration9> none;
        c.chk(c.d->ProcessVertices(0, 0, 3, dst.get(), nullptr, 0), "ProcessVertices");
        c.fvf(kPFvf);
        c.chk(c.d->SetStreamSource(0, dst.get(), 0, sizeof(P)), "SetStreamSource");
        c.chk(c.d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1), "DrawPrimitive");
    });

    Cases::single(rr::CaseSpec("prim", "npatch_tessellation").tolerance(rr::Tolerance::blend(6).with_edge(1)), [](Ctx& c) {
        c.clear(Dx::kColor(15, 15, 40));
        c.camera(4.0f);
        c.rs(D3DRS_ZENABLE, D3DZB_TRUE);
        c.rs(D3DRS_LIGHTING, TRUE);
        c.rs(D3DRS_AMBIENT, 0xFF404040);
        c.material(Dx::material(0.9f, 0.7f, 0.5f, 0.9f, 0.7f, 0.5f));
        c.light(0, Dx::directional(0.3f, -0.4f, 1.0f, 1, 1, 1));
        c.chk(c.d->SetNPatchMode(4.0f), "SetNPatchMode");
        c.fvf(D3DFVF_XYZ | D3DFVF_NORMAL);
        struct V { float x, y, z, nx, ny, nz; };
        const Mesh m = Geo::sphere(6, 4, 1.3f, 0xFFFFFFFF);
        std::vector<V> v;
        for (const Vtx& q : m.vertices) {
            v.push_back({q.x, q.y, q.z, q.nx, q.ny, q.nz});
        }
        c.draw_idx(D3DPT_TRIANGLELIST, static_cast<UINT>(v.size()), m.triangles(), m.indices.data(), D3DFMT_INDEX16, v.data(), sizeof(V));
    }, {}, Needs::caps([](const D3DCAPS9& caps) { return (caps.DevCaps & D3DDEVCAPS_NPATCHES) != 0; }, "N-patches"));
}

void dynamic_cases() {
    for (int mode = 0; mode < 3; ++mode) {
        const char* names[3] = {"dynamic_vb_noverwrite_append", "dynamic_vb_discard_each_frame", "dynamic_vb_point_sprites_discard"};
        Cases::frames(rr::CaseSpec("prim", names[mode]).frames(8, {7}).tolerance(rr::Tolerance::raster(4, 1)), [mode] {
            struct State {
                Com<IDirect3DVertexBuffer9> vb;
                UINT cursor = 0;
            };
            auto st = std::make_shared<State>();
            return FrameBody([mode, st](Ctx& c, unsigned f) {
                constexpr UINT kCapacity = 256;
                if (f == 0) {
                    c.chk(c.d->CreateVertexBuffer(kCapacity * sizeof(P), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY | (mode == 2 ? D3DUSAGE_POINTS : 0), kPFvf, D3DPOOL_DEFAULT, st->vb.put(), nullptr), "CreateVertexBuffer");
                    c.clear(Dx::kColor(15, 15, 40));
                }
                if (!st->vb) {
                    return;
                }
                const unsigned n = mode == 2 ? 24 : 18;
                std::vector<P> v;
                for (unsigned i = 0; i < n; ++i) {
                    const float a = 0.45f * static_cast<float>(i) + 0.3f * static_cast<float>(f);
                    const float r = 10.0f + 4.0f * static_cast<float>(i);
                    if (mode == 2) {
                        v.push_back({64 + r * std::cos(a), 64 + r * std::sin(a), 0.5f, 1, hue(i, n)});
                    } else if (i % 3 == 0) {
                        const float x = 64 + 40 * std::cos(a);
                        const float y = 64 + 40 * std::sin(a);
                        v.push_back({x, y, 0.5f, 1, hue(i, n)});
                        v.push_back({x + 18, y + 4, 0.5f, 1, hue(i + 1, n)});
                        v.push_back({x + 6, y + 20, 0.5f, 1, hue(i + 2, n)});
                    }
                }
                const UINT bytes = static_cast<UINT>(v.size() * sizeof(P));
                DWORD flags = D3DLOCK_DISCARD;
                if (mode == 0) {
                    if (st->cursor + v.size() > kCapacity) {
                        st->cursor = 0;
                    } else if (st->cursor != 0) {
                        flags = D3DLOCK_NOOVERWRITE;
                    }
                } else {
                    st->cursor = 0;
                }
                void* data = nullptr;
                c.chk(st->vb->Lock(st->cursor * sizeof(P), bytes, &data, flags), "Lock");
                if (data == nullptr) {
                    return;
                }
                std::memcpy(data, v.data(), bytes);
                st->vb->Unlock();
                c.clear(Dx::kColor(15, 15, 40), D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
                c.fvf(kPFvf);
                c.chk(c.d->SetStreamSource(0, st->vb.get(), 0, sizeof(P)), "SetStreamSource");
                if (mode == 2) {
                    c.rs(D3DRS_POINTSPRITEENABLE, TRUE);
                    c.rsf(D3DRS_POINTSIZE, 6.0f);
                    c.chk(c.d->DrawPrimitive(D3DPT_POINTLIST, st->cursor, static_cast<UINT>(v.size())), "DrawPrimitive");
                } else {
                    c.chk(c.d->DrawPrimitive(D3DPT_TRIANGLELIST, st->cursor, static_cast<UINT>(v.size() / 3)), "DrawPrimitive");
                }
                st->cursor += static_cast<UINT>(v.size());
            });
        });
    }
    Cases::frames(rr::CaseSpec("prim", "index_buffer_rewrite_each_frame").frames(6, {5}).tolerance(rr::Tolerance::blend(2)), [] {
        struct State {
            Com<IDirect3DVertexBuffer9> vb;
            Com<IDirect3DIndexBuffer9> ib;
        };
        auto st = std::make_shared<State>();
        return FrameBody([st](Ctx& c, unsigned f) {
            const std::vector<P> v = ring(64, 64, 54, 12, true);
            if (f == 0) {
                st->vb = c.vbuffer(v.data(), static_cast<UINT>(v.size() * sizeof(P)));
                c.chk(c.d->CreateIndexBuffer(12 * 3 * 2, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, st->ib.put(), nullptr), "CreateIndexBuffer");
            }
            if (!st->vb || !st->ib) {
                return;
            }
            std::uint16_t* idx = nullptr;
            c.chk(st->ib->Lock(0, 0, reinterpret_cast<void**>(&idx), D3DLOCK_DISCARD), "Lock");
            if (idx == nullptr) {
                return;
            }
            for (unsigned i = 0; i < 12; ++i) {
                const unsigned shown = (i + f) % 3 == 0 ? 0 : 1;
                idx[i * 3] = 0;
                idx[i * 3 + 1] = static_cast<std::uint16_t>(shown ? 1 + i : 1 + (i + 1) % 12);
                idx[i * 3 + 2] = static_cast<std::uint16_t>(shown ? 1 + (i + 1) % 12 : 1 + i);
            }
            st->ib->Unlock();
            c.rs(D3DRS_CULLMODE, D3DCULL_CW);
            c.clear(Dx::kColor(15, 15, 40));
            c.fvf(kPFvf);
            c.chk(c.d->SetStreamSource(0, st->vb.get(), 0, sizeof(P)), "SetStreamSource");
            c.chk(c.d->SetIndices(st->ib.get()), "SetIndices");
            c.chk(c.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 13, 0, 12), "DrawIndexedPrimitive");
        });
    });
}

const Registrar kPrims(primitive_cases);
const Registrar kDyn(dynamic_cases);

}  // namespace

}  // namespace lsw::suites::render::d3d9
