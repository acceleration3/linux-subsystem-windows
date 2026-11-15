#pragma once

#include "render/d3d12/compute.hpp"

#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

class SceneMath {
public:
    [[nodiscard]] static rr::Mat4 look_at(const float eye[3], const float center[3], const float up[3]) {
        float f[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
        normalize(f);
        float s[3];
        cross(f, up, s);
        normalize(s);
        float u[3];
        cross(s, f, u);
        rr::Mat4 m;
        m.m[0] = s[0];
        m.m[4] = s[1];
        m.m[8] = s[2];
        m.m[1] = u[0];
        m.m[5] = u[1];
        m.m[9] = u[2];
        m.m[2] = -f[0];
        m.m[6] = -f[1];
        m.m[10] = -f[2];
        m.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
        m.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
        m.m[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
        return m;
    }

    [[nodiscard]] static rr::Mat4 ortho_d3d(float width, float height, float near_plane, float far_plane) {
        rr::Mat4 m;
        m.m[0] = 2.0f / width;
        m.m[5] = 2.0f / height;
        m.m[10] = 1.0f / (near_plane - far_plane);
        m.m[14] = near_plane / (near_plane - far_plane);
        return m;
    }

    [[nodiscard]] static rr::Mat4 inverse(const rr::Mat4& matrix) {
        const float* m = matrix.m;
        float inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        det = det == 0.0f ? 1.0f : 1.0f / det;
        rr::Mat4 out;
        for (int i = 0; i < 16; ++i) {
            out.m[i] = inv[i] * det;
        }
        return out;
    }

private:
    static void normalize(float v[3]) {
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int i = 0; i < 3; ++i) {
            v[i] /= length;
        }
    }
    static void cross(const float a[3], const float b[3], float out[3]) {
        out[0] = a[1] * b[2] - a[2] * b[1];
        out[1] = a[2] * b[0] - a[0] * b[2];
        out[2] = a[0] * b[1] - a[1] * b[0];
    }
};

class UploadRing {
public:
    [[nodiscard]] bool create(Kit& kit, UINT slot_bytes_in, UINT slot_count, std::string& error) {
        slot_bytes = slot_bytes_in;
        buffer = kit.make_upload(nullptr, static_cast<UINT64>(slot_bytes) * slot_count, error);
        if (buffer == nullptr) {
            return false;
        }
        const D3D12_RANGE none{0, 0};
        const HRESULT result = buffer->Map(0, &none, reinterpret_cast<void**>(&mapped));
        if (FAILED(result)) {
            return fail_with(error, "Map of the upload ring: " + D3d12Tools::hr(result));
        }
        return true;
    }
    [[nodiscard]] std::uint8_t* at(UINT slot, UINT offset = 0) const { return mapped + static_cast<std::size_t>(slot) * slot_bytes + offset; }
    [[nodiscard]] D3D12_GPU_VIRTUAL_ADDRESS address(UINT slot, UINT offset = 0) const { return buffer->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * slot_bytes + offset; }

    ID3D12Resource* buffer = nullptr;
    std::uint8_t* mapped = nullptr;
    UINT slot_bytes = 0;
};

inline void pass_target(ID3D12GraphicsCommandList* list, Surface& target, UINT width, UINT height, const float* clear) {
    target.bind_target(list);
    if (clear != nullptr) {
        list->ClearRenderTargetView(target.view, clear, 0, nullptr);
    }
    const D3D12_VIEWPORT vp = viewport_of(width, height);
    const D3D12_RECT scissor = rect_of(width, height);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &scissor);
}

inline void draw_fullscreen(ID3D12GraphicsCommandList* list, ID3D12PipelineState* state) {
    list->SetPipelineState(state);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

}  // namespace lsw::suites::render::d3d12
