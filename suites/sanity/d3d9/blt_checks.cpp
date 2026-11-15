#include "sanity/d3d9/blt_checks.hpp"

#include "sanity/d3d9/kit.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace lsw::suites::sanity::d3d9 {

namespace {

bool draw_into(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DBaseTexture9* texture, UINT width, UINT height) {
    Com<IDirect3DSurface9> original;
    device->GetRenderTarget(0, original.put());
    device->SetRenderTarget(0, target);
    D3d9Tools::prepare(device, true);
    device->SetTexture(0, texture);
    const bool drawn = D3d9Tools::draw_textured(device, 0, 0, static_cast<float>(width), static_cast<float>(height), 0, 0, 1, 1);
    device->SetTexture(0, nullptr);
    device->SetRenderTarget(0, original.get());
    return drawn;
}

struct StretchCase {
    const char* name;
    RECT source;
    RECT destination;
    UINT width;
    UINT height;
    D3DTEXTUREFILTERTYPE filter;
    D3DFORMAT format;
};

}  // namespace

Outcome BltChecks::stretchrect(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    D3d9Twin twin;
    if (!twin.open(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const std::vector<std::uint32_t> texels = D3d9Tools::pattern(222, 64, 64);
    Com<IDirect3DTexture9> managed = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    if (!managed || !D3d9Tools::write_texture_level(managed.get(), 0, 222, error)) {
        return Outcome::fail("the source texture cannot be prepared: " + error);
    }
    Com<IDirect3DSurface9> source = D3d9Tools::target(device, 64, 64);
    if (!source || !draw_into(device, source.get(), managed.get(), 64, 64)) {
        return Outcome::fail("the source render target cannot be drawn");
    }
    const StretchCase cases[] = {
        {"point 3x enlargement", {0, 0, 64, 64}, {0, 0, 192, 192}, 192, 192, D3DTEXF_POINT, D3DFMT_A8R8G8B8},
        {"linear 2x enlargement", {0, 0, 64, 64}, {0, 0, 128, 128}, 128, 128, D3DTEXF_LINEAR, D3DFMT_A8R8G8B8},
        {"linear 2:1 reduction", {0, 0, 64, 64}, {0, 0, 32, 32}, 32, 32, D3DTEXF_LINEAR, D3DFMT_A8R8G8B8},
        {"point sub-rectangle 3x", {8, 8, 40, 24}, {12, 20, 108, 68}, 128, 96, D3DTEXF_POINT, D3DFMT_A8R8G8B8},
        {"unfiltered 1:1 sub-rectangle", {4, 4, 36, 36}, {10, 10, 42, 42}, 64, 64, D3DTEXF_NONE, D3DFMT_A8R8G8B8},
        {"conversion to X8R8G8B8", {0, 0, 64, 64}, {0, 0, 64, 64}, 64, 64, D3DTEXF_NONE, D3DFMT_X8R8G8B8},
        {"conversion to R5G6B5", {0, 0, 64, 64}, {0, 0, 64, 64}, 64, 64, D3DTEXF_NONE, D3DFMT_R5G6B5},
    };
    for (const StretchCase& c : cases) {
        const bool conversion = c.format != D3DFMT_A8R8G8B8;
        if (conversion && (FAILED(rig.d3d->CheckDeviceFormatConversion(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, c.format)) ||
                           FAILED(rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, c.format)))) {
            context.note(std::string(c.name) + ": not supported by this device");
            continue;
        }
        Com<IDirect3DSurface9> destination = D3d9Tools::target(device, c.width, c.height, c.format);
        if (!destination) {
            problems.add(std::string(c.name) + ": the destination cannot be created");
            continue;
        }
        device->ColorFill(destination.get(), nullptr, 0xFF202020u);
        const HRESULT result = device->StretchRect(source.get(), &c.source, destination.get(), &c.destination, c.filter);
        if (FAILED(result)) {
            problems.add(std::string(c.name) + ": StretchRect failed with " + Kit::hr(result));
            continue;
        }
        SceneImage image;
        if (!D3d9Tools::read_surface(device, destination.get(), image, error)) {
            problems.add(std::string(c.name) + ": " + error);
            continue;
        }
        const TwinScene scene = Kit::textured_quad(texels, 64, 64, c.width, c.height, static_cast<float>(c.destination.left), static_cast<float>(c.destination.top),
                                                    static_cast<float>(c.destination.right), static_cast<float>(c.destination.bottom),
                                                    static_cast<float>(c.source.left) / 64.0f, static_cast<float>(c.source.top) / 64.0f,
                                                    static_cast<float>(c.source.right) / 64.0f, static_cast<float>(c.source.bottom) / 64.0f,
                                                    c.filter == D3DTEXF_LINEAR, conversion, 32.0f / 255.0f);
        twin.compare(context, problems, std::string("stretchrect ") + c.name, image, scene,
                     c.format == D3DFMT_R5G6B5 ? Tolerance::bytes(9) : Tolerance::bytes(c.filter == D3DTEXF_LINEAR ? 3 : 0, 0.005));
    }
    Com<IDirect3DSurface9> other = D3d9Tools::target(device, 64, 64);
    const RECT inverted = {40, 0, 10, 10};
    const RECT outside = {0, 0, 65, 64};
    const RECT half = {0, 0, 32, 32};
    Kit::expect_failure(problems, "StretchRect with an inverted source rect", device->StretchRect(source.get(), &inverted, other.get(), nullptr, D3DTEXF_NONE));
    Kit::expect_failure(problems, "StretchRect with an inverted destination rect", device->StretchRect(source.get(), nullptr, other.get(), &inverted, D3DTEXF_NONE));
    Kit::expect_failure(problems, "StretchRect with a source rect outside the surface", device->StretchRect(source.get(), &outside, other.get(), nullptr, D3DTEXF_NONE));
    Kit::expect_failure(problems, "StretchRect with the ANISOTROPIC filter", device->StretchRect(source.get(), nullptr, other.get(), nullptr, D3DTEXF_ANISOTROPIC));
    Kit::expect_failure(problems, "StretchRect scaling a surface onto itself", device->StretchRect(source.get(), nullptr, source.get(), &half, D3DTEXF_POINT));
    Com<IDirect3DSurface9> sys;
    device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, sys.put(), nullptr);
    if (sys) {
        Kit::expect_failure(problems, "StretchRect from a system-memory surface", device->StretchRect(sys.get(), nullptr, other.get(), nullptr, D3DTEXF_NONE));
        Kit::expect_failure(problems, "StretchRect to a system-memory surface", device->StretchRect(source.get(), nullptr, sys.get(), nullptr, D3DTEXF_NONE));
    }
    Kit::check_usable(problems, "after the failing blits", device);
    return problems.outcome();
}

