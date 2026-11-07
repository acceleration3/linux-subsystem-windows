#pragma once

#include "core/text.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"
#include "support/d3d9_twin.hpp"
#include "support/problems.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace lsw::suites::sanity::d3d9 {
class Kit {
public:
    static constexpr HRESULT kInvalid = D3DERR_INVALIDCALL;

    [[nodiscard]] static bool open(D3d9Rig& rig, bool ex, std::string& error, const D3d9Settings& settings = {}) {
        return rig.open(ex, error) && rig.create(settings, error);
    }

    [[nodiscard]] static std::string hr(HRESULT result) { return D3d9Tools::hr(result); }

    [[nodiscard]] static std::string label(bool ex) { return ex ? "9Ex" : "9"; }

    static void expect(Problems& problems, const std::string& what, HRESULT actual, HRESULT wanted) {
        if (actual != wanted) {
            problems.add(what + " returned " + hr(actual) + ", expected " + hr(wanted));
        }
    }

    static void expect_failure(Problems& problems, const std::string& what, HRESULT actual, HRESULT wanted = D3DERR_INVALIDCALL) {
        if (SUCCEEDED(actual)) {
            problems.add(what + " succeeded, expected " + hr(wanted));
        } else if (actual != wanted) {
            problems.add(what + " returned " + hr(actual) + ", expected " + hr(wanted));
        }
    }

    static void expect_success(Problems& problems, const std::string& what, HRESULT actual) {
        if (FAILED(actual)) {
            problems.add(what + " failed with " + hr(actual));
        }
    }

    [[nodiscard]] static bool usable(IDirect3DDevice9* device, std::string& error) {
        HRESULT result = S_OK;
        Com<IDirect3DSurface9> saved;
        device->GetRenderTarget(0, saved.put());
        Com<IDirect3DSurface9> rt = D3d9Tools::target(device, 8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, false, &result);
        if (!rt) {
            error = "the device cannot create a render target afterwards: " + hr(result);
            return false;
        }
        device->SetRenderTarget(0, rt.get());
        D3d9Tools::clear(device, 0xFF123456u);
        SceneImage image;
        const bool read = D3d9Tools::read_surface(device, rt.get(), image, error);
        device->SetRenderTarget(0, saved.get());
        if (!read) {
            return false;
        }
        std::string where;
        if (!D3d9Tools::uniform(image, 0xFF123456u, 0, where)) {
            error = "a clear on the device afterwards is wrong: " + where;
            return false;
        }
        return true;
    }

    static void check_usable(Problems& problems, const std::string& label_text, IDirect3DDevice9* device) {
        std::string error;
        if (!usable(device, error)) {
            problems.add(label_text + ": " + error);
        }
    }

    [[nodiscard]] static D3DCOLOR rgb(int r, int g, int b, int a = 255) {
        return D3DCOLOR_ARGB(static_cast<unsigned>(a), static_cast<unsigned>(r), static_cast<unsigned>(g), static_cast<unsigned>(b));
    }

    [[nodiscard]] static std::uint32_t frame_color(int k) {
        return rgb((k * 37 + 11) & 255, (k * 91 + 5) & 255, (k * 53 + 200) & 255);
    }

    [[nodiscard]] static TwinScene textured_quad(const std::vector<std::uint32_t>& texels, UINT texture_width, UINT texture_height,
                                                 UINT width, UINT height, float left, float top, float right, float bottom,
                                                 float u0, float v0, float u1, float v1, bool linear, bool opaque, float clear_level) {
        TwinScene scene;
        scene.width = width;
        scene.height = height;
        scene.linear = linear;
        scene.clear[0] = scene.clear[1] = scene.clear[2] = clear_level;
        scene.clear[3] = 1.0f;
        const float lx = left * 2.0f / static_cast<float>(width) - 1.0f;
        const float rx = right * 2.0f / static_cast<float>(width) - 1.0f;
        const float ty = 1.0f - top * 2.0f / static_cast<float>(height);
        const float by = 1.0f - bottom * 2.0f / static_cast<float>(height);
        const float data[16] = {lx, ty, u0, v0, rx, ty, u1, v0, lx, by, u0, v1, rx, by, u1, v1};
        scene.vertices.resize(sizeof(data));
        std::memcpy(scene.vertices.data(), data, sizeof(data));
        scene.stride = 16;
        scene.vertex_count = 4;
        scene.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        scene.elements = {{"POSITION", DXGI_FORMAT_R32G32_FLOAT, 0}, {"TEXCOORD", DXGI_FORMAT_R32G32_FLOAT, 8}};
        scene.vs = "struct I { float2 p : POSITION; float2 uv : TEXCOORD; };\n"
                   "struct O { float4 p : SV_Position; float2 uv : TEXCOORD; };\n"
                   "O main(I i) { O o; o.p = float4(i.p, 0, 1); o.uv = i.uv; return o; }\n";
        scene.ps = std::string("Texture2D t0 : register(t0); SamplerState s0 : register(s0);\n"
                               "float4 main(float4 p : SV_Position, float2 uv : TEXCOORD) : SV_Target { float4 c = t0.Sample(s0, uv); return ") +
                   (opaque ? "float4(c.rgb, 1); }\n" : "c; }\n");
        TwinTexture texture;
        texture.width = texture_width;
        texture.height = texture_height;
        texture.bgra.resize(texels.size() * 4);
        std::memcpy(texture.bgra.data(), texels.data(), texture.bgra.size());
        scene.textures.push_back(std::move(texture));
        return scene;
    }

    [[nodiscard]] static bool has_depth(D3d9Rig& rig, D3DFORMAT format) {
        return SUCCEEDED(rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, format));
    }
};
}  // namespace lsw::suites::sanity::d3d9
