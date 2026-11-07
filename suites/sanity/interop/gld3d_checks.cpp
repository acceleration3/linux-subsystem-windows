#include "sanity/interop/gld3d_checks.hpp"

#include "sanity/interop/kit.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/format_codec.hpp"
#include "support/gl_frame_draw.hpp"
#include "support/gl_rig.hpp"
#include "support/swap_rig.hpp"
#include "support/wgl_interop.hpp"

#include <cmath>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::interop {

namespace {

enum class Open { Ready, Unsupported, Failed };

struct Bench {
    GlRig gl;
    WglInterop wgl;
    D3d11Device d3d11;
    HANDLE device = nullptr;
    unsigned fbo = 0;
    std::string detail;

    Open open(Context& context, int size, bool d3d11_device = true) {
        std::string error;
        GlRigOptions options;
        options.width = size;
        options.height = size;
        options.visible = false;
        if (!gl.open(options, error)) {
            detail = error;
            return Open::Failed;
        }
        gl.record(context);
        if (!gl.wgl.has(gl.dc(), "WGL_NV_DX_interop2")) {
            detail = "WGL_NV_DX_interop2 is not advertised by the context";
            return Open::Unsupported;
        }
        wgl.load(GlLibrary::instance());
        if (!wgl.complete()) {
            detail = "WGL_NV_DX_interop2 is advertised but these entry points do not resolve: " + wgl.missing();
            return Open::Failed;
        }
        if (d3d11_device) {
            if (!d3d11.create_ex(gl.adapter.luid, nullptr, 0, 0, error)) {
                detail = error;
                return Open::Failed;
            }
            device = wgl.open_device(d3d11.device);
            if (device == nullptr) {
                detail = "wglDXOpenDeviceNV returned NULL (error " + std::to_string(::GetLastError()) + ")";
                return Open::Failed;
            }
        }
        gl.gl.glGenFramebuffers(1, &fbo);
        return Open::Ready;
    }

    void close() {
        std::string error;
        gl.gl.glFinish();
        if (device != nullptr) {
            wgl.close_device(device);
            device = nullptr;
        }
        if (d3d11.device != nullptr) {
            static_cast<void>(D3d11Tools::wait_event(d3d11, d3d11.context, error));
        }
        if (fbo != 0) {
            gl.gl.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            gl.gl.glDeleteFramebuffers(1, &fbo);
            fbo = 0;
        }
        gl.gl.glFinish();
    }

    void attach(unsigned name, bool renderbuffer) const {
        const GlApi& api = gl.gl;
        api.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        if (renderbuffer) {
            api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::RENDERBUFFER, name);
        } else {
            api.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, name, 0);
        }
    }

    void read(std::vector<std::uint8_t>& out, int width, int height) const {
        const GlApi& api = gl.gl;
        out.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
        api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
        api.glReadBuffer(glc::COLOR_ATTACHMENT0);
        api.glReadPixels(0, 0, width, height, glc::RGBA, glc::UNSIGNED_BYTE, out.data());
    }
};

Outcome opened(const Bench& bench, Open state) {
    return state == Open::Unsupported ? Context::skip(bench.detail) : Outcome::fail(bench.detail);
}

std::vector<std::uint8_t> flipped(int frame, int width, int height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int row = 0; row < height; ++row) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t c = FrameImage::at(frame, width, height, x, height - 1 - row);
            std::uint8_t* px = &out[Kit::at(x, row, width) * 4u];
            px[0] = static_cast<std::uint8_t>((c >> 16) & 255u);
            px[1] = static_cast<std::uint8_t>((c >> 8) & 255u);
            px[2] = static_cast<std::uint8_t>(c & 255u);
            px[3] = 255;
        }
    }
    return out;
}

std::vector<std::uint8_t> upright(int frame, int width, int height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int row = 0; row < height; ++row) {
        for (int x = 0; x < width; ++x) {
            const std::uint32_t c = FrameImage::at(frame, width, height, x, row);
            std::uint8_t* px = &out[Kit::at(x, row, width) * 4u];
            px[0] = static_cast<std::uint8_t>((c >> 16) & 255u);
            px[1] = static_cast<std::uint8_t>((c >> 8) & 255u);
            px[2] = static_cast<std::uint8_t>(c & 255u);
            px[3] = 255;
        }
    }
    return out;
}

struct Target {
    std::string label;
    unsigned gl_name = 0;
    bool renderbuffer = false;
    HANDLE object = nullptr;
    int width = 0;
    int height = 0;
    std::function<bool(int, std::string&)> d3d_write;
    std::function<std::vector<std::uint8_t>(int)> d3d_expected;
    std::function<bool(std::vector<std::uint8_t>&, std::string&)> d3d_read;
};

