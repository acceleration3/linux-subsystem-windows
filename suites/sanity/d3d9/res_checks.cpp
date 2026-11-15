#include "sanity/d3d9/res_checks.hpp"

#include "sanity/d3d9/kit.hpp"
#include "support/dxgi_budget.hpp"
#include "support/pattern.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::d3d9 {

namespace {

struct Pools {
    const char* name;
    D3DPOOL pool;
};

constexpr Pools kPools[] = {{"DEFAULT", D3DPOOL_DEFAULT}, {"MANAGED", D3DPOOL_MANAGED}, {"SYSTEMMEM", D3DPOOL_SYSTEMMEM}, {"SCRATCH", D3DPOOL_SCRATCH}};

std::string usage_text(DWORD usage) {
    std::string out;
    const struct {
        DWORD bit;
        const char* name;
    } names[] = {{D3DUSAGE_RENDERTARGET, "RENDERTARGET"}, {D3DUSAGE_DEPTHSTENCIL, "DEPTHSTENCIL"}, {D3DUSAGE_DYNAMIC, "DYNAMIC"},
                 {D3DUSAGE_WRITEONLY, "WRITEONLY"}, {D3DUSAGE_AUTOGENMIPMAP, "AUTOGENMIPMAP"}};
    for (const auto& n : names) {
        if ((usage & n.bit) != 0) {
            out += std::string(out.empty() ? "" : "|") + n.name;
        }
    }
    return out.empty() ? "0" : out;
}

class Matrix {
public:
    Matrix(Problems& problems, bool ex) : problems_(problems), ex_(ex) {}

    void run(const std::string& name, bool expected, const std::function<HRESULT(std::string&)>& make) {
        std::string detail;
        const HRESULT result = make(detail);
        const std::string label = std::string("D3D") + Kit::label(ex_) + " " + name;
        ++count;
        if (expected) {
            if (FAILED(result)) {
                problems_.add(label + " is rejected with " + Kit::hr(result) + " but is valid");
            } else if (!detail.empty()) {
                problems_.add(label + ": " + detail);
            }
        } else if (SUCCEEDED(result)) {
            problems_.add(label + " is accepted but is invalid");
        } else if (result != D3DERR_INVALIDCALL) {
            problems_.add(label + " is rejected with " + Kit::hr(result) + " instead of D3DERR_INVALIDCALL");
        }
    }