Outcome BltChecks::updatesurface_texture(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    std::vector<std::uint32_t> expected = D3d9Tools::pattern(10, 64, 64);
    Com<IDirect3DTexture9> sys = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM);
    Com<IDirect3DTexture9> gpu = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
    if (!sys || !gpu || !D3d9Tools::write_texture_level(sys.get(), 0, 10, error)) {
        return Outcome::fail("the textures cannot be prepared: " + error);
    }
    Kit::expect_success(problems, "UpdateTexture", device->UpdateTexture(sys.get(), gpu.get()));
    auto verify = [&](const std::string& stage) {
        SceneImage image;
        std::string where;
        if (!D3d9Tools::sample(device, gpu.get(), 64, 64, image, error)) {
            problems.add(stage + ": " + error);
        } else if (!D3d9Tools::same(D3d9Tools::image_of(64, 64, expected), image, 0, where)) {
            problems.add(stage + ": " + where);
        }
    };
    verify("UpdateTexture of the whole texture");
    Com<IDirect3DSurface9> piece;
    device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, piece.put(), nullptr);
    const std::vector<std::uint32_t> second = D3d9Tools::pattern(11, 64, 64);
    if (!piece || !D3d9Tools::write_surface(piece.get(), nullptr, second, 64, error)) {
        return Outcome::fail("the source surface cannot be prepared: " + error);
    }
    const RECT source_rect = {8, 8, 40, 32};
    const POINT destination = {16, 4};
    Com<IDirect3DSurface9> level0 = D3d9Tools::level(gpu.get(), 0);
    Kit::expect_success(problems, "UpdateSurface with a sub-rect and a destination point", device->UpdateSurface(piece.get(), &source_rect, level0.get(), &destination));
    for (LONG y = 0; y < 24; ++y) {
        for (LONG x = 0; x < 32; ++x) {
            expected[static_cast<std::size_t>(destination.y + y) * 64 + static_cast<std::size_t>(destination.x + x)] = second[static_cast<std::size_t>(source_rect.top + y) * 64 + static_cast<std::size_t>(source_rect.left + x)];
        }
    }
    verify("UpdateSurface of a sub-rect");
    Com<IDirect3DTexture9> small_gpu = D3d9Tools::texture(device, 32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
    Com<IDirect3DSurface9> small_sys;
    device->CreateOffscreenPlainSurface(32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, small_sys.put(), nullptr);
    const std::vector<std::uint32_t> third = D3d9Tools::pattern(12, 32, 32);
    if (small_gpu && small_sys && D3d9Tools::write_surface(small_sys.get(), nullptr, third, 32, error)) {
        Com<IDirect3DSurface9> small_level = D3d9Tools::level(small_gpu.get(), 0);
        Kit::expect_success(problems, "UpdateSurface of a whole surface", device->UpdateSurface(small_sys.get(), nullptr, small_level.get(), nullptr));
        SceneImage image;
        std::string where;
        if (!D3d9Tools::sample(device, small_gpu.get(), 32, 32, image, error) || !D3d9Tools::same(D3d9Tools::image_of(32, 32, third), image, 0, where)) {
            problems.add("UpdateSurface of a whole surface: " + where + error);
        }
    }
    const RECT dirty = {20, 24, 52, 40};
    {
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(sys->LockRect(0, &locked, &dirty, 0))) {
            const std::vector<std::uint32_t> patch = D3d9Tools::pattern(13, 64, 64);
            for (LONG y = dirty.top; y < dirty.bottom; ++y) {
                std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y - dirty.top) * static_cast<std::size_t>(locked.Pitch), patch.data() + static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(dirty.left), static_cast<std::size_t>(dirty.right - dirty.left) * 4);
                for (LONG x = dirty.left; x < dirty.right; ++x) {
                    expected[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)] = patch[static_cast<std::size_t>(y) * 64 + static_cast<std::size_t>(x)];
                }
            }
            sys->UnlockRect(0);
        }
    }
    Kit::expect_success(problems, "UpdateTexture of a dirty sub-region", device->UpdateTexture(sys.get(), gpu.get()));
    verify("UpdateTexture of a dirty sub-region");
    Com<IDirect3DTexture9> chain_sys = D3d9Tools::texture(device, 64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM);
    Com<IDirect3DTexture9> chain_gpu = D3d9Tools::texture(device, 64, 64, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
    if (chain_sys && chain_gpu) {
        const UINT levels = chain_sys->GetLevelCount();
        for (UINT l = 0; l < levels; ++l) {
            if (!D3d9Tools::write_texture_level(chain_sys.get(), l, 300 + l, error)) {
                problems.add(error);
            }
        }
        Kit::expect_success(problems, "UpdateTexture of a full mip chain", device->UpdateTexture(chain_sys.get(), chain_gpu.get()));
        for (UINT l = 0; l < levels; ++l) {
            const UINT size = 64u >> l;
            SceneImage image;
            std::string where;
            if (!D3d9Tools::sample(device, chain_gpu.get(), size, size, image, error)) {
                problems.add("mip level " + std::to_string(l) + ": " + error);
            } else if (!D3d9Tools::same(D3d9Tools::image_of(size, size, D3d9Tools::pattern(300 + l, size, size)), image, 0, where)) {
                problems.add("mip level " + std::to_string(l) + " after UpdateTexture: " + where);
            }
        }
        context.measure("levels", levels);
    }
    Kit::check_usable(problems, "after the updates", device);
    return problems.outcome();
}