void round_trip(Context& context, Problems& problems, Bench& bench, Target& target, int rounds, int first_frame) {
    const GlApi& api = bench.gl.gl;
    for (int round = 0; round < rounds; ++round) {
        const int frame = first_frame + round;
        std::string error;
        if (!target.d3d_write(frame, error)) {
            problems.add(target.label + " round " + std::to_string(round) + ": " + error);
            return;
        }
        HANDLE objects[1] = {target.object};
        if (!bench.wgl.lock_objects(bench.device, 1, objects)) {
            problems.add(target.label + " round " + std::to_string(round) + ": wglDXLockObjectsNV failed (error " + std::to_string(::GetLastError()) + ")");
            return;
        }
        bench.attach(target.gl_name, target.renderbuffer);
        std::vector<std::uint8_t> seen;
        bench.read(seen, target.width, target.height);
        const std::vector<std::uint8_t> want = target.d3d_expected(frame);
        if (seen != want) {
            problems.add(target.label + " round " + std::to_string(round) + ": GL does not read what Direct3D wrote before the lock (a stale or wrong frame)");
            ArtifactWriter writer(context);
            writer.bytes(Kit::sanitize(target.label) + ".gl_read." + std::to_string(round), seen.data(), seen.size());
            writer.bytes(Kit::sanitize(target.label) + ".d3d_wrote." + std::to_string(round), want.data(), want.size());
        }
        api.glViewport(0, 0, target.width, target.height);
        GlFrameDraw::frame(api, target.width, target.height, (frame + 30) % FrameImage::kFrames);
        api.glFinish();
        const BOOL unlocked = bench.wgl.unlock_objects(bench.device, 1, objects);
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        if (!unlocked) {
            problems.add(target.label + " round " + std::to_string(round) + ": wglDXUnlockObjectsNV failed (error " + std::to_string(::GetLastError()) + ")");
            return;
        }
        std::vector<std::uint8_t> back;
        if (!target.d3d_read(back, error)) {
            problems.add(target.label + " round " + std::to_string(round) + ": " + error);
            return;
        }
        const std::vector<std::uint8_t> drawn = flipped((frame + 30) % FrameImage::kFrames, target.width, target.height);
        if (back != drawn) {
            problems.add(target.label + " round " + std::to_string(round) + ": Direct3D does not read what GL rendered before the unlock");
            ArtifactWriter writer(context);
            writer.bytes(Kit::sanitize(target.label) + ".d3d_read." + std::to_string(round), back.data(), back.size());
            writer.bytes(Kit::sanitize(target.label) + ".gl_drew." + std::to_string(round), drawn.data(), drawn.size());
            return;
        }
    }
}

std::vector<std::uint8_t> sample_rgba(D3d11Device& d3d, ID3D11Texture2D* texture, UINT width, UINT height, std::string& error) {
    Com<ID3D11Texture2D> out = D3d11Tools::target(d3d, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    Com<ID3D11ShaderResourceView> view = D3d11Tools::srv(d3d, texture);
    Com<ID3D11RenderTargetView> rtv = out ? D3d11Tools::rtv(d3d, out.get()) : Com<ID3D11RenderTargetView>();
    Com<ID3D11PixelShader> ps = D3d11Tools::ps(d3d, "Texture2D t : register(t0);\nfloat4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(p.xy, 0)); }\n", error);
    if (!view || !rtv || !ps) {
        error = "the sampling pipeline cannot be created: " + error;
        return {};
    }
    ID3D11ShaderResourceView* views[1] = {view.get()};
    d3d.context->PSSetShaderResources(0, 1, views);
    if (!D3d11Tools::draw_fullscreen(d3d, ps.get(), rtv.get(), width, height, error)) {
        return {};
    }
    ID3D11ShaderResourceView* none[1] = {nullptr};
    d3d.context->PSSetShaderResources(0, 1, none);
    d3d.context->OMSetRenderTargets(0, nullptr, nullptr);
    TexelImage image;
    if (!D3d11Tools::wait_event(d3d, d3d.context, error) || !D3d11Tools::read_texture(d3d, out.get(), 0, image, error)) {
        return {};
    }
    return image.data;
}

}  // namespace

