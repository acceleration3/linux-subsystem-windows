#include "sanity/d3d9/lock_checks.hpp"

#include "sanity/d3d9/kit.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::d3d9 {

namespace {

bool draw_buffer(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DVertexBuffer9* buffer, UINT first_vertex) {
    device->SetRenderTarget(0, target);
    D3d9Tools::prepare(device, false);
    device->SetFVF(D3d9Tools::kTlFvf);
    device->SetStreamSource(0, buffer, 0, sizeof(TlVertex));
    if (FAILED(device->BeginScene())) {
        return false;
    }
    const HRESULT result = device->DrawPrimitive(D3DPT_TRIANGLESTRIP, first_vertex, 2);
    device->EndScene();
    return SUCCEEDED(result);
}

bool draw_texture_to(IDirect3DDevice9* device, IDirect3DSurface9* target, IDirect3DBaseTexture9* texture, UINT size) {
    device->SetRenderTarget(0, target);
    D3d9Tools::prepare(device, true);
    D3d9Tools::sampler(device, 0, false);
    device->SetTexture(0, texture);
    const bool drawn = D3d9Tools::draw_textured(device, 0, 0, static_cast<float>(size), static_cast<float>(size), 0, 0, 1, 1);
    device->SetTexture(0, nullptr);
    return drawn;
}

void fill_rect(IDirect3DTexture9* texture, const RECT& rect, const std::vector<std::uint32_t>& full, UINT stride, DWORD flags) {
    D3DLOCKED_RECT locked{};
    if (FAILED(texture->LockRect(0, &locked, &rect, flags))) {
        return;
    }
    for (LONG y = 0; y < rect.bottom - rect.top; ++y) {
        std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch),
                    full.data() + static_cast<std::size_t>(rect.top + y) * stride + static_cast<std::size_t>(rect.left), static_cast<std::size_t>(rect.right - rect.left) * 4);
    }
    texture->UnlockRect(0);
}

}  // namespace

Outcome LockChecks::discard_fresh(Context& context) {
    Problems problems;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        D3d9Settings settings;
        settings.depth = false;
        if (!Kit::open(rig, ex, error, settings)) {
            if (ex) {
                continue;
            }
            return Outcome::fail(error);
        }
        IDirect3DDevice9* device = rig.dev();
        const std::string name = std::string("D3D") + Kit::label(ex);
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        Com<IDirect3DSurface9> first = D3d9Tools::target(device, 64, 64);
        Com<IDirect3DSurface9> second = D3d9Tools::target(device, 64, 64);
        Com<IDirect3DVertexBuffer9> vb;
        const UINT bytes = 4 * sizeof(TlVertex);
        if (FAILED(device->CreateVertexBuffer(bytes, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3d9Tools::kTlFvf, D3DPOOL_DEFAULT, vb.put(), nullptr))) {
            return Outcome::fail("a dynamic vertex buffer cannot be created");
        }
        void* memory = nullptr;
        TlVertex quad[4];
        D3d9Tools::tl_quad(quad, 8, 8, 56, 56, 0xFFFF0000u);
        if (FAILED(vb->Lock(0, 0, &memory, D3DLOCK_DISCARD))) {
            return Outcome::fail("Lock(DISCARD) on a dynamic vertex buffer failed");
        }
        std::memcpy(memory, quad, bytes);
        vb->Unlock();
        device->SetRenderTarget(0, first.get());
        D3d9Tools::clear(device, 0xFF000000u);
        if (!draw_buffer(device, first.get(), vb.get(), 0)) {
            problems.add(name + ": the first buffer draw failed");
        }
        D3d9Tools::tl_quad(quad, 8, 8, 56, 56, 0xFF0000FFu);
        if (FAILED(vb->Lock(0, 0, &memory, D3DLOCK_DISCARD))) {
            problems.add(name + ": the second Lock(DISCARD) failed");
        } else {
            std::memset(memory, 0xCD, bytes);
            std::memcpy(memory, quad, bytes);
            vb->Unlock();
        }
        device->SetRenderTarget(0, second.get());
        D3d9Tools::clear(device, 0xFF000000u);
        if (!draw_buffer(device, second.get(), vb.get(), 0)) {
            problems.add(name + ": the second buffer draw failed");
        }
        SceneImage a;
        SceneImage b;
        std::string where;
        if (!D3d9Tools::read_surface(device, first.get(), a, error) || !D3d9Tools::read_surface(device, second.get(), b, error)) {
            return Outcome::fail(error);
        }
        if (!D3d9Tools::region(a, 8, 8, 56, 56, 0xFFFF0000u, 0, where) || !D3d9Tools::region(a, 0, 0, 64, 8, 0xFF000000u, 0, where)) {
            problems.add(name + ": the draw issued before the DISCARD lock does not show the old vertex data: " + where);
        }
        if (!D3d9Tools::region(b, 8, 8, 56, 56, 0xFF0000FFu, 0, where)) {
            problems.add(name + ": the draw after the DISCARD lock does not show the new vertex data: " + where);
        }
        Com<IDirect3DTexture9> dynamic = D3d9Tools::texture(device, 64, 64, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        if (!dynamic) {
            problems.add(name + ": a dynamic texture cannot be created");
        } else {
            for (const std::uint32_t colour : {0xFF00FF00u, 0xFFFFFF00u}) {
                D3DLOCKED_RECT locked{};
                if (FAILED(dynamic->LockRect(0, &locked, nullptr, D3DLOCK_DISCARD))) {
                    problems.add(name + ": LockRect(DISCARD) on a dynamic texture failed");
                    break;
                }
                for (UINT y = 0; y < 64; ++y) {
                    auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch));
                    std::fill(row, row + 64, colour);
                }
                dynamic->UnlockRect(0);
                Com<IDirect3DSurface9>& target = colour == 0xFF00FF00u ? first : second;
                if (!draw_texture_to(device, target.get(), dynamic.get(), 64)) {
                    problems.add(name + ": drawing the dynamic texture failed");
                }
            }
            if (D3d9Tools::read_surface(device, first.get(), a, error) && D3d9Tools::read_surface(device, second.get(), b, error)) {
                if (!D3d9Tools::uniform(a, 0xFF00FF00u, 0, where) || !D3d9Tools::uniform(b, 0xFFFFFF00u, 0, where)) {
                    problems.add(name + ": a draw of the dynamic texture before the DISCARD relock shows the later contents: " + where);
                }
            }
        }
        device->SetRenderTarget(0, original.get());
        Kit::check_usable(problems, name, device);
    }
    return problems.outcome();
}