    int count = 0;

private:
    Problems& problems_;
    bool ex_;
};

void match_desc(const D3DSURFACE_DESC& desc, D3DRESOURCETYPE type, D3DPOOL pool, D3DFORMAT format, UINT width, UINT height,
                DWORD usage, std::string& detail) {
    if (desc.Type != type || desc.Pool != pool || desc.Format != format || desc.Width != width || desc.Height != height ||
        (desc.Usage & usage) != usage) {
        detail = "GetDesc reports type " + std::to_string(desc.Type) + " pool " + std::to_string(desc.Pool) + " format " + std::to_string(desc.Format) +
                 " " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " usage " + usage_text(desc.Usage);
    }
}

void run_matrix(D3d9Rig& rig, Matrix& matrix) {
    IDirect3DDevice9* device = rig.dev();
    IDirect3D9* d3d = rig.d3d.get();
    const bool ex = rig.ex;
    auto supported = [&](DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format) {
        return d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, type, format);
    };
    const bool rt_texture = supported(D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8) == D3D_OK;
    const bool rt_cube = supported(D3DUSAGE_RENDERTARGET, D3DRTYPE_CUBETEXTURE, D3DFMT_A8R8G8B8) == D3D_OK;
    const bool ds_texture = supported(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24S8) == D3D_OK;
    const HRESULT autogen = supported(D3DUSAGE_AUTOGENMIPMAP, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    const bool cube_cap = (caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0;
    const bool volume_cap = (caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0;
    for (const Pools& p : kPools) {
        const bool managed = p.pool == D3DPOOL_MANAGED;
        const bool legal_managed = !managed || !ex;
        auto texture_case = [&](DWORD usage, bool valid) {
            matrix.run(std::string("2D texture pool ") + p.name + " usage " + usage_text(usage), valid && legal_managed, [&](std::string& detail) {
                const D3DFORMAT format = (usage & D3DUSAGE_DEPTHSTENCIL) != 0 ? D3DFMT_D24S8 : D3DFMT_A8R8G8B8;
                Com<IDirect3DTexture9> t;
                const HRESULT r = device->CreateTexture(64, 64, 1, usage, format, p.pool, t.put(), nullptr);
                if (SUCCEEDED(r)) {
                    D3DSURFACE_DESC desc{};
                    t->GetLevelDesc(0, &desc);
                    match_desc(desc, D3DRTYPE_SURFACE, p.pool, format, 64, 64, usage & ~static_cast<DWORD>(D3DUSAGE_AUTOGENMIPMAP), detail);
                    if (t->GetType() != D3DRTYPE_TEXTURE) {
                        detail = "GetType is not D3DRTYPE_TEXTURE";
                    }
                }
                return r;
            });
        };
        texture_case(0, true);
        if (p.pool == D3DPOOL_DEFAULT || p.pool == D3DPOOL_MANAGED) {
            texture_case(D3DUSAGE_DYNAMIC, p.pool == D3DPOOL_DEFAULT);
        }
        if (rt_texture) {
            texture_case(D3DUSAGE_RENDERTARGET, p.pool == D3DPOOL_DEFAULT);
            texture_case(D3DUSAGE_RENDERTARGET | D3DUSAGE_DYNAMIC, false);
        }
        if (ds_texture) {
            texture_case(D3DUSAGE_DEPTHSTENCIL, p.pool == D3DPOOL_DEFAULT);
        }
        if (SUCCEEDED(autogen) && (p.pool == D3DPOOL_DEFAULT || p.pool == D3DPOOL_MANAGED)) {
            texture_case(D3DUSAGE_AUTOGENMIPMAP, true);
        }
        if (cube_cap) {
            auto cube_case = [&](DWORD usage, bool valid) {
                matrix.run(std::string("cube texture pool ") + p.name + " usage " + usage_text(usage), valid && legal_managed, [&](std::string& detail) {
                    Com<IDirect3DCubeTexture9> t;
                    const HRESULT r = device->CreateCubeTexture(64, 1, usage, D3DFMT_A8R8G8B8, p.pool, t.put(), nullptr);
                    if (SUCCEEDED(r)) {
                        D3DSURFACE_DESC desc{};
                        t->GetLevelDesc(0, &desc);
                        match_desc(desc, D3DRTYPE_SURFACE, p.pool, D3DFMT_A8R8G8B8, 64, 64, usage, detail);
                    }
                    return r;
                });
            };
            cube_case(0, true);
            if (rt_cube) {
                cube_case(D3DUSAGE_RENDERTARGET, p.pool == D3DPOOL_DEFAULT);
            }
        }
        if (volume_cap) {
            auto volume_case = [&](DWORD usage, D3DFORMAT format, bool valid) {
                matrix.run(std::string("volume texture pool ") + p.name + " usage " + usage_text(usage), valid && legal_managed, [&](std::string& detail) {
                    Com<IDirect3DVolumeTexture9> t;
                    const HRESULT r = device->CreateVolumeTexture(16, 16, 4, 1, usage, format, p.pool, t.put(), nullptr);
                    if (SUCCEEDED(r)) {
                        D3DVOLUME_DESC desc{};
                        t->GetLevelDesc(0, &desc);
                        if (desc.Pool != p.pool || desc.Format != format || desc.Width != 16 || desc.Height != 16 || desc.Depth != 4) {
                            detail = "GetLevelDesc reports " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + "x" + std::to_string(desc.Depth);
                        }
                    }
                    return r;
                });
            };
            volume_case(0, D3DFMT_A8R8G8B8, true);
            volume_case(D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, false);
            volume_case(D3DUSAGE_DEPTHSTENCIL, D3DFMT_D24S8, false);
            if (p.pool == D3DPOOL_DEFAULT) {
                volume_case(D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, true);
            }
        }
        auto buffer_case = [&](DWORD usage, bool valid) {
            const bool allowed = valid && legal_managed && p.pool != D3DPOOL_SCRATCH;
            matrix.run(std::string("vertex buffer pool ") + p.name + " usage " + usage_text(usage), allowed, [&](std::string& detail) {
                Com<IDirect3DVertexBuffer9> vb;
                const HRESULT r = device->CreateVertexBuffer(256, usage, 0, p.pool, vb.put(), nullptr);
                if (SUCCEEDED(r)) {
                    D3DVERTEXBUFFER_DESC desc{};
                    vb->GetDesc(&desc);
                    if (desc.Size != 256 || desc.Pool != p.pool || desc.Type != D3DRTYPE_VERTEXBUFFER || (desc.Usage & usage) != usage) {
                        detail = "GetDesc reports size " + std::to_string(desc.Size) + " pool " + std::to_string(desc.Pool) + " usage " + usage_text(desc.Usage);
                    }
                }
                return r;
            });
            matrix.run(std::string("index buffer pool ") + p.name + " usage " + usage_text(usage), allowed, [&](std::string& detail) {
                Com<IDirect3DIndexBuffer9> ib;
                const HRESULT r = device->CreateIndexBuffer(256, usage, D3DFMT_INDEX16, p.pool, ib.put(), nullptr);
                if (SUCCEEDED(r)) {
                    D3DINDEXBUFFER_DESC desc{};
                    ib->GetDesc(&desc);
                    if (desc.Size != 256 || desc.Pool != p.pool || desc.Format != D3DFMT_INDEX16 || (desc.Usage & usage) != usage) {
                        detail = "GetDesc reports size " + std::to_string(desc.Size) + " pool " + std::to_string(desc.Pool);
                    }
                }
                return r;
            });
        };
        buffer_case(0, true);
        buffer_case(D3DUSAGE_WRITEONLY, true);
        buffer_case(D3DUSAGE_DYNAMIC, p.pool != D3DPOOL_MANAGED);
        buffer_case(D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, p.pool != D3DPOOL_MANAGED);
        matrix.run(std::string("vertex buffer pool ") + p.name + " usage RENDERTARGET", false, [&](std::string&) {
            Com<IDirect3DVertexBuffer9> vb;
            return device->CreateVertexBuffer(256, D3DUSAGE_RENDERTARGET, 0, p.pool, vb.put(), nullptr);
        });
        matrix.run(std::string("offscreen plain surface pool ") + p.name, p.pool != D3DPOOL_MANAGED, [&](std::string& detail) {
            Com<IDirect3DSurface9> s;
            const HRESULT r = device->CreateOffscreenPlainSurface(64, 32, D3DFMT_A8R8G8B8, p.pool, s.put(), nullptr);
            if (SUCCEEDED(r)) {
                D3DSURFACE_DESC desc{};
                s->GetDesc(&desc);
                match_desc(desc, D3DRTYPE_SURFACE, p.pool, D3DFMT_A8R8G8B8, 64, 32, 0, detail);
            }
            return r;
        });
    }
    matrix.run("render target surface", true, [&](std::string& detail) {
        Com<IDirect3DSurface9> s;
        const HRESULT r = device->CreateRenderTarget(64, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, s.put(), nullptr);
        if (SUCCEEDED(r)) {
            D3DSURFACE_DESC desc{};
            s->GetDesc(&desc);
            match_desc(desc, D3DRTYPE_SURFACE, D3DPOOL_DEFAULT, D3DFMT_A8R8G8B8, 64, 32, D3DUSAGE_RENDERTARGET, detail);
        }
        return r;
    });
    matrix.run("depth stencil surface", Kit::has_depth(rig, D3DFMT_D24S8), [&](std::string& detail) {
        Com<IDirect3DSurface9> s;
        const HRESULT r = device->CreateDepthStencilSurface(64, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, s.put(), nullptr);
        if (SUCCEEDED(r)) {
            D3DSURFACE_DESC desc{};
            s->GetDesc(&desc);
            match_desc(desc, D3DRTYPE_SURFACE, D3DPOOL_DEFAULT, D3DFMT_D24S8, 64, 32, D3DUSAGE_DEPTHSTENCIL, detail);
        }
        return r;
    });
}

UINT chain_length(UINT width, UINT height) {
    UINT length = 1;
    UINT largest = std::max(width, height);
    while (largest > 1) {
        largest >>= 1;
        ++length;
    }
    return length;
}

bool lock_level(IDirect3DTexture9* texture, UINT index, D3DLOCKED_RECT& locked) {
    return SUCCEEDED(texture->LockRect(index, &locked, nullptr, 0));
}

std::uint32_t uniform_color(int index) {
    return Kit::frame_color(index + 20);
}

}  // namespace

Outcome ResChecks::pool_usage_matrix(Context& context) {
    Problems problems;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        if (!Kit::open(rig, ex, error)) {
            if (ex) {
                context.note("Direct3D 9Ex is unavailable: " + error);
                continue;
            }
            return Outcome::fail(error);
        }
        Matrix matrix(problems, ex);
        run_matrix(rig, matrix);
        context.measure(std::string("cases.") + Kit::label(ex), matrix.count);
        Kit::check_usable(problems, std::string("D3D") + Kit::label(ex) + " after the matrix", rig.dev());
    }
    return problems.outcome();
}

Outcome ResChecks::mip_levels_and_sizes(Context& context) {
    Problems problems;
    D3d9Rig rig;
    std::string error;
    if (!Kit::open(rig, false, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    struct Size {
        UINT w;
        UINT h;
    };
    const Size sizes[] = {{64, 64}, {100, 37}, {1, 1}, {7, 300}, {513, 2}, {256, 1}, {33, 33}};
    for (const D3DPOOL pool : {D3DPOOL_SCRATCH, D3DPOOL_SYSTEMMEM, D3DPOOL_MANAGED}) {
        for (const Size& size : sizes) {
            const std::string name = "pool " + std::to_string(pool) + " " + std::to_string(size.w) + "x" + std::to_string(size.h);
            D3DCAPS9 caps{};
            device->GetDeviceCaps(&caps);
            const bool restricted = (caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0 && (caps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) == 0;
            const bool odd = (size.w & (size.w - 1)) != 0 || (size.h & (size.h - 1)) != 0;
            if (pool != D3DPOOL_SCRATCH && restricted && odd) {
                continue;
            }
            HRESULT result = S_OK;
            Com<IDirect3DTexture9> t = D3d9Tools::texture(device, size.w, size.h, 0, 0, D3DFMT_A8R8G8B8, pool, &result);
            if (!t) {
                problems.add(name + ": creation failed with " + Kit::hr(result));
                continue;
            }
            const UINT expected = chain_length(size.w, size.h);
            if (t->GetLevelCount() != expected && pool != D3DPOOL_MANAGED) {
                problems.add(name + ": " + std::to_string(t->GetLevelCount()) + " levels, expected " + std::to_string(expected));
            }
            if (t->GetLevelCount() > expected) {
                problems.add(name + ": more levels than the chain down to 1x1");
            }
            const UINT levels = std::min<UINT>(t->GetLevelCount(), expected);
            if (pool == D3DPOOL_MANAGED && levels != expected) {
                problems.add(name + ": " + std::to_string(t->GetLevelCount()) + " levels, expected " + std::to_string(expected));
            }
            UINT w = size.w;
            UINT h = size.h;
            for (UINT i = 0; i < levels; ++i) {
                D3DSURFACE_DESC desc{};
                t->GetLevelDesc(i, &desc);
                if (desc.Width != w || desc.Height != h) {
                    problems.add(name + " level " + std::to_string(i) + " is " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + ", expected " + std::to_string(w) + "x" + std::to_string(h));
                }
                D3DLOCKED_RECT locked{};
                if (!lock_level(t.get(), i, locked)) {
                    problems.add(name + " level " + std::to_string(i) + " cannot be locked");
                } else {
                    if (locked.Pitch < static_cast<INT>(desc.Width * 4) || locked.Pitch % 4 != 0) {
                        problems.add(name + " level " + std::to_string(i) + " has pitch " + std::to_string(locked.Pitch) + " for width " + std::to_string(desc.Width));
                    }
                    t->UnlockRect(i);
                }
                w = std::max(1u, w >> 1);
                h = std::max(1u, h >> 1);
            }
            for (UINT i = 0; i < levels; ++i) {
                if (!D3d9Tools::write_texture_level(t.get(), i, 500 + i, error)) {
                    problems.add(name + ": " + error);
                }
            }
            for (UINT i = 0; i < levels; ++i) {
                D3DSURFACE_DESC desc{};
                t->GetLevelDesc(i, &desc);
                D3DLOCKED_RECT locked{};
                if (!lock_level(t.get(), i, locked)) {
                    continue;
                }
                const std::vector<std::uint32_t> expected_pixels = D3d9Tools::pattern(500 + i, desc.Width, desc.Height);
                bool same = true;
                for (UINT y = 0; y < desc.Height && same; ++y) {
                    same = std::memcmp(static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch),
                                       expected_pixels.data() + static_cast<std::size_t>(y) * desc.Width, static_cast<std::size_t>(desc.Width) * 4) == 0;
                }
                t->UnlockRect(i);
                if (!same) {
                    problems.add(name + " level " + std::to_string(i) + " lost the data written to it");
                }
            }
        }
    }
    context.measure("sizes", std::size(sizes));
    return problems.outcome();
}

Outcome ResChecks::volume_and_cube(Context& context) {
    Problems problems;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        if (!Kit::open(rig, ex, error)) {
            if (ex) {
                continue;
            }
            return Outcome::fail(error);
        }
        IDirect3DDevice9* device = rig.dev();
        D3DCAPS9 caps{};
        device->GetDeviceCaps(&caps);
        struct Variant {
            const char* name;
            D3DPOOL pool;
            DWORD usage;
        };
        std::vector<Variant> variants = {{"SYSTEMMEM", D3DPOOL_SYSTEMMEM, 0}, {"DEFAULT+DYNAMIC", D3DPOOL_DEFAULT, D3DUSAGE_DYNAMIC}};
        if (!ex) {
            variants.push_back({"MANAGED", D3DPOOL_MANAGED, 0});
        }
        for (const Variant& v : variants) {
            const std::string name = std::string("D3D") + Kit::label(ex) + " " + v.name;
            if ((caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0) {
                Com<IDirect3DVolumeTexture9> volume;
                const HRESULT created = device->CreateVolumeTexture(16, 16, 8, 1, v.usage, D3DFMT_A8R8G8B8, v.pool, volume.put(), nullptr);
                if (FAILED(created)) {
                    problems.add(name + " volume creation failed with " + Kit::hr(created));
                } else {
                    D3DLOCKED_BOX box{};
                    if (FAILED(volume->LockBox(0, &box, nullptr, 0))) {
                        problems.add(name + " volume LockBox failed");
                    } else {
                        if (box.RowPitch < 64 || box.SlicePitch < box.RowPitch * 16) {
                            problems.add(name + " volume pitches " + std::to_string(box.RowPitch) + "/" + std::to_string(box.SlicePitch) + " are too small");
                        }
                        for (int z = 7; z >= 0; --z) {
                            const std::vector<std::uint32_t> slice = D3d9Tools::pattern(900 + static_cast<unsigned>(z), 16, 16);
                            for (int y = 0; y < 16; ++y) {
                                std::memcpy(static_cast<std::uint8_t*>(box.pBits) + static_cast<std::size_t>(z) * static_cast<std::size_t>(box.SlicePitch) + static_cast<std::size_t>(y) * static_cast<std::size_t>(box.RowPitch),
                                            slice.data() + static_cast<std::size_t>(y) * 16, 64);
                            }
                        }
                        volume->UnlockBox(0);
                        if (SUCCEEDED(volume->LockBox(0, &box, nullptr, D3DLOCK_READONLY))) {
                            for (int z = 0; z < 8; ++z) {
                                const std::vector<std::uint32_t> slice = D3d9Tools::pattern(900 + static_cast<unsigned>(z), 16, 16);
                                for (int y = 0; y < 16; ++y) {
                                    if (std::memcmp(static_cast<const std::uint8_t*>(box.pBits) + static_cast<std::size_t>(z) * static_cast<std::size_t>(box.SlicePitch) + static_cast<std::size_t>(y) * static_cast<std::size_t>(box.RowPitch),
                                                    slice.data() + static_cast<std::size_t>(y) * 16, 64) != 0) {
                                        problems.add(name + " volume slice " + std::to_string(z) + " row " + std::to_string(y) + " differs from what was written");
                                        break;
                                    }
                                }
                            }
                            volume->UnlockBox(0);
                        }
                        D3DLOCKED_BOX partial{};
                        const D3DBOX region = {4, 2, 12, 10, 3, 6};
                        if (SUCCEEDED(volume->LockBox(0, &partial, &region, D3DLOCK_READONLY))) {
                            const std::vector<std::uint32_t> slice = D3d9Tools::pattern(900 + 3, 16, 16);
                            std::uint32_t first = 0;
                            std::memcpy(&first, partial.pBits, 4);
                            if (first != slice[2 * 16 + 4]) {
                                problems.add(name + " a partial box lock does not start at the requested corner");
                            }
                            volume->UnlockBox(0);
                        }
                    }
                }
            }
            if ((caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) != 0) {
                Com<IDirect3DCubeTexture9> cube;
                const HRESULT created = device->CreateCubeTexture(32, 2, v.usage, D3DFMT_A8R8G8B8, v.pool, cube.put(), nullptr);
                if (FAILED(created)) {
                    problems.add(name + " cube creation failed with " + Kit::hr(created));
                    continue;
                }
                for (int pass = 0; pass < 2; ++pass) {
                    for (UINT level = 0; level < 2; ++level) {
                        const UINT size = 32u >> level;
                        for (int face = 0; face < 6; ++face) {
                            const int f = pass == 0 ? face : 5 - face;
                            const std::vector<std::uint32_t> pixels = D3d9Tools::pattern(1200 + static_cast<unsigned>(f * 10) + level, size, size);
                            D3DLOCKED_RECT locked{};
                            if (FAILED(cube->LockRect(static_cast<D3DCUBEMAP_FACES>(f), level, &locked, nullptr, 0))) {
                                problems.add(name + " cube face " + std::to_string(f) + " level " + std::to_string(level) + " cannot be locked");
                                continue;
                            }
                            for (UINT y = 0; y < size; ++y) {
                                std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), pixels.data() + static_cast<std::size_t>(y) * size, size * 4);
                            }
                            cube->UnlockRect(static_cast<D3DCUBEMAP_FACES>(f), level);
                        }
                    }
                }
                for (UINT level = 0; level < 2; ++level) {
                    const UINT size = 32u >> level;
                    for (int face = 0; face < 6; ++face) {
                        const std::vector<std::uint32_t> pixels = D3d9Tools::pattern(1200 + static_cast<unsigned>(face * 10) + level, size, size);
                        D3DLOCKED_RECT locked{};
                        if (FAILED(cube->LockRect(static_cast<D3DCUBEMAP_FACES>(face), level, &locked, nullptr, D3DLOCK_READONLY))) {
                            continue;
                        }
                        for (UINT y = 0; y < size; ++y) {
                            if (std::memcmp(static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), pixels.data() + static_cast<std::size_t>(y) * size, size * 4) != 0) {
                                problems.add(name + " cube face " + std::to_string(face) + " level " + std::to_string(level) + " row " + std::to_string(y) + " differs from what was written");
                                break;
                            }
                        }
                        cube->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), level);
                    }
                }
            }
        }
        Kit::check_usable(problems, std::string("D3D") + Kit::label(ex), device);
    }
    context.measure("variants", 3);
    return problems.outcome();
}