Outcome GlD3dChecks::d3d11_objects(Context& context) {
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    Problems problems;
    constexpr UINT kEdge = 64;
    D3d11Device& d3d = bench.d3d11;
    const GlApi& api = bench.gl.gl;
    Com<ID3D11Texture2D> as_texture = D3d11Tools::texture(d3d, D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE));
    Com<ID3D11Texture2D> as_renderbuffer = D3d11Tools::texture(d3d, D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE));
    if (!as_texture || !as_renderbuffer) {
        bench.close();
        return Outcome::fail("the Direct3D 11 textures cannot be created");
    }
    unsigned gl_texture = 0;
    unsigned gl_renderbuffer = 0;
    api.glGenTextures(1, &gl_texture);
    api.glGenRenderbuffers(1, &gl_renderbuffer);
    Target texture_target;
    texture_target.label = "texture";
    texture_target.gl_name = gl_texture;
    texture_target.width = kEdge;
    texture_target.height = kEdge;
    texture_target.object = bench.wgl.register_object(bench.device, as_texture.get(), gl_texture, glc::TEXTURE_2D, WglInterop::kAccessReadWrite);
    Target renderbuffer_target;
    renderbuffer_target.label = "renderbuffer";
    renderbuffer_target.gl_name = gl_renderbuffer;
    renderbuffer_target.renderbuffer = true;
    renderbuffer_target.width = kEdge;
    renderbuffer_target.height = kEdge;
    renderbuffer_target.object = bench.wgl.register_object(bench.device, as_renderbuffer.get(), gl_renderbuffer, glc::RENDERBUFFER, WglInterop::kAccessReadWrite);
    if (texture_target.object == nullptr) {
        problems.add("wglDXRegisterObjectNV of a D3D11 texture as GL_TEXTURE_2D failed (error " + std::to_string(::GetLastError()) + ")");
    }
    if (renderbuffer_target.object == nullptr) {
        problems.add("wglDXRegisterObjectNV of a D3D11 texture as GL_RENDERBUFFER failed (error " + std::to_string(::GetLastError()) + ")");
    }
    auto bind = [&](Target& target, ID3D11Texture2D* resource, std::uint64_t base) {
        target.d3d_write = [&d3d, resource, base](int frame, std::string& e) {
            const std::vector<std::uint8_t> data = Kit::frame_bytes(base, frame, kEdge, kEdge);
            d3d.context->UpdateSubresource(resource, 0, nullptr, data.data(), kEdge * 4u, 0);
            d3d.context->Flush();
            return D3d11Tools::wait_event(d3d, d3d.context, e);
        };
        target.d3d_expected = [base](int frame) { return Kit::frame_bytes(base, frame, kEdge, kEdge); };
        target.d3d_read = [&d3d, resource](std::vector<std::uint8_t>& out, std::string& e) {
            TexelImage image;
            if (!D3d11Tools::read_texture(d3d, resource, 0, image, e)) {
                return false;
            }
            out = image.data;
            return true;
        };
    };
    bind(texture_target, as_texture.get(), Kit::seed(40));
    bind(renderbuffer_target, as_renderbuffer.get(), Kit::seed(41));
    for (Target* target : {&texture_target, &renderbuffer_target}) {
        if (target->object != nullptr) {
            round_trip(context, problems, bench, *target, 4, 0);
        }
    }
    if (texture_target.object != nullptr) {
        const int frame = 40;
        std::string error;
        HANDLE objects[1] = {texture_target.object};
        if (texture_target.d3d_write(frame, error) && bench.wgl.lock_objects(bench.device, 1, objects)) {
            GlFrameDraw::frame(api, kEdge, kEdge, 7);
            bench.attach(texture_target.gl_name, false);
            api.glViewport(0, 0, kEdge, kEdge);
            GlFrameDraw::frame(api, kEdge, kEdge, 7);
            api.glFinish();
            bench.wgl.unlock_objects(bench.device, 1, objects);
            api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            const std::vector<std::uint8_t> sampled = sample_rgba(d3d, as_texture.get(), kEdge, kEdge, error);
            if (sampled.empty()) {
                problems.add("sampling the interop texture in a Direct3D 11 shader: " + error);
            } else if (sampled != flipped(7, kEdge, kEdge)) {
                problems.add("a Direct3D 11 shader sampling the texture after Unlock does not see the GL result");
            }
        } else {
            problems.add("the sampling pass cannot run: " + error);
        }
    }
    for (Target* target : {&texture_target, &renderbuffer_target}) {
        if (target->object != nullptr && !bench.wgl.unregister_object(bench.device, target->object)) {
            problems.add(target->label + ": wglDXUnregisterObjectNV failed (error " + std::to_string(::GetLastError()) + ")");
        }
    }
    api.glFinish();
    api.glDeleteTextures(1, &gl_texture);
    api.glDeleteRenderbuffers(1, &gl_renderbuffer);
    bench.close();
    return problems.outcome();
}