Outcome LockChecks::nooverwrite_inflight(Context& context) {
    Problems problems;
    constexpr int kSlots = 8;
    constexpr int kFrames = 200;
    for (const bool ex : {false, true}) {
        D3d9Rig rig;
        std::string error;
        D3d9Settings settings;
        settings.depth = false;
        if (!Kit::open(rig, ex, error, settings)) {
            if (ex) {
                continue;
            }
            return Outcome::fail(error);
        }
        IDirect3DDevice9* device = rig.dev();
        const std::string name = std::string("D3D") + Kit::label(ex);
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        Com<IDirect3DSurface9> target = D3d9Tools::target(device, 128, 128);
        device->SetRenderTarget(0, target.get());
        D3d9Tools::clear(device, 0xFF000000u);
        const UINT slot_bytes = 4 * sizeof(TlVertex);
        Com<IDirect3DVertexBuffer9> vb;
        if (FAILED(device->CreateVertexBuffer(slot_bytes * kSlots, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3d9Tools::kTlFvf, D3DPOOL_DEFAULT, vb.put(), nullptr))) {
            return Outcome::fail("a dynamic vertex buffer cannot be created");
        }
        D3d9Tools::prepare(device, false);
        device->SetFVF(D3d9Tools::kTlFvf);
        device->SetStreamSource(0, vb.get(), 0, sizeof(TlVertex));
        int refused = 0;
        for (int frame = 0; frame < kFrames; ++frame) {
            const int slot = frame % kSlots;
            const int tile = frame % 64;
            TlVertex quad[4];
            const float left = static_cast<float>((tile % 8) * 16);
            const float top = static_cast<float>((tile / 8) * 16);
            D3d9Tools::tl_quad(quad, left, top, left + 16.0f, top + 16.0f, Kit::frame_color(frame));
            void* memory = nullptr;
            const HRESULT locked = slot == 0 ? vb->Lock(0, slot_bytes * kSlots, &memory, D3DLOCK_DISCARD) : vb->Lock(slot_bytes * static_cast<UINT>(slot), slot_bytes, &memory, D3DLOCK_NOOVERWRITE);
            if (FAILED(locked)) {
                problems.add(name + ": frame " + std::to_string(frame) + " lock failed with " + Kit::hr(locked));
                break;
            }
            std::memcpy(memory, quad, slot_bytes);
            vb->Unlock();
            device->BeginScene();
            if (FAILED(device->DrawPrimitive(D3DPT_TRIANGLESTRIP, static_cast<UINT>(slot) * 4, 2))) {
                ++refused;
            }
            device->EndScene();
        }
        if (refused != 0) {
            problems.add(name + ": " + std::to_string(refused) + " draws were refused");
        }
        SceneImage image;
        if (!D3d9Tools::read_surface(device, target.get(), image, error)) {
            return Outcome::fail(error);
        }
        int wrong = 0;
        std::string first_wrong;
        for (int tile = 0; tile < 64; ++tile) {
            const int last = tile + 64 * ((kFrames - 1 - tile) / 64);
            std::string where;
            const UINT x = static_cast<UINT>(tile % 8) * 16;
            const UINT y = static_cast<UINT>(tile / 8) * 16;
            if (!D3d9Tools::region(image, x, y, x + 16, y + 16, Kit::frame_color(last), 0, where)) {
                if (wrong++ == 0) {
                    first_wrong = "tile " + std::to_string(tile) + " (last written by frame " + std::to_string(last) + "): " + where;
                }
            }
        }
        if (wrong != 0) {
            problems.add(name + ": " + std::to_string(wrong) + " of 64 tiles show data other than what their draw consumed; first " + first_wrong);
        }
        device->SetRenderTarget(0, original.get());
        Kit::check_usable(problems, name, device);
    }
    static_cast<void>(context);
    return problems.outcome();
}