Outcome ResChecks::managed_roundtrip(Context& context) {
    D3d9Rig rig;
    std::string error;
    if (!Kit::open(rig, false, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    HRESULT result = S_OK;
    Com<IDirect3DTexture9> managed = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result);
    if (!managed) {
        return Outcome::fail("a MANAGED texture cannot be created: " + Kit::hr(result));
    }
    if (!D3d9Tools::write_texture_level(managed.get(), 0, 77, error)) {
        return Outcome::fail(error);
    }
    const SceneImage expected = D3d9Tools::image_of(64, 64, D3d9Tools::pattern(77, 64, 64));
    auto verify = [&](const std::string& stage) {
        SceneImage image;
        std::string where;
        if (!D3d9Tools::sample(device, managed.get(), 64, 64, image, error)) {
            problems.add(stage + ": " + error);
        } else if (!D3d9Tools::same(expected, image, 0, where)) {
            problems.add(stage + ": the managed texture no longer shows what was written: " + where);
        }
    };
    verify("after the first draw");
    VideoMemory video;
    std::string budget_error;
    std::uint64_t pressure = 256ull << 20;
    if (DxgiBudget::query(rig.adapter.luid, true, video, budget_error) && video.budget > 0) {
        pressure = std::min<std::uint64_t>(pressure, video.budget / 8);
    }
    std::vector<Com<IDirect3DSurface9>> held;
    std::uint64_t total = 0;
    while (total < pressure) {
        Com<IDirect3DTexture9> t = D3d9Tools::texture(device, 1024, 1024, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        if (!t) {
            break;
        }
        Com<IDirect3DSurface9> s = D3d9Tools::level(t.get(), 0);
        device->ColorFill(s.get(), nullptr, Kit::frame_color(static_cast<int>(held.size())));
        held.push_back(std::move(s));
        total += 4ull << 20;
    }
    context.measure("pressure_mib", total >> 20);
    if (!D3d9Tools::wait_event(device, error)) {
        return Outcome::fail(error);
    }
    verify("after the pressure loop");
    held.clear();
    D3DPRESENT_PARAMETERS pp = rig.parameters(rig.settings);
    result = device->Reset(&pp);
    if (FAILED(result)) {
        problems.add("Reset failed with " + Kit::hr(result));
    } else {
        verify("after Reset");
    }
    Kit::check_usable(problems, "after Reset", device);
    return problems.outcome();
}

Outcome ResChecks::priority_and_lod(Context& context) {
    D3d9Rig rig;
    std::string error;
    if (!Kit::open(rig, false, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    Com<IDirect3DTexture9> managed = D3d9Tools::texture(device, 32, 32, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    if (!managed) {
        return Outcome::fail("a MANAGED mip-mapped texture cannot be created");
    }
    const UINT levels = managed->GetLevelCount();
    if (levels != 6) {
        problems.add("a 32x32 texture with Levels 0 has " + std::to_string(levels) + " levels");
    }
    for (UINT i = 0; i < levels; ++i) {
        D3DLOCKED_RECT locked{};
        D3DSURFACE_DESC desc{};
        managed->GetLevelDesc(i, &desc);
        if (managed->LockRect(i, &locked, nullptr, 0) == D3D_OK) {
            for (UINT y = 0; y < desc.Height; ++y) {
                for (UINT x = 0; x < desc.Width; ++x) {
                    *reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch) + x * 4) = uniform_color(static_cast<int>(i));
                }
            }
            managed->UnlockRect(i);
        }
    }
    const DWORD initial_priority = managed->GetPriority();
    if (initial_priority != 0) {
        problems.add("the initial priority is " + std::to_string(initial_priority) + " instead of 0");
    }
    DWORD previous = managed->SetPriority(7);
    if (previous != 0) {
        problems.add("SetPriority(7) returned " + std::to_string(previous) + " instead of the previous 0");
    }
    previous = managed->SetPriority(3);
    if (previous != 7 || managed->GetPriority() != 3) {
        problems.add("SetPriority/GetPriority do not round-trip (previous " + std::to_string(previous) + ", now " + std::to_string(managed->GetPriority()) + ")");
    }
    previous = managed->SetLOD(3);
    if (previous != 0 || managed->GetLOD() != 3) {
        problems.add("SetLOD(3) returned " + std::to_string(previous) + " and GetLOD reads " + std::to_string(managed->GetLOD()));
    }
    managed->SetLOD(100);
    if (managed->GetLOD() != levels - 1) {
        problems.add("SetLOD(100) is not clamped to the level count: GetLOD reads " + std::to_string(managed->GetLOD()));
    }
    for (const DWORD lod : {2u, 4u, 0u}) {
        managed->SetLOD(lod);
        if (managed->GetLOD() != lod) {
            problems.add("GetLOD reads " + std::to_string(managed->GetLOD()) + " after SetLOD(" + std::to_string(lod) + ")");
        }
        SceneImage image;
        if (!D3d9Tools::sample(device, managed.get(), 32, 32, image, error)) {
            problems.add(error);
            continue;
        }
        std::string where;
        if (!D3d9Tools::uniform(image, uniform_color(static_cast<int>(lod)), 0, where)) {
            problems.add("with LOD " + std::to_string(lod) + " a 32x32 draw does not sample mip level " + std::to_string(lod) + ": " + where);
        }
    }
    Com<IDirect3DVertexBuffer9> vb;
    device->CreateVertexBuffer(64, 0, 0, D3DPOOL_MANAGED, vb.put(), nullptr);
    if (vb) {
        vb->SetPriority(5);
        if (vb->GetPriority() != 5) {
            problems.add("a managed vertex buffer does not keep its priority");
        }
    }
    Com<IDirect3DTexture9> unmanaged = D3d9Tools::texture(device, 16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
    if (unmanaged && (unmanaged->SetLOD(1) != 0 || unmanaged->GetLOD() != 0 || unmanaged->SetPriority(4) != 0 || unmanaged->GetPriority() != 0)) {
        problems.add("a DEFAULT-pool texture reports a non-zero LOD or priority");
    }
    context.measure("levels", levels);
    return problems.outcome();
}

Outcome ResChecks::autogenmipmap(Context& context) {
    D3d9Rig rig;
    std::string error;
    if (!Kit::open(rig, false, error)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    const HRESULT support = rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_AUTOGENMIPMAP, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8);
    Problems problems;
    if (support == D3DERR_NOTAVAILABLE) {
        HRESULT result = S_OK;
        Com<IDirect3DTexture9> t = D3d9Tools::texture(device, 64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result);
        Kit::expect_failure(problems, "creating an AUTOGENMIPMAP texture after CheckDeviceFormat reports it unavailable", result);
        return problems.outcome();
    }
    if (support == D3DOK_NOAUTOGEN) {
        return Context::skip("the driver creates AUTOGENMIPMAP textures but reports D3DOK_NOAUTOGEN: levels are not generated");
    }
    if (support != D3D_OK) {
        return Outcome::fail("CheckDeviceFormat(AUTOGENMIPMAP) returned " + Kit::hr(support));
    }
    auto box = [](const std::vector<std::uint32_t>& pixels, UINT size, UINT x, UINT y, int channel) {
        int sum = 0;
        for (UINT dy = 0; dy < 2; ++dy) {
            for (UINT dx = 0; dx < 2; ++dx) {
                sum += static_cast<int>((pixels[(y * 2 + dy) * size + x * 2 + dx] >> (channel * 8)) & 255u);
            }
        }
        return (sum + 2) / 4;
    };
    auto compare_level = [&](IDirect3DBaseTexture9* texture, const std::vector<std::uint32_t>& top, UINT size, const std::string& stage) {
        SceneImage image;
        if (!D3d9Tools::sample(device, texture, size / 2, size / 2, image, error)) {
            problems.add(stage + ": " + error);
            return;
        }
        for (UINT y = 0; y < size / 2; ++y) {
            for (UINT x = 0; x < size / 2; ++x) {
                for (int c = 0; c < 4; ++c) {
                    const int got = static_cast<int>((D3d9Tools::pixel(image, x, y) >> (c * 8)) & 255u);
                    if (std::abs(got - box(top, size, x, y, c)) > 2) {
                        problems.add(stage + ": level 1 pixel (" + std::to_string(x) + "," + std::to_string(y) + ") channel " + std::to_string(c) + " is " + std::to_string(got) + ", the box filter gives " + std::to_string(box(top, size, x, y, c)));
                        return;
                    }
                }
            }
        }
    };
    HRESULT result = S_OK;
    Com<IDirect3DTexture9> written = D3d9Tools::texture(device, 64, 64, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result);
    if (!written) {
        return Outcome::fail("an AUTOGENMIPMAP texture cannot be created: " + Kit::hr(result));
    }
    if (!D3d9Tools::write_texture_level(written.get(), 0, 31, error)) {
        return Outcome::fail(error);
    }
    compare_level(written.get(), D3d9Tools::pattern(31, 64, 64), 64, "after writing level 0");
    if (!D3d9Tools::write_texture_level(written.get(), 0, 32, error)) {
        return Outcome::fail(error);
    }
    written->GenerateMipSubLevels();
    compare_level(written.get(), D3d9Tools::pattern(32, 64, 64), 64, "after a second write and GenerateMipSubLevels");
    Kit::expect_success(problems, "SetAutoGenFilterType(POINT)", written->SetAutoGenFilterType(D3DTEXF_POINT));
    if (written->GetAutoGenFilterType() != D3DTEXF_POINT) {
        problems.add("GetAutoGenFilterType does not return the type set");
    }
    written->SetAutoGenFilterType(D3DTEXF_LINEAR);
    if (written->GetAutoGenFilterType() != D3DTEXF_LINEAR) {
        problems.add("GetAutoGenFilterType does not return LINEAR after setting it");
    }
    Com<IDirect3DTexture9> rendered = D3d9Tools::texture(device, 64, 64, 0, D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &result);
    if (!rendered) {
        context.note("a render-target AUTOGENMIPMAP texture is not available: " + Kit::hr(result));
    } else {
        Com<IDirect3DSurface9> top = D3d9Tools::level(rendered.get(), 0);
        const RECT left = {0, 0, 32, 64};
        const RECT right = {32, 0, 64, 64};
        device->ColorFill(top.get(), &left, 0xFFFF0000u);
        device->ColorFill(top.get(), &right, 0xFF0000FFu);
        rendered->GenerateMipSubLevels();
        SceneImage image;
        if (!D3d9Tools::sample(device, rendered.get(), 32, 32, image, error)) {
            problems.add("rendered texture: " + error);
        } else {
            std::string where;
            if (!D3d9Tools::region(image, 0, 0, 15, 32, 0xFFFF0000u, 2, where) || !D3d9Tools::region(image, 17, 0, 32, 32, 0xFF0000FFu, 2, where)) {
                problems.add("level 1 of a rendered texture is not the box filter of the halves: " + where);
            }
        }
    }
    return problems.outcome();
}

Outcome ResChecks::exhaustion(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    VideoMemory video;
    if (!DxgiBudget::query(rig.adapter.luid, true, video, error)) {
        return Outcome::fail(error);
    }
    const std::uint64_t chunk = 64ull << 20;
    const std::uint64_t cap = std::min<std::uint64_t>(std::max<std::uint64_t>(video.budget + video.budget / 2, 2ull << 30), sizeof(void*) == 4 ? (1ull << 30) : (32ull << 30));
    Problems problems;
    std::vector<Com<IDirect3DSurface9>> held;
    HRESULT stop = S_OK;
    std::uint64_t total = 0;
    while (total < cap) {
        HRESULT result = S_OK;
        Com<IDirect3DSurface9> s = D3d9Tools::target(device, 4096, 4096, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, &result);
        if (!s) {
            stop = result;
            break;
        }
        device->ColorFill(s.get(), nullptr, 0xFF102030u);
        held.push_back(std::move(s));
        total += chunk;
    }
    context.measure("allocated_mib", total >> 20);
    context.measure("budget_mib", video.budget >> 20);
    context.measure("stop", Kit::hr(stop));
    if (stop == S_OK) {
        context.note("no failure was reached before the cap of " + std::to_string(cap >> 20) + " MiB");
    } else if (stop != D3DERR_OUTOFVIDEOMEMORY && stop != E_OUTOFMEMORY) {
        problems.add("creation failed with " + Kit::hr(stop) + " instead of D3DERR_OUTOFVIDEOMEMORY or E_OUTOFMEMORY");
    }
    Kit::check_usable(problems, "after the failure", device);
    const std::size_t count = held.size();
    held.clear();
    static_cast<void>(D3d9Tools::wait_event(device, error));
    ::Sleep(300);
    std::size_t again = 0;
    std::vector<Com<IDirect3DSurface9>> second;
    for (; again < count; ++again) {
        Com<IDirect3DSurface9> s = D3d9Tools::target(device, 4096, 4096);
        if (!s) {
            break;
        }
        second.push_back(std::move(s));
    }
    context.measure("allocated_again", static_cast<long long>(again));
    if (again < count) {
        problems.add("only " + std::to_string(again) + " of the " + std::to_string(count) + " allocations succeed again after releasing them");
    }
    second.clear();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