Outcome GlD3dChecks::d3d9ex_objects(Context& context) {
    Bench bench;
    const Open state = bench.open(context, 64, false);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    D3d9Rig rig;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    settings.visible = false;
    if (!rig.open(true, error) || !rig.create(settings, error)) {
        bench.close();
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    IDirect3DDevice9Ex* device9 = rig.device_ex.get();
    bench.device = bench.wgl.open_device(device9);
    if (bench.device == nullptr) {
        const DWORD code = ::GetLastError();
        bench.close();
        return Outcome::fail("wglDXOpenDeviceNV with a Direct3D 9Ex device returned NULL (error " + std::to_string(code) + ")");
    }
    Problems problems;
    constexpr UINT kEdge = 64;
    const GlApi& api = bench.gl.gl;
    HANDLE texture_share = nullptr;
    HANDLE surface_share = nullptr;
    HRESULT made = S_OK;
    Com<IDirect3DTexture9> texture = D3d9Tools::texture(device9, kEdge, kEdge, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &made, &texture_share);
    Com<IDirect3DSurface9> level = texture ? D3d9Tools::level(texture.get(), 0) : Com<IDirect3DSurface9>();
    Com<IDirect3DSurface9> surface = D3d9Tools::target(device9, kEdge, kEdge, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, nullptr);
    if (!texture || !level) {
        problems.add("a shared render target texture cannot be created on the Direct3D 9Ex device: " + D3d9Tools::hr(made));
    }
    if (!surface) {
        problems.add("a render target surface cannot be created on the Direct3D 9Ex device");
    }
    unsigned gl_texture = 0;
    unsigned gl_renderbuffer = 0;
    api.glGenTextures(1, &gl_texture);
    api.glGenRenderbuffers(1, &gl_renderbuffer);
    auto bind = [&](Target& target, IDirect3DSurface9* resource) {
        target.d3d_write = [device9, resource](int frame, std::string& e) {
            Com<IDirect3DSurface9> saved;
            device9->GetRenderTarget(0, saved.put());
            device9->SetRenderTarget(0, resource);
            for (int q = 0; q < 4; ++q) {
                const RECT r = FrameImage::quadrant(kEdge, kEdge, q);
                const D3DRECT rect{r.left, r.top, r.right, r.bottom};
                device9->Clear(1, &rect, D3DCLEAR_TARGET, FrameImage::color(frame, q), 1.0f, 0);
            }
            device9->SetRenderTarget(0, saved.get());
            return D3d9Tools::wait_event(device9, e);
        };
        target.d3d_expected = [](int frame) { return upright(frame, kEdge, kEdge); };
        target.d3d_read = [device9, resource](std::vector<std::uint8_t>& out, std::string& e) {
            SceneImage image;
            if (!D3d9Tools::read_surface(device9, resource, image, e)) {
                return false;
            }
            out.resize(image.data.size());
            for (std::size_t i = 0; i + 3 < image.data.size(); i += 4) {
                out[i] = image.data[i + 2];
                out[i + 1] = image.data[i + 1];
                out[i + 2] = image.data[i];
                out[i + 3] = image.data[i + 3];
            }
            return true;
        };
    };
    Target texture_target;
    texture_target.label = "texture";
    texture_target.gl_name = gl_texture;
    texture_target.width = kEdge;
    texture_target.height = kEdge;
    Target surface_target;
    surface_target.label = "render target surface";
    surface_target.gl_name = gl_renderbuffer;
    surface_target.renderbuffer = true;
    surface_target.width = kEdge;
    surface_target.height = kEdge;
    if (texture && level) {
        if (texture_share != nullptr) {
            bench.wgl.set_share_handle(texture.get(), texture_share);
        }
        texture_target.object = bench.wgl.register_object(bench.device, texture.get(), gl_texture, glc::TEXTURE_2D, WglInterop::kAccessReadWrite);
        if (texture_target.object == nullptr) {
            problems.add("wglDXRegisterObjectNV of an IDirect3DTexture9 as GL_TEXTURE_2D failed (error " + std::to_string(::GetLastError()) + ")");
        }
        bind(texture_target, level.get());
    }
    if (surface) {
        if (surface_share != nullptr) {
            bench.wgl.set_share_handle(surface.get(), surface_share);
        }
        surface_target.object = bench.wgl.register_object(bench.device, surface.get(), gl_renderbuffer, glc::RENDERBUFFER, WglInterop::kAccessReadWrite);
        if (surface_target.object == nullptr) {
            problems.add("wglDXRegisterObjectNV of an IDirect3DSurface9 as GL_RENDERBUFFER failed (error " + std::to_string(::GetLastError()) + ")");
        }
        bind(surface_target, surface.get());
    }
    for (Target* target : {&texture_target, &surface_target}) {
        if (target->object != nullptr) {
            round_trip(context, problems, bench, *target, 4, 0);
        }
    }
    for (Target* target : {&texture_target, &surface_target}) {
        if (target->object != nullptr && !bench.wgl.unregister_object(bench.device, target->object)) {
            problems.add(target->label + ": wglDXUnregisterObjectNV failed (error " + std::to_string(::GetLastError()) + ")");
        }
    }
    api.glFinish();
    api.glDeleteTextures(1, &gl_texture);
    api.glDeleteRenderbuffers(1, &gl_renderbuffer);
    bench.close();
    static_cast<void>(D3d9Tools::wait_event(device9, error));
    return problems.outcome();
}

Outcome GlD3dChecks::lock_ordering(Context& context) {
    Bench bench;
    const Open state = bench.open(context, 64);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    Problems problems;
    constexpr UINT kEdge = 64;
    D3d11Device& d3d = bench.d3d11;
    const GlApi& api = bench.gl.gl;
    Com<ID3D11Texture2D> resource = D3d11Tools::texture(d3d, D3d11Tools::desc2d(kEdge, kEdge, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE));
    if (!resource) {
        bench.close();
        return Outcome::fail("the Direct3D 11 texture cannot be created");
    }
    unsigned gl_texture = 0;
    api.glGenTextures(1, &gl_texture);
    Target target;
    target.label = "texture";
    target.gl_name = gl_texture;
    target.width = kEdge;
    target.height = kEdge;
    target.object = bench.wgl.register_object(bench.device, resource.get(), gl_texture, glc::TEXTURE_2D, WglInterop::kAccessReadWrite);
    if (target.object == nullptr) {
        problems.add("wglDXRegisterObjectNV failed (error " + std::to_string(::GetLastError()) + ")");
    } else {
        ID3D11Texture2D* raw = resource.get();
        target.d3d_write = [&d3d, raw](int frame, std::string& e) {
            const std::vector<std::uint8_t> data = Kit::frame_bytes(Kit::seed(42), frame, kEdge, kEdge);
            d3d.context->UpdateSubresource(raw, 0, nullptr, data.data(), kEdge * 4u, 0);
            d3d.context->Flush();
            return D3d11Tools::wait_event(d3d, d3d.context, e);
        };
        target.d3d_expected = [](int frame) { return Kit::frame_bytes(Kit::seed(42), frame, kEdge, kEdge); };
        target.d3d_read = [&d3d, raw](std::vector<std::uint8_t>& out, std::string& e) {
            TexelImage image;
            if (!D3d11Tools::read_texture(d3d, raw, 0, image, e)) {
                return false;
            }
            out = image.data;
            return true;
        };
        round_trip(context, problems, bench, target, 60, 100);
        HANDLE bogus[1] = {reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x7FFF0))};
        if (bench.wgl.lock_objects(bench.device, 1, bogus)) {
            problems.add("wglDXLockObjectsNV succeeds for an object handle that was never registered");
        }
        context.measure("lock_invalid_handle_error", static_cast<long long>(::GetLastError()));
        if (bench.wgl.unlock_objects(bench.device, 1, bogus)) {
            problems.add("wglDXUnlockObjectsNV succeeds for an object handle that was never registered");
        }
        if (bench.wgl.register_object(bench.device, resource.get(), gl_texture, 0xFFFF, WglInterop::kAccessReadWrite) != nullptr) {
            problems.add("wglDXRegisterObjectNV accepts an invalid GL object type");
        }
        if (!bench.wgl.unregister_object(bench.device, target.object)) {
            problems.add("wglDXUnregisterObjectNV of an unlocked object failed (error " + std::to_string(::GetLastError()) + ")");
        }
    }
    api.glFinish();
    api.glDeleteTextures(1, &gl_texture);
    bench.close();
    return problems.outcome();
}