Outcome LockChecks::readonly_and_partial(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    Com<IDirect3DSurface9> sys;
    if (FAILED(device->CreateOffscreenPlainSurface(64, 64, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, sys.put(), nullptr))) {
        return Outcome::fail("a system-memory surface cannot be created");
    }
    const std::vector<std::uint32_t> base = D3d9Tools::pattern(40, 64, 64);
    if (!D3d9Tools::write_surface(sys.get(), nullptr, base, 64, error)) {
        return Outcome::fail(error);
    }
    SceneImage image;
    std::string where;
    if (!D3d9Tools::read_surface(device, sys.get(), image, error) || !D3d9Tools::same(D3d9Tools::image_of(64, 64, base), image, 0, where)) {
        problems.add("a READONLY lock does not return what was written: " + where + error);
    }
    const RECT part = {8, 4, 40, 20};
    D3DLOCKED_RECT locked{};
    if (SUCCEEDED(sys->LockRect(&locked, &part, 0))) {
        for (LONG y = 0; y < part.bottom - part.top; ++y) {
            auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch));
            std::fill(row, row + (part.right - part.left), 0xFF00FF00u);
        }
        sys->UnlockRect();
        SceneImage after;
        if (D3d9Tools::read_surface(device, sys.get(), after, error)) {
            for (UINT y = 0; y < 64; ++y) {
                for (UINT x = 0; x < 64; ++x) {
                    const bool inside = x >= 8 && x < 40 && y >= 4 && y < 20;
                    const std::uint32_t expected = inside ? 0xFF00FF00u : base[y * 64 + x];
                    if (D3d9Tools::pixel(after, x, y) != expected) {
                        problems.add("a partial rect lock changed pixel (" + std::to_string(x) + "," + std::to_string(y) + ") " + (inside ? "to the wrong value" : "outside the rect"));
                        y = 64;
                        break;
                    }
                }
            }
        }
    } else {
        problems.add("a partial LockRect failed");
    }
    const RECT bad_rects[] = {{-1, 0, 10, 10}, {0, 0, 65, 10}, {0, 0, 10, 65}, {10, 10, 10, 20}, {20, 5, 10, 10}, {0, 70, 8, 80}};
    for (const RECT& bad : bad_rects) {
        D3DLOCKED_RECT unused{};
        const HRESULT result = sys->LockRect(&unused, &bad, 0);
        context.measure("lockrect_rect_" + std::to_string(bad.left) + "_" + std::to_string(bad.top) + "_" + std::to_string(bad.right) + "_" + std::to_string(bad.bottom), Kit::hr(result));
        if (SUCCEEDED(result)) {
            sys->UnlockRect();
        }
    }
    D3DLOCKED_RECT after_bad{};
    if (FAILED(sys->LockRect(&after_bad, nullptr, D3DLOCK_READONLY))) {
        problems.add("the surface cannot be locked after the rejected rects");
    } else {
        sys->UnlockRect();
    }
    Com<IDirect3DSurface9> lockable = D3d9Tools::target(device, 64, 64, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, true);
    if (!lockable) {
        problems.add("a lockable render target cannot be created");
    } else {
        Com<IDirect3DSurface9> original;
        device->GetRenderTarget(0, original.put());
        device->SetRenderTarget(0, lockable.get());
        D3d9Tools::clear(device, 0xFF000000u);
        D3d9Tools::prepare(device, false);
        if (!D3d9Tools::draw_quad(device, 10, 12, 50, 44, 0xFFFFAA00u)) {
            problems.add("drawing into the lockable render target failed");
        }
        D3DLOCKED_RECT direct{};
        if (FAILED(lockable->LockRect(&direct, nullptr, D3DLOCK_READONLY))) {
            problems.add("a lockable render target cannot be locked READONLY");
        } else {
            const auto* pixels = static_cast<const std::uint8_t*>(direct.pBits);
            auto at = [&](UINT x, UINT y) {
                std::uint32_t value = 0;
                std::memcpy(&value, pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(direct.Pitch) + x * 4, 4);
                return value;
            };
            if ((at(20, 20) & 0xFFFFFFu) != 0xFFAA00u || (at(60, 60) & 0xFFFFFFu) != 0 || (at(10, 12) & 0xFFFFFFu) != 0xFFAA00u || (at(9, 12) & 0xFFFFFFu) != 0) {
                problems.add("a READONLY lock of a lockable render target does not show the GPU render");
            }
            lockable->UnlockRect();
        }
        device->SetRenderTarget(0, original.get());
    }
    Com<IDirect3DTexture9> texture = D3d9Tools::texture(device, 64, 64, 3, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM);
    if (texture) {
        for (UINT level = 0; level < 3; ++level) {
            D3DSURFACE_DESC desc{};
            texture->GetLevelDesc(level, &desc);
            const RECT inner = {1, 1, static_cast<LONG>(desc.Width) - 1, static_cast<LONG>(desc.Height) - 1};
            D3DLOCKED_RECT whole{};
            D3DLOCKED_RECT sub{};
            if (SUCCEEDED(texture->LockRect(level, &whole, nullptr, 0))) {
                texture->UnlockRect(level);
            }
            if (SUCCEEDED(texture->LockRect(level, &sub, &inner, 0))) {
                if (whole.Pitch != sub.Pitch) {
                    problems.add("level " + std::to_string(level) + " reports different pitches for a whole and a partial lock");
                }
                texture->UnlockRect(level);
            }
            D3DLOCKED_RECT bad{};
            const RECT outside = {0, 0, static_cast<LONG>(desc.Width) + 1, 1};
            const HRESULT beyond_result = texture->LockRect(level, &bad, &outside, 0);
            context.measure("lockrect_beyond_width_level_" + std::to_string(level), Kit::hr(beyond_result));
            if (SUCCEEDED(beyond_result)) {
                texture->UnlockRect(level);
            }
        }
    }
    D3DCAPS9 caps{};
    device->GetDeviceCaps(&caps);
    if ((caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) != 0) {
        Com<IDirect3DVolumeTexture9> volume;
        if (SUCCEEDED(device->CreateVolumeTexture(16, 16, 8, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, volume.put(), nullptr))) {
            D3DLOCKED_BOX box{};
            const D3DBOX beyond = {0, 0, 16, 16, 4, 9};
            const HRESULT box_result = volume->LockBox(0, &box, &beyond, 0);
            context.measure("lockbox_beyond_depth", Kit::hr(box_result));
            if (SUCCEEDED(box_result)) {
                volume->UnlockBox(0);
            }
            const D3DBOX inside = {2, 2, 10, 10, 1, 5};
            if (FAILED(volume->LockBox(0, &box, &inside, 0))) {
                problems.add("a partial LockBox failed");
            } else {
                volume->UnlockBox(0);
            }
        }
    }
    context.measure("bad_rects", std::size(bad_rects));
    Kit::check_usable(problems, "after the locks", device);
    return problems.outcome();
}