Outcome BltChecks::colorfill_and_readback(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    struct Format {
        D3DFORMAT format;
        const char* name;
        int tolerance;
        bool alpha;
    };
    const Format formats[] = {{D3DFMT_A8R8G8B8, "A8R8G8B8", 0, true}, {D3DFMT_X8R8G8B8, "X8R8G8B8", 0, false}, {D3DFMT_R5G6B5, "R5G6B5", 8, false},
                              {D3DFMT_A1R5G5B5, "A1R5G5B5", 8, true}, {D3DFMT_X1R5G5B5, "X1R5G5B5", 8, false}, {D3DFMT_A4R4G4B4, "A4R4G4B4", 17, true},
                              {D3DFMT_A2R10G10B10, "A2R10G10B10", 1, true}};
    const std::uint32_t colours[] = {0xFF336699u, 0xFFFF00FFu, 0x0000FF00u, 0xFF000000u, 0xFFFFFFFFu};
    int tested = 0;
    for (const Format& f : formats) {
        if (FAILED(rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, f.format))) {
            context.note(std::string(f.name) + " is not a render target format on this device");
            continue;
        }
        HRESULT created = S_OK;
        Com<IDirect3DSurface9> surface = D3d9Tools::target(device, 16, 16, f.format, D3DMULTISAMPLE_NONE, false, &created);
        if (!surface) {
            problems.add(std::string(f.name) + ": the render target cannot be created: " + Kit::hr(created));
            continue;
        }
        ++tested;
        for (const std::uint32_t colour : colours) {
            const HRESULT filled = device->ColorFill(surface.get(), nullptr, colour);
            if (FAILED(filled)) {
                problems.add(std::string(f.name) + ": ColorFill failed with " + Kit::hr(filled));
                continue;
            }
            SceneImage image;
            if (!D3d9Tools::read_surface(device, surface.get(), image, error)) {
                problems.add(std::string(f.name) + ": " + error);
                continue;
            }
            std::string where;
            std::uint32_t expected = f.alpha ? ((colour >> 24) >= 128 ? (colour | 0xFF000000u) : (colour & 0x00FFFFFFu)) : (colour | 0xFF000000u);
            if (!D3d9Tools::uniform(image, expected, f.tolerance, where, !f.alpha)) {
                problems.add(std::string(f.name) + " ColorFill " + D3d9Tools::color_text(colour) + ": " + where);
            }
        }
        device->ColorFill(surface.get(), nullptr, 0xFF000000u);
        const RECT inner = {4, 4, 12, 12};
        device->ColorFill(surface.get(), &inner, 0xFFFFFFFFu);
        SceneImage image;
        std::string where;
        if (D3d9Tools::read_surface(device, surface.get(), image, error)) {
            if (!D3d9Tools::region(image, 4, 4, 12, 12, 0xFFFFFFFFu, f.tolerance, where, true) || !D3d9Tools::region(image, 0, 0, 16, 4, 0xFF000000u, f.tolerance, where, true)) {
                problems.add(std::string(f.name) + " partial ColorFill: " + where);
            }
        }
    }
    for (const D3DFORMAT format : {D3DFMT_A8R8G8B8, D3DFMT_X8R8G8B8}) {
        Com<IDirect3DSurface9> plain;
        Com<IDirect3DSurface9> copy = D3d9Tools::target(device, 16, 16, format);
        if (SUCCEEDED(device->CreateOffscreenPlainSurface(16, 16, format, D3DPOOL_DEFAULT, plain.put(), nullptr)) && copy) {
            const HRESULT filled = device->ColorFill(plain.get(), nullptr, 0xFF11AA55u);
            const HRESULT copied = device->StretchRect(plain.get(), nullptr, copy.get(), nullptr, D3DTEXF_NONE);
            SceneImage image;
            std::string where;
            if (FAILED(filled) || FAILED(copied)) {
                problems.add("an offscreen plain surface cannot be filled and copied: " + Kit::hr(FAILED(filled) ? filled : copied));
            } else if (!D3d9Tools::read_surface(device, copy.get(), image, error) || !D3d9Tools::uniform(image, 0xFF11AA55u, 0, where)) {
                problems.add("ColorFill on an offscreen plain surface read back wrong: " + where + error);
            }
        }
    }
    Com<IDirect3DSurface9> target = D3d9Tools::target(device, 64, 64);
    Com<IDirect3DSurface9> original;
    device->GetRenderTarget(0, original.put());
    device->SetRenderTarget(0, target.get());
    int wrong = 0;
    for (int k = 0; k < 24; ++k) {
        D3d9Tools::prepare(device, false);
        D3d9Tools::clear(device, Kit::frame_color(k));
        if (!D3d9Tools::draw_quad(device, 16, 16, 48, 48, Kit::frame_color(k + 100))) {
            problems.add("a draw failed in the immediate readback loop");
            break;
        }
        SceneImage image;
        if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
            problems.add(error);
            break;
        }
        if (D3d9Tools::pixel(image, 2, 2) != Kit::frame_color(k) || D3d9Tools::pixel(image, 32, 32) != Kit::frame_color(k + 100) || D3d9Tools::pixel(image, 16, 16) != Kit::frame_color(k + 100) || D3d9Tools::pixel(image, 15, 16) != Kit::frame_color(k)) {
            ++wrong;
        }
    }
    device->SetRenderTarget(0, original.get());
    if (wrong != 0) {
        problems.add(std::to_string(wrong) + " of 24 immediate readbacks do not show the render drawn just before");
    }
    context.measure("formats_tested", tested);
    Kit::check_usable(problems, "after the fills", device);
    return problems.outcome();
}