Outcome GlD3dChecks::formats_and_targets(Context& context) {
    Bench bench;
    const Open state = bench.open(context, 32);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    Problems problems;
    constexpr int kEdge = 32;
    D3d11Device& d3d = bench.d3d11;
    const GlApi& api = bench.gl.gl;
    struct Case {
        const char* name;
        DXGI_FORMAT dxgi;
        unsigned gl_internal;
        bool depth;
        bool srgb;
        int samples;
        bool floats;
    };
    const Case cases[] = {{"RGBA8", DXGI_FORMAT_R8G8B8A8_UNORM, glc::RGBA8, false, false, 1, false},
                          {"RGBA8 sRGB", DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, glc::SRGB8_ALPHA8, false, true, 1, false},
                          {"RGBA16F", DXGI_FORMAT_R16G16B16A16_FLOAT, glc::RGBA16F, false, false, 1, true},
                          {"RGBA32F", DXGI_FORMAT_R32G32B32A32_FLOAT, glc::RGBA32F, false, false, 1, true},
                          {"D32F depth", DXGI_FORMAT_D32_FLOAT, glc::DEPTH_COMPONENT32F, true, false, 1, true},
                          {"RGBA8 4x MSAA", DXGI_FORMAT_R8G8B8A8_UNORM, glc::RGBA8, false, false, 4, false}};
    for (const Case& item : cases) {
        UINT levels = 0;
        if (item.samples > 1 && (FAILED(d3d.device->CheckMultisampleQualityLevels(item.dxgi, static_cast<UINT>(item.samples), &levels)) || levels == 0)) {
            context.measure(std::string("usable.") + item.name, "multisampling is not offered by Direct3D 11 for this format");
            continue;
        }
        D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(kEdge, kEdge, item.dxgi, item.depth ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 1, static_cast<UINT>(item.samples));
        Com<ID3D11Texture2D> resource = D3d11Tools::texture(d3d, desc);
        if (!resource) {
            context.measure(std::string("usable.") + item.name, "Direct3D 11 cannot create the texture");
            continue;
        }
        unsigned gl_object = 0;
        const bool as_renderbuffer = item.depth || item.samples > 1;
        if (as_renderbuffer) {
            api.glGenRenderbuffers(1, &gl_object);
        } else {
            api.glGenTextures(1, &gl_object);
        }
        HANDLE object = bench.wgl.register_object(bench.device, resource.get(), gl_object, as_renderbuffer ? glc::RENDERBUFFER : glc::TEXTURE_2D, WglInterop::kAccessReadWrite);
        if (object == nullptr) {
            context.measure(std::string("usable.") + item.name, "wglDXRegisterObjectNV declines (error " + std::to_string(::GetLastError()) + ")");
            if (as_renderbuffer) {
                api.glDeleteRenderbuffers(1, &gl_object);
            } else {
                api.glDeleteTextures(1, &gl_object);
            }
            continue;
        }
        context.measure(std::string("usable.") + item.name, "registered");
        unsigned twin = 0;
        unsigned twin_fbo = 0;
        api.glGenRenderbuffers(1, &twin);
        api.glBindRenderbuffer(glc::RENDERBUFFER, twin);
        if (item.samples > 1) {
            api.glRenderbufferStorageMultisample(glc::RENDERBUFFER, item.samples, item.gl_internal, kEdge, kEdge);
        } else {
            api.glRenderbufferStorage(glc::RENDERBUFFER, item.gl_internal, kEdge, kEdge);
        }
        api.glGenFramebuffers(1, &twin_fbo);
        const unsigned attachment = item.depth ? glc::DEPTH_ATTACHMENT : glc::COLOR_ATTACHMENT0;
        auto render = [&](unsigned fbo_name) {
            api.glBindFramebuffer(glc::FRAMEBUFFER, fbo_name);
            api.glViewport(0, 0, kEdge, kEdge);
            if (item.srgb) {
                api.glEnable(glc::FRAMEBUFFER_SRGB);
            }
            if (item.depth) {
                api.glClearDepth(0.25);
                api.glClear(glc::DEPTH_BUFFER_BIT);
            } else {
                GlFrameDraw::frame(api, kEdge, kEdge, 17);
            }
            if (item.srgb) {
                api.glDisable(glc::FRAMEBUFFER_SRGB);
            }
            api.glFinish();
        };
        std::vector<std::uint8_t> twin_bytes(static_cast<std::size_t>(kEdge) * kEdge * 16u);
        api.glBindFramebuffer(glc::FRAMEBUFFER, twin_fbo);
        api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, attachment, glc::RENDERBUFFER, twin);
        const bool twin_complete = api.glCheckFramebufferStatus(glc::FRAMEBUFFER) == glc::FRAMEBUFFER_COMPLETE;
        auto resolve_read = [&](unsigned source_fbo, std::vector<std::uint8_t>& out) {
            unsigned resolve_rbo = 0;
            unsigned resolve_fbo = 0;
            api.glGenRenderbuffers(1, &resolve_rbo);
            api.glBindRenderbuffer(glc::RENDERBUFFER, resolve_rbo);
            api.glRenderbufferStorage(glc::RENDERBUFFER, item.gl_internal, kEdge, kEdge);
            api.glGenFramebuffers(1, &resolve_fbo);
            api.glBindFramebuffer(glc::FRAMEBUFFER, resolve_fbo);
            api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, attachment, glc::RENDERBUFFER, resolve_rbo);
            api.glBindFramebuffer(glc::READ_FRAMEBUFFER, source_fbo);
            api.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, resolve_fbo);
            api.glBlitFramebuffer(0, 0, kEdge, kEdge, 0, 0, kEdge, kEdge, item.depth ? glc::DEPTH_BUFFER_BIT : glc::COLOR_BUFFER_BIT, glc::NEAREST);
            api.glBindFramebuffer(glc::READ_FRAMEBUFFER, resolve_fbo);
            api.glPixelStorei(glc::PACK_ALIGNMENT, 1);
            if (item.depth) {
                api.glReadPixels(0, 0, kEdge, kEdge, glc::DEPTH_COMPONENT, glc::FLOAT, out.data());
            } else if (item.floats) {
                api.glReadPixels(0, 0, kEdge, kEdge, glc::RGBA, glc::FLOAT, out.data());
            } else {
                api.glReadPixels(0, 0, kEdge, kEdge, glc::RGBA, glc::UNSIGNED_BYTE, out.data());
            }
            api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
            api.glDeleteFramebuffers(1, &resolve_fbo);
            api.glDeleteRenderbuffers(1, &resolve_rbo);
        };
        std::string error;
        if (!twin_complete) {
            problems.add(std::string(item.name) + ": the pure GL twin framebuffer is incomplete");
        } else {
            render(twin_fbo);
            resolve_read(twin_fbo, twin_bytes);
            HANDLE objects[1] = {object};
            if (!bench.wgl.lock_objects(bench.device, 1, objects)) {
                problems.add(std::string(item.name) + ": wglDXLockObjectsNV failed (error " + std::to_string(::GetLastError()) + ")");
            } else {
                bench.gl.gl.glBindFramebuffer(glc::FRAMEBUFFER, bench.fbo);
                if (as_renderbuffer) {
                    api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, attachment, glc::RENDERBUFFER, gl_object);
                } else {
                    api.glFramebufferTexture2D(glc::FRAMEBUFFER, attachment, glc::TEXTURE_2D, gl_object, 0);
                }
                if (api.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
                    problems.add(std::string(item.name) + ": the framebuffer over the interop object is incomplete");
                } else {
                    render(bench.fbo);
                }
                api.glBindFramebuffer(glc::FRAMEBUFFER, bench.fbo);
                api.glFramebufferRenderbuffer(glc::FRAMEBUFFER, attachment, glc::RENDERBUFFER, 0);
                api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
                api.glFinish();
                bench.wgl.unlock_objects(bench.device, 1, objects);
                Com<ID3D11Texture2D> resolved;
                ID3D11Texture2D* flat = resource.get();
                if (item.samples > 1) {
                    resolved = D3d11Tools::texture(d3d, D3d11Tools::desc2d(kEdge, kEdge, item.dxgi, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET));
                    flat = resolved.get();
                    if (flat != nullptr) {
                        d3d.context->ResolveSubresource(flat, 0, resource.get(), 0, item.dxgi);
                    }
                }
                TexelImage image;
                if (flat == nullptr || !D3d11Tools::wait_event(d3d, d3d.context, error) || !D3d11Tools::read_texture(d3d, flat, 0, image, error)) {
                    problems.add(std::string(item.name) + ": Direct3D cannot read the object back: " + error);
                } else {
                    const FormatSpec spec = FormatCodec::spec(item.dxgi);
                    const int element = FormatCodec::element_bytes(item.dxgi);
                    std::size_t bad = 0;
                    std::string first;
                    for (int i = 0; i < kEdge * kEdge; ++i) {
                        if (item.floats) {
                            double decoded[4];
                            FormatCodec::decode(spec, &image.data[static_cast<std::size_t>(i * element)], decoded);
                            const float* twin_float = reinterpret_cast<const float*>(twin_bytes.data());
                            const int count = item.depth ? 1 : 4;
                            for (int c = 0; c < count; ++c) {
                                const double have = decoded[item.depth ? 0 : c];
                                const double want = twin_float[i * count + c];
                                if (std::fabs(have - want) > (item.dxgi == DXGI_FORMAT_R16G16B16A16_FLOAT ? 1.0 / 1024.0 : 0.0)) {
                                    if (bad++ == 0) {
                                        first = "texel " + std::to_string(i) + " channel " + std::to_string(c) + " is " + std::to_string(have) + ", the GL twin has " + std::to_string(want);
                                    }
                                }
                            }
                        } else if (std::memcmp(&image.data[static_cast<std::size_t>(i) * 4u], &twin_bytes[static_cast<std::size_t>(i) * 4u], 4) != 0) {
                            if (bad++ == 0) {
                                first = "texel " + std::to_string(i) + " differs from the GL twin";
                            }
                        }
                    }
                    if (bad != 0) {
                        problems.add(std::string(item.name) + ": the interop object renders differently from its pure GL twin: " + std::to_string(bad) + " mismatches, first " + first);
                    }
                }
            }
        }
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        api.glFinish();
        if (!bench.wgl.unregister_object(bench.device, object)) {
            problems.add(std::string(item.name) + ": wglDXUnregisterObjectNV failed (error " + std::to_string(::GetLastError()) + ")");
        }
        api.glDeleteFramebuffers(1, &twin_fbo);
        api.glDeleteRenderbuffers(1, &twin);
        if (as_renderbuffer) {
            api.glDeleteRenderbuffers(1, &gl_object);
        } else {
            api.glDeleteTextures(1, &gl_object);
        }
        api.glFinish();
    }
    bench.close();
    return problems.outcome();
}