Outcome LockChecks::dirty_regions(Context& context) {
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!Kit::open(rig, false, error, settings)) {
        return Outcome::fail(error);
    }
    IDirect3DDevice9* device = rig.dev();
    Problems problems;
    Com<IDirect3DTexture9> managed = D3d9Tools::texture(device, 64, 64, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    if (!managed) {
        return Outcome::fail("a MANAGED texture cannot be created");
    }
    const std::vector<std::uint32_t> a = D3d9Tools::pattern(1, 64, 64);
    const std::vector<std::uint32_t> b = D3d9Tools::pattern(2, 64, 64);
    const std::vector<std::uint32_t> c = D3d9Tools::pattern(3, 64, 64);
    if (!D3d9Tools::write_texture_level(managed.get(), 0, 1, error)) {
        return Outcome::fail(error);
    }
    SceneImage image;
    if (!D3d9Tools::sample(device, managed.get(), 64, 64, image, error)) {
        return Outcome::fail(error);
    }
    const RECT inner = {8, 8, 32, 32};
    const RECT outer = {40, 40, 64, 64};
    fill_rect(managed.get(), outer, b, 64, D3DLOCK_NO_DIRTY_UPDATE);
    fill_rect(managed.get(), inner, c, 64, 0);
    if (!D3d9Tools::sample(device, managed.get(), 64, 64, image, error)) {
        return Outcome::fail(error);
    }
    int outer_new = 0;
    int outer_old = 0;
    for (UINT y = 0; y < 64; ++y) {
        for (UINT x = 0; x < 64; ++x) {
            const std::uint32_t got = D3d9Tools::pixel(image, x, y);
            const bool in_inner = x >= 8 && x < 32 && y >= 8 && y < 32;
            const bool in_outer = x >= 40 && y >= 40;
            const std::size_t i = y * 64 + x;
            if (in_inner && got != c[i]) {
                problems.add("a write inside the dirty region is not visible at (" + std::to_string(x) + "," + std::to_string(y) + ")");
                y = 64;
                break;
            }
            if (in_outer) {
                if (got == b[i]) {
                    ++outer_new;
                } else if (got == a[i]) {
                    ++outer_old;
                } else {
                    problems.add("a pixel in the NO_DIRTY_UPDATE region at (" + std::to_string(x) + "," + std::to_string(y) + ") is neither the old nor the new data");
                    y = 64;
                    break;
                }
            }
            if (!in_inner && !in_outer && got != a[i]) {
                problems.add("a pixel that was never written changed at (" + std::to_string(x) + "," + std::to_string(y) + ")");
                y = 64;
                break;
            }
        }
    }
    context.measure("no_dirty_update.shows_new_pixels", outer_new);
    context.measure("no_dirty_update.shows_old_pixels", outer_old);
    RECT invalid = {-5, 0, 10, 10};
    Kit::expect_failure(problems, "AddDirtyRect with a rect outside the texture", managed->AddDirtyRect(&invalid));
    Kit::expect_success(problems, "AddDirtyRect(NULL)", managed->AddDirtyRect(nullptr));
    if (!D3d9Tools::sample(device, managed.get(), 64, 64, image, error)) {
        return Outcome::fail(error);
    }
    for (UINT y = 0; y < 64; ++y) {
        for (UINT x = 0; x < 64; ++x) {
            const bool in_inner = x >= 8 && x < 32 && y >= 8 && y < 32;
            const bool in_outer = x >= 40 && y >= 40;
            const std::size_t i = y * 64 + x;
            const std::uint32_t expected = in_inner ? c[i] : in_outer ? b[i] : a[i];
            if (D3d9Tools::pixel(image, x, y) != expected) {
                problems.add("after AddDirtyRect(NULL) pixel (" + std::to_string(x) + "," + std::to_string(y) + ") does not show the last data written");
                y = 64;
                break;
            }
        }
    }
    Kit::check_usable(problems, "after the dirty regions", device);
    return problems.outcome();
}

