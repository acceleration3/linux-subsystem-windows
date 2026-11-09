#pragma once

#include "render/common/pattern.hpp"
#include "render/d3d9/d3d9.hpp"
#include "support/d3d9_tools.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites::render::d3d9 {

struct Xf {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    [[nodiscard]] static Xf translate(float x, float y, float z);
    [[nodiscard]] static Xf scale(float x, float y, float z);
    [[nodiscard]] static Xf rot_x(float radians);
    [[nodiscard]] static Xf rot_y(float radians);
    [[nodiscard]] static Xf rot_z(float radians);
    [[nodiscard]] static Xf perspective(float fovy, float aspect, float near_plane, float far_plane);
    [[nodiscard]] static Xf ortho(float width, float height, float near_plane, float far_plane);
    [[nodiscard]] static Xf look_at(const float eye[3], const float at[3], const float up[3]);
    [[nodiscard]] Xf operator*(const Xf& rhs) const;
    [[nodiscard]] const D3DMATRIX* ptr() const { return reinterpret_cast<const D3DMATRIX*>(this); }
};

struct Vtx {
    float x, y, z;
    float nx, ny, nz;
    DWORD diffuse;
    DWORD specular;
    float u, v;
};
constexpr DWORD kVtxFvf = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX1;

struct TlVtx {
    float x, y, z, rhw;
    DWORD color;
    float u, v;
};
constexpr DWORD kTlFvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;

struct Mesh {
    std::vector<Vtx> vertices;
    std::vector<std::uint16_t> indices;
    [[nodiscard]] UINT triangles() const { return static_cast<UINT>(indices.size() / 3); }
};

class Geo {
public:
    [[nodiscard]] static Mesh sphere(unsigned slices, unsigned stacks, float radius, DWORD color);
    [[nodiscard]] static Mesh grid(unsigned cells, float extent, float height, DWORD color);
    [[nodiscard]] static Mesh cube(float half, DWORD color);
    [[nodiscard]] static std::vector<Vtx> quad(float x0, float y0, float x1, float y1, float z, DWORD color);
    [[nodiscard]] static std::vector<TlVtx> tl_quad(float x0, float y0, float x1, float y1, DWORD color, float u0 = 0, float v0 = 0,
                                                    float u1 = 1, float v1 = 1, float z = 0.5f);
};

using Fill = std::function<std::uint32_t(UINT x, UINT y, UINT w, UINT h)>;

class Texels {
public:
    [[nodiscard]] static std::uint32_t argb(unsigned a, unsigned r, unsigned g, unsigned b) { return (a << 24) | (r << 16) | (g << 8) | b; }
    [[nodiscard]] static Fill checker(unsigned cells, std::uint32_t a, std::uint32_t b);
    [[nodiscard]] static Fill rainbow();
    [[nodiscard]] static Fill alpha_rings();
    [[nodiscard]] static Fill solid(std::uint32_t value);
    [[nodiscard]] static std::uint16_t half(float value);
    [[nodiscard]] static UINT bytes_per_texel(D3DFORMAT format);
    [[nodiscard]] static bool is_block(D3DFORMAT format);
    [[nodiscard]] static bool pack(D3DFORMAT format, std::uint32_t argb, std::uint8_t* dst);
    static void encode_block(D3DFORMAT format, const std::uint32_t texels[16], std::uint8_t* dst);
};

class Ctx {
public:
    explicit Ctx(Kit& k) : kit(k), d(k.device()) {}

    Kit& kit;
    IDirect3DDevice9* d;
    Surface target;
    Surface depth;
    UINT size = 128;
    std::string error;
    bool good = true;
    bool in_scene = false;
    rr::CaptureSet pending;