Outcome GlD3dChecks::present_via_d3d(Context& context) {
    Bench bench;
    const Open state = bench.open(context, 64, false);
    if (state != Open::Ready) {
        return opened(bench, state);
    }
    SwapRig swap;
    SwapOptions options;
    options.width = 128;
    options.height = 128;
    options.effect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    options.buffers = 2;
    options.usage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    std::string error;
    if (!swap.open(options, error)) {
        bench.close();
        return Outcome::fail("the Direct3D 11 swap chain: " + error);
    }
    Problems problems;
    const GlApi& api = bench.gl.gl;
    bench.device = bench.wgl.open_device(swap.device.device);
    if (bench.device == nullptr) {
        const DWORD code = ::GetLastError();
        swap.chain.reset();
        swap.device.destroy();
        bench.close();
        return Outcome::fail("wglDXOpenDeviceNV with the swap chain device returned NULL (error " + std::to_string(code) + ")");
    }
    Com<ID3D11Texture2D> back = swap.back_buffer(0);
    unsigned gl_texture = 0;
    api.glGenTextures(1, &gl_texture);
    HANDLE object = bench.wgl.register_object(bench.device, back.get(), gl_texture, glc::TEXTURE_2D, WglInterop::kAccessWriteDiscard);
    if (object == nullptr) {
        problems.add("wglDXRegisterObjectNV of the swap chain back buffer failed (error " + std::to_string(::GetLastError()) + ")");
    }
    for (int frame = 0; object != nullptr && frame < 6; ++frame) {
        HANDLE objects[1] = {object};
        if (!bench.wgl.lock_objects(bench.device, 1, objects)) {
            problems.add("frame " + std::to_string(frame) + ": wglDXLockObjectsNV failed (error " + std::to_string(::GetLastError()) + ")");
            break;
        }
        bench.attach(gl_texture, false);
        api.glViewport(0, 0, 128, 128);
        GlFrameDraw::frame(api, 128, 128, frame + 3);
        api.glFinish();
        api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
        bench.wgl.unlock_objects(bench.device, 1, objects);
        const HRESULT presented = swap.chain->Present(1, 0);
        if (FAILED(presented)) {
            problems.add("frame " + std::to_string(frame) + ": Present returned " + Kit::hr(presented));
            break;
        }
        static_cast<void>(D3d11Tools::wait_event(swap.device, swap.device.context, error));
        std::vector<std::uint32_t> want(128u * 128u);
        const std::vector<std::uint8_t> rows = flipped(frame + 3, 128, 128);
        for (std::size_t i = 0; i < want.size(); ++i) {
            want[i] = Kit::pack(rows[i * 4], rows[i * 4 + 1], rows[i * 4 + 2]);
        }
        std::string detail;
        if (!Kit::settle_screen(context, "present_via_d3d_" + std::to_string(frame), ScreenCapture::client_on_screen(swap.window.hwnd()), want, 0, 3000, detail)) {
            problems.add("frame " + std::to_string(frame) + ": the presented GL frame is not what GL rendered: " + detail);
            break;
        }
    }
    api.glFinish();
    if (object != nullptr) {
        bench.wgl.unregister_object(bench.device, object);
    }
    back.reset();
    api.glDeleteTextures(1, &gl_texture);
    bench.close();
    swap.chain.reset();
    swap.device.destroy();
    return problems.outcome();
}