Outcome BltChecks::frontbuffer(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    settings.width = 160;
    settings.height = 120;
    settings.x = 80;
    settings.y = 80;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    D3DDISPLAYMODE mode{};
    if (FAILED(rig.d3d->GetAdapterDisplayMode(rig.ordinal, &mode))) {
        return Outcome::fail("GetAdapterDisplayMode failed");
    }
    const RECT monitor = rig.monitor_rect();
    const D3DCOLOR colour = 0xFFC8641Eu;
    const int want[3] = {0xC8, 0x64, 0x1E};
    D3d9Tools::clear(device, colour);
    Kit::expect_success(problems, "Present", device->Present(nullptr, nullptr, nullptr, nullptr));
    int got[3] = {0, 0, 0};
    if (!D3d9Tools::wait_screen_color(rig.window.hwnd(), 80, 60, want, 2, 3000, got)) {
        context.note("the screen under the window shows (" + std::to_string(got[0]) + "," + std::to_string(got[1]) + "," + std::to_string(got[2]) + ") instead of the presented colour");
    }
    Com<IDirect3DSurface9> screen;
    HRESULT created = device->CreateOffscreenPlainSurface(mode.Width, mode.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, screen.put(), nullptr);
    if (FAILED(created)) {
        return Outcome::fail("the screen-size destination cannot be created: " + Kit::hr(created));
    }
    const HRESULT grabbed = device->GetFrontBufferData(0, screen.get());
    if (FAILED(grabbed)) {
        problems.add("GetFrontBufferData failed with " + Kit::hr(grabbed));
    } else {
        SceneImage image;
        if (D3d9Tools::read_surface(device, screen.get(), image, error)) {
            const POINT origin = rig.client_origin();
            int mismatches = 0;
            std::string first;
            for (const POINT probe : {POINT{20, 20}, POINT{80, 60}, POINT{140, 100}, POINT{10, 110}}) {
                const LONG x = origin.x - monitor.left + probe.x;
                const LONG y = origin.y - monitor.top + probe.y;
                if (x < 0 || y < 0 || x >= static_cast<LONG>(image.width) || y >= static_cast<LONG>(image.height)) {
                    continue;
                }
                const std::uint32_t value = D3d9Tools::pixel(image, static_cast<UINT>(x), static_cast<UINT>(y));
                std::string where;
                SceneImage one;
                one.width = 1;
                one.height = 1;
                one.data.assign(reinterpret_cast<const std::uint8_t*>(&value), reinterpret_cast<const std::uint8_t*>(&value) + 4);
                if (!D3d9Tools::uniform(one, colour, 2, where, true)) {
                    if (mismatches++ == 0) {
                        first = "client (" + std::to_string(probe.x) + "," + std::to_string(probe.y) + ") reads " + D3d9Tools::color_text(value);
                    }
                }
            }
            if (mismatches != 0) {
                problems.add("GetFrontBufferData shows the presented colour under only part of the topmost window: " + first);
            }
            context.measure("screen", std::to_string(image.width) + "x" + std::to_string(image.height));
        } else {
            problems.add(error);
        }
    }
    Com<IDirect3DSurface9> small;
    Com<IDirect3DSurface9> wrong_format;
    Com<IDirect3DSurface9> wrong_pool;
    device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, small.put(), nullptr);
    device->CreateOffscreenPlainSurface(mode.Width, mode.Height, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, wrong_format.put(), nullptr);
    device->CreateOffscreenPlainSurface(mode.Width, mode.Height, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, wrong_pool.put(), nullptr);
    if (small) {
        Kit::expect_failure(problems, "GetFrontBufferData into a 64x64 surface", device->GetFrontBufferData(0, small.get()));
    }
    if (wrong_format) {
        Kit::expect_failure(problems, "GetFrontBufferData into an X8R8G8B8 surface", device->GetFrontBufferData(0, wrong_format.get()));
    }
    if (wrong_pool) {
        Kit::expect_failure(problems, "GetFrontBufferData into a DEFAULT-pool surface", device->GetFrontBufferData(0, wrong_pool.get()));
    }
    Kit::check_usable(problems, "after the front buffer reads", device);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