    [[nodiscard]] bool open(UINT extent, D3DFORMAT format, D3DFORMAT depth_format);
    void fail(const std::string& message);
    void chk(HRESULT result, const char* what);
    void rs(D3DRENDERSTATETYPE type, DWORD value) { chk(d->SetRenderState(type, value), "SetRenderState"); }
    void rsf(D3DRENDERSTATETYPE type, float value);
    void tss(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) { chk(d->SetTextureStageState(stage, type, value), "SetTextureStageState"); }
    void ss(DWORD stage, D3DSAMPLERSTATETYPE type, DWORD value) { chk(d->SetSamplerState(stage, type, value), "SetSamplerState"); }
    void xf(D3DTRANSFORMSTATETYPE type, const Xf& value) { chk(d->SetTransform(type, value.ptr()), "SetTransform"); }
    void tex(DWORD stage, IDirect3DBaseTexture9* texture) { chk(d->SetTexture(stage, texture), "SetTexture"); }
    void begin();
    void end();
    void clear(D3DCOLOR color, DWORD flags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, float z = 1.0f, DWORD stencil = 0);
    void snap(const std::string& label);
    void snap_surface(Surface& surface, const std::string& label);
    void base_state();
    void camera(float distance, float fovy_degrees = 60.0f);
    void fvf(DWORD value) { chk(d->SetFVF(value), "SetFVF"); }
    void draw(D3DPRIMITIVETYPE type, UINT primitives, const void* vertices, UINT stride);
    void draw_mesh(const Mesh& mesh);
    void quad(float x0, float y0, float x1, float y1, float z, DWORD tl, DWORD tr, DWORD bl, DWORD br);
    void quad(float x0, float y0, float x1, float y1, float z, DWORD color) { quad(x0, y0, x1, y1, z, color, color, color, color); }
    void draw_idx(D3DPRIMITIVETYPE type, UINT vertex_count, UINT primitives, const void* indices, D3DFORMAT index_format, const void* vertices,
                  UINT stride);
    void light(DWORD index, const D3DLIGHT9& value, bool enable = true);
    void material(const D3DMATERIAL9& value) { chk(d->SetMaterial(&value), "SetMaterial"); }
    [[nodiscard]] Com<IDirect3DTexture9> texture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                 const Fill& fill);
    [[nodiscard]] bool fill_level(IDirect3DTexture9* texture, UINT level, D3DFORMAT format, const Fill& fill);
    [[nodiscard]] Com<IDirect3DVertexShader9> vs(const std::string& source, const char* profile = "vs_3_0");
    [[nodiscard]] Com<IDirect3DPixelShader9> ps(const std::string& source, const char* profile = "ps_3_0");
    [[nodiscard]] Com<IDirect3DVertexBuffer9> vbuffer(const void* data, UINT bytes, DWORD usage = D3DUSAGE_WRITEONLY, D3DPOOL pool = D3DPOOL_DEFAULT);
    [[nodiscard]] Com<IDirect3DIndexBuffer9> ibuffer(const void* data, UINT bytes, D3DFORMAT format);
    [[nodiscard]] Com<IDirect3DVertexDeclaration9> decl(const D3DVERTEXELEMENT9* elements);
    [[nodiscard]] bool format_ok(DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format) const;
    [[nodiscard]] D3DCAPS9 caps() const;
};

struct Layout {
    D3DFORMAT format = D3DFMT_A8R8G8B8;
    D3DFORMAT depth = D3DFMT_D24S8;
    UINT extent = 128;
};

using Need = std::function<std::string(IDirect3DDevice9*)>;
using Body = std::function<void(Ctx&)>;
using FrameBody = std::function<void(Ctx&, unsigned)>;
using FrameMaker = std::function<FrameBody()>;

class Cases {
public:
    static void single(rr::CaseSpec spec, Body body, Layout layout = {}, Need need = nullptr);
    static void frames(rr::CaseSpec spec, FrameMaker maker, Layout layout = {}, Need need = nullptr);
};

class Needs {
public:
    [[nodiscard]] static Need caps(std::function<bool(const D3DCAPS9&)> test, std::string what);
    [[nodiscard]] static Need format(DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format, std::string what);
    [[nodiscard]] static Need all(std::vector<Need> parts);
    [[nodiscard]] static Need shader(unsigned vertex_major, unsigned pixel_major);
};

struct Registrar {
    explicit Registrar(void (*fn)()) { fn(); }
};

struct Shaders {
    static constexpr const char* kPassVs =
        "struct I { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
        "struct O { float4 p : POSITION; float4 c : COLOR0; float2 t : TEXCOORD0; };\n"
        "O main(I i) { O o; o.p = i.p; o.c = i.c; o.t = i.t; return o; }\n";
};

class Dx {
public:
    static constexpr DWORD kColor(unsigned r, unsigned g, unsigned b, unsigned a = 255) { return (a << 24) | (r << 16) | (g << 8) | b; }
    [[nodiscard]] static DWORD fbits(float v) {
        DWORD out = 0;
        std::memcpy(&out, &v, 4);
        return out;
    }
    [[nodiscard]] static D3DLIGHT9 directional(float dx, float dy, float dz, float r, float g, float b);
    [[nodiscard]] static D3DLIGHT9 point(float x, float y, float z, float range, float a0, float a1, float a2, float r, float g, float b);
    [[nodiscard]] static D3DLIGHT9 spot(float x, float y, float z, float dx, float dy, float dz, float range, float falloff, float theta,
                                        float phi, float r, float g, float b);
    [[nodiscard]] static D3DMATERIAL9 material(float dr, float dg, float db, float ar, float ag, float ab, float er = 0, float eg = 0,
                                               float eb = 0, float sr = 0, float sg = 0, float sb = 0, float power = 0);
};
}  // namespace lsw::suites::render::d3d9