Outcome LockChecks::errors(Context& context) {
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
        const std::string name = std::string("D3D") + Kit::label(ex);
        Com<IDirect3DSurface9> sys;
        device->CreateOffscreenPlainSurface(32, 32, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, sys.put(), nullptr);
        D3DLOCKED_RECT one{};
        D3DLOCKED_RECT two{};
        if (sys && SUCCEEDED(sys->LockRect(&one, nullptr, 0))) {
            Kit::expect_failure(problems, name + " a second LockRect of a locked surface", sys->LockRect(&two, nullptr, 0));
            Kit::expect_success(problems, name + " UnlockRect", sys->UnlockRect());
            Kit::expect_failure(problems, name + " UnlockRect without a lock", sys->UnlockRect());
        } else {
            problems.add(name + ": the first LockRect failed");
        }
        Com<IDirect3DTexture9> sys_texture = D3d9Tools::texture(device, 32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM);
        if (sys_texture && SUCCEEDED(sys_texture->LockRect(0, &one, nullptr, 0))) {
            const HRESULT second_texture = sys_texture->LockRect(0, &two, nullptr, 0);
            context.measure(name + ".second_texture_lockrect", Kit::hr(second_texture));
            if (SUCCEEDED(second_texture)) {
                sys_texture->UnlockRect(0);
            }
            sys_texture->UnlockRect(0);
            context.measure(name + ".texture_unlockrect_without_lock", Kit::hr(sys_texture->UnlockRect(0)));
        }
        Com<IDirect3DVertexBuffer9> vb;
        device->CreateVertexBuffer(256, 0, 0, D3DPOOL_DEFAULT, vb.put(), nullptr);
        void* memory = nullptr;
        void* again = nullptr;
        if (vb && SUCCEEDED(vb->Lock(0, 0, &memory, 0))) {
            const HRESULT second_lock = vb->Lock(0, 0, &again, 0);
            context.measure(name + ".second_vertex_buffer_lock", Kit::hr(second_lock));
            vb->Unlock();
            context.measure(name + ".vertex_buffer_unlock_without_lock", Kit::hr(vb->Unlock()));
        } else {
            problems.add(name + ": the first vertex buffer Lock failed");
        }
        if (vb) {
            const HRESULT beyond_offset = vb->Lock(300, 16, &memory, 0);
            context.measure(name + ".lock_offset_beyond_buffer", Kit::hr(beyond_offset));
            if (SUCCEEDED(beyond_offset)) {
                vb->Unlock();
            }
            const HRESULT beyond_size = vb->Lock(0, 257, &memory, 0);
            context.measure(name + ".lock_size_beyond_buffer", Kit::hr(beyond_size));
            if (SUCCEEDED(beyond_size)) {
                vb->Unlock();
            }
            const HRESULT readonly_discard = vb->Lock(0, 0, &memory, D3DLOCK_READONLY | D3DLOCK_DISCARD);
            context.measure(name + ".lock_readonly_discard", Kit::hr(readonly_discard));
            if (SUCCEEDED(readonly_discard)) {
                vb->Unlock();
            }
            const HRESULT discard = vb->Lock(0, 0, &memory, D3DLOCK_DISCARD);
            context.measure(name + ".discard_on_static_buffer", Kit::hr(discard));
            if (SUCCEEDED(discard)) {
                vb->Unlock();
            } else if (discard != D3DERR_INVALIDCALL) {
                problems.add(name + " Lock(DISCARD) on a non-dynamic buffer returned " + Kit::hr(discard));
            }
        }
        Com<IDirect3DSurface9> hidden = D3d9Tools::target(device, 32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false);
        if (hidden) {
            Kit::expect_failure(problems, name + " LockRect of a render target created without LOCKABLE", hidden->LockRect(&one, nullptr, D3DLOCK_READONLY));
        }
        if (Kit::has_depth(rig, D3DFMT_D24S8)) {
            Com<IDirect3DSurface9> depth;
            device->CreateDepthStencilSurface(32, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, depth.put(), nullptr);
            if (depth) {
                Kit::expect_failure(problems, name + " LockRect of a depth-stencil surface", depth->LockRect(&one, nullptr, D3DLOCK_READONLY));
            }
        }
        Com<IDirect3DTexture9> plain = D3d9Tools::texture(device, 32, 32, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        if (plain) {
            Kit::expect_failure(problems, name + " LockRect of a DEFAULT-pool texture that is not dynamic", plain->LockRect(0, &one, nullptr, 0));
        }
        Com<IDirect3DTexture9> target_texture = D3d9Tools::texture(device, 32, 32, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT);
        if (target_texture) {
            Kit::expect_failure(problems, name + " LockRect of a render-target texture", target_texture->LockRect(0, &one, nullptr, 0));
        }
        Kit::check_usable(problems, name, device);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::d3d9