Outcome GlD3dChecks::caps_unsupported(Context& context) {
    Bench bench;
    Open state = bench.open(context, 64, false);
    if (state == Open::Failed && bench.detail.find("do not resolve") == std::string::npos) {
        return Outcome::fail(bench.detail);
    }
    Problems problems;
    const bool interop2 = bench.gl.wgl.has(bench.gl.dc(), "WGL_NV_DX_interop2");
    const bool interop = bench.gl.wgl.has(bench.gl.dc(), "WGL_NV_DX_interop");
    context.measure("WGL_NV_DX_interop", interop);
    context.measure("WGL_NV_DX_interop2", interop2);
    if (interop2 && !interop) {
        problems.add("WGL_NV_DX_interop2 is advertised without WGL_NV_DX_interop, which it extends");
    }
    WglInterop wgl;
    wgl.load(GlLibrary::instance());
    const std::string missing = wgl.missing();
    if (interop && !missing.empty()) {
        problems.add("WGL_NV_DX_interop is advertised but these entry points do not resolve: " + missing);
    }
    D3d11Device d3d;
    std::string error;
    if (!d3d.create_ex(bench.gl.adapter.luid, nullptr, 0, 0, error)) {
        problems.add(error);
    } else if (interop && wgl.complete()) {
        HANDLE device = wgl.open_device(d3d.device);
        if (device == nullptr) {
            problems.add("wglDXOpenDeviceNV fails for a valid Direct3D 11 device although WGL_NV_DX_interop is advertised (error " + std::to_string(::GetLastError()) + ")");
        } else {
            if (wgl.open_device(nullptr) != nullptr) {
                problems.add("wglDXOpenDeviceNV accepts a NULL device");
            }
            wgl.close_device(device);
        }
    } else if (!interop && wgl.open_device != nullptr) {
        HANDLE device = wgl.open_device(d3d.device);
        if (device != nullptr) {
            wgl.close_device(device);
            problems.add("wglDXOpenDeviceNV succeeds although WGL_NV_DX_interop is not advertised");
        }
    }
    if (!interop && !interop2 && wgl.complete()) {
        context.note("the WGL_NV_DX_interop entry points resolve although the extension is not advertised; opening a device fails");
    }
    bench.close();
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
