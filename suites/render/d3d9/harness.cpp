#include "render/d3d9/harness.hpp"

#include <algorithm>

namespace lsw::suites::render::d3d9 {

Xf Xf::translate(float x, float y, float z) {
    Xf out;
    out.m[3][0] = x;
    out.m[3][1] = y;
    out.m[3][2] = z;
    return out;
}

Xf Xf::scale(float x, float y, float z) {
    Xf out;
    out.m[0][0] = x;
    out.m[1][1] = y;
    out.m[2][2] = z;
    return out;
}

Xf Xf::rot_x(float radians) {
    Xf out;
    out.m[1][1] = std::cos(radians);
    out.m[1][2] = std::sin(radians);
    out.m[2][1] = -std::sin(radians);
    out.m[2][2] = std::cos(radians);
    return out;
}

Xf Xf::rot_y(float radians) {
    Xf out;
    out.m[0][0] = std::cos(radians);
    out.m[0][2] = -std::sin(radians);
    out.m[2][0] = std::sin(radians);
    out.m[2][2] = std::cos(radians);
    return out;
}

Xf Xf::rot_z(float radians) {
    Xf out;
    out.m[0][0] = std::cos(radians);
    out.m[0][1] = std::sin(radians);
    out.m[1][0] = -std::sin(radians);
    out.m[1][1] = std::cos(radians);
    return out;
}

Xf Xf::perspective(float fovy, float aspect, float near_plane, float far_plane) {
    Xf out;
    const float ys = 1.0f / std::tan(fovy * 0.5f);
    out.m[0][0] = ys / aspect;
    out.m[1][1] = ys;
    out.m[2][2] = far_plane / (far_plane - near_plane);
    out.m[2][3] = 1.0f;
    out.m[3][2] = -near_plane * far_plane / (far_plane - near_plane);
    out.m[3][3] = 0.0f;
    return out;
}

Xf Xf::ortho(float width, float height, float near_plane, float far_plane) {
    Xf out;
    out.m[0][0] = 2.0f / width;
    out.m[1][1] = 2.0f / height;
    out.m[2][2] = 1.0f / (far_plane - near_plane);
    out.m[3][2] = -near_plane / (far_plane - near_plane);
    return out;
}

Xf Xf::look_at(const float eye[3], const float at[3], const float up[3]) {
    float z[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    const auto norm = [](float v[3]) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    };
    norm(z);
    float x[3] = {up[1] * z[2] - up[2] * z[1], up[2] * z[0] - up[0] * z[2], up[0] * z[1] - up[1] * z[0]};
    norm(x);
    const float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    Xf out;
    for (int i = 0; i < 3; ++i) {
        out.m[i][0] = x[i];
        out.m[i][1] = y[i];
        out.m[i][2] = z[i];
    }
    out.m[3][0] = -(x[0] * eye[0] + x[1] * eye[1] + x[2] * eye[2]);
    out.m[3][1] = -(y[0] * eye[0] + y[1] * eye[1] + y[2] * eye[2]);
    out.m[3][2] = -(z[0] * eye[0] + z[1] * eye[1] + z[2] * eye[2]);
    return out;
}

Xf Xf::operator*(const Xf& rhs) const {
    Xf out;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += m[r][k] * rhs.m[k][c];
            }
            out.m[r][c] = sum;
        }
    }
    return out;
}

Xf Xf::rigid_inverse() const {
    Xf out;
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) {
            out.m[r][k] = m[k][r];
        }
    }
    for (int k = 0; k < 3; ++k) {
        out.m[3][k] = -(m[3][0] * out.m[0][k] + m[3][1] * out.m[1][k] + m[3][2] * out.m[2][k]);
    }
    return out;
}

Xf Xf::mirror_y() const { return scale(1.0f, -1.0f, 1.0f) * *this; }

Mesh Geo::sphere(unsigned slices, unsigned stacks, float radius, DWORD color) {
    Mesh mesh;
    for (unsigned i = 0; i <= stacks; ++i) {
        const float theta = 3.14159265f * static_cast<float>(i) / static_cast<float>(stacks);
        for (unsigned j = 0; j <= slices; ++j) {
            const float phi = 6.2831853f * static_cast<float>(j) / static_cast<float>(slices);
            const float nx = std::sin(theta) * std::cos(phi);
            const float ny = std::cos(theta);
            const float nz = std::sin(theta) * std::sin(phi);
            mesh.vertices.push_back({nx * radius, ny * radius, nz * radius, nx, ny, nz, color, 0xFF000000u,
                                     static_cast<float>(j) / static_cast<float>(slices), static_cast<float>(i) / static_cast<float>(stacks)});
        }
    }
    for (unsigned i = 0; i < stacks; ++i) {
        for (unsigned j = 0; j < slices; ++j) {
            const std::uint16_t a = static_cast<std::uint16_t>(i * (slices + 1) + j);
            const std::uint16_t b = static_cast<std::uint16_t>(a + 1);
            const std::uint16_t c = static_cast<std::uint16_t>(a + slices + 1);
            const std::uint16_t d = static_cast<std::uint16_t>(c + 1);
            mesh.indices.insert(mesh.indices.end(), {a, b, d, a, d, c});
        }
    }
    return mesh;
}

Mesh Geo::grid(unsigned cells, float extent, float height, DWORD color) {
    Mesh mesh;
    const auto h = [&](float x, float z) { return height * std::sin(x * 1.3f) * std::cos(z * 1.1f); };
    for (unsigned j = 0; j <= cells; ++j) {
        for (unsigned i = 0; i <= cells; ++i) {
            const float x = -extent + 2.0f * extent * static_cast<float>(i) / static_cast<float>(cells);
            const float z = -extent + 2.0f * extent * static_cast<float>(j) / static_cast<float>(cells);
            const float e = 0.01f;
            float nx = -(h(x + e, z) - h(x - e, z)) / (2 * e);
            float nz = -(h(x, z + e) - h(x, z - e)) / (2 * e);
            const float len = std::sqrt(nx * nx + 1.0f + nz * nz);
            mesh.vertices.push_back({x, h(x, z), z, nx / len, 1.0f / len, nz / len, color, 0xFF000000u,
                                     static_cast<float>(i) * 0.5f, static_cast<float>(cells - j) * 0.5f});
        }
    }
    for (unsigned j = 0; j < cells; ++j) {
        for (unsigned i = 0; i < cells; ++i) {
            const std::uint16_t bl = static_cast<std::uint16_t>(j * (cells + 1) + i);
            const std::uint16_t br = static_cast<std::uint16_t>(bl + 1);
            const std::uint16_t tl = static_cast<std::uint16_t>(bl + cells + 1);
            const std::uint16_t tr = static_cast<std::uint16_t>(tl + 1);
            mesh.indices.insert(mesh.indices.end(), {tl, tr, br, tl, br, bl});
        }
    }
    return mesh;
}

Mesh Geo::cube(float h, DWORD color) {
    struct Face {
        float n[3];
        float c[4][3];
    };
    const Face faces[6] = {
        {{0, 0, -1}, {{-h, h, -h}, {h, h, -h}, {h, -h, -h}, {-h, -h, -h}}},
        {{0, 0, 1}, {{h, h, h}, {-h, h, h}, {-h, -h, h}, {h, -h, h}}},
        {{-1, 0, 0}, {{-h, h, h}, {-h, h, -h}, {-h, -h, -h}, {-h, -h, h}}},
        {{1, 0, 0}, {{h, h, -h}, {h, h, h}, {h, -h, h}, {h, -h, -h}}},
        {{0, 1, 0}, {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},
        {{0, -1, 0}, {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
    };
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    Mesh mesh;
    for (const Face& face : faces) {
        const std::uint16_t base = static_cast<std::uint16_t>(mesh.vertices.size());
        for (int k = 0; k < 4; ++k) {
            mesh.vertices.push_back({face.c[k][0], face.c[k][1], face.c[k][2], face.n[0], face.n[1], face.n[2], color, 0xFF000000u, uv[k][0], uv[k][1]});
        }
        mesh.indices.insert(mesh.indices.end(), {base, static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2), base,
                                                 static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)});
    }
    return mesh;
}

std::vector<Vtx> Geo::quad(float x0, float y0, float x1, float y1, float z, DWORD color) {
    const Vtx tl{x0, y1, z, 0, 0, -1, color, 0xFF000000u, 0, 0};
    const Vtx tr{x1, y1, z, 0, 0, -1, color, 0xFF000000u, 1, 0};
    const Vtx br{x1, y0, z, 0, 0, -1, color, 0xFF000000u, 1, 1};
    const Vtx bl{x0, y0, z, 0, 0, -1, color, 0xFF000000u, 0, 1};
    return {tl, tr, br, tl, br, bl};
}

std::vector<TlVtx> Geo::tl_quad(float x0, float y0, float x1, float y1, DWORD color, float u0, float v0, float u1, float v1, float z) {
    const TlVtx tl{x0, y0, z, 1.0f, color, u0, v0};
    const TlVtx tr{x1, y0, z, 1.0f, color, u1, v0};
    const TlVtx br{x1, y1, z, 1.0f, color, u1, v1};
    const TlVtx bl{x0, y1, z, 1.0f, color, u0, v1};
    return {tl, tr, br, tl, br, bl};
}

Fill Texels::checker(unsigned cells, std::uint32_t a, std::uint32_t b) {
    return [=](UINT x, UINT y, UINT w, UINT h) {
        const UINT cx = x * cells / std::max(1u, w);
        const UINT cy = y * cells / std::max(1u, h);
        return ((cx + cy) & 1u) ? b : a;
    };
}

Fill Texels::rainbow() {
    return [](UINT x, UINT y, UINT w, UINT h) {
        const unsigned r = x * 255 / std::max(1u, w - 1);
        const unsigned g = y * 255 / std::max(1u, h - 1);
        const unsigned b = 255 - (x + y) * 255 / std::max(1u, w + h - 2);
        return argb(255, r, g, b);
    };
}

Fill Texels::alpha_rings() {
    return [](UINT x, UINT y, UINT w, UINT h) {
        const float dx = (static_cast<float>(x) + 0.5f) / static_cast<float>(w) - 0.5f;
        const float dy = (static_cast<float>(y) + 0.5f) / static_cast<float>(h) - 0.5f;
        const float r = std::sqrt(dx * dx + dy * dy) * 2.0f;
        const unsigned a = static_cast<unsigned>(std::clamp(1.0f - r, 0.0f, 1.0f) * 255.0f);
        return argb(a, 255 - x * 200 / std::max(1u, w), 80 + y * 170 / std::max(1u, h), 90);
    };
}

Fill Texels::solid(std::uint32_t value) {
    return [=](UINT, UINT, UINT, UINT) { return value; };
}

std::uint16_t Texels::half(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, 4);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = static_cast<int>((bits >> 23) & 0xFF) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0) {
        return static_cast<std::uint16_t>(sign);
    }
    if (exponent >= 31) {
        return static_cast<std::uint16_t>(sign | 0x7C00u);
    }
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
}

UINT Texels::bytes_per_texel(D3DFORMAT f) {
    switch (f) {
        case D3DFMT_A8:
        case D3DFMT_L8:
        case D3DFMT_P8:
            return 1;
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_A8L8:
        case D3DFMT_L16:
        case D3DFMT_R16F:
        case D3DFMT_V8U8:
            return 2;
        case D3DFMT_A16B16G16R16:
        case D3DFMT_A16B16G16R16F:
        case D3DFMT_G32R32F:
            return 8;
        case D3DFMT_A32B32G32R32F:
            return 16;
        default:
            return 4;
    }
}

constexpr D3DFORMAT kAti1 = static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'I', '1'));
constexpr D3DFORMAT kAti2 = static_cast<D3DFORMAT>(MAKEFOURCC('A', 'T', 'I', '2'));

bool Texels::is_block(D3DFORMAT f) {
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5 || f == kAti1 || f == kAti2;
}

UINT Texels::block_bytes(D3DFORMAT f) { return (f == D3DFMT_DXT1 || f == kAti1) ? 8 : 16; }

namespace {

void bc4_block(const std::uint8_t v[16], std::uint8_t* dst) {
    unsigned amin = 255;
    unsigned amax = 0;
    for (int i = 0; i < 16; ++i) {
        amin = std::min<unsigned>(amin, v[i]);
        amax = std::max<unsigned>(amax, v[i]);
    }
    dst[0] = static_cast<std::uint8_t>(amax);
    dst[1] = static_cast<std::uint8_t>(amin);
    float pal[8];
    pal[0] = static_cast<float>(amax);
    pal[1] = static_cast<float>(amin);
    if (amax > amin) {
        for (int k = 1; k < 7; ++k) {
            pal[1 + k] = (static_cast<float>(7 - k) * pal[0] + static_cast<float>(k) * pal[1]) / 7.0f;
        }
    } else {
        for (int k = 2; k < 8; ++k) {
            pal[k] = pal[0];
        }
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        float best_d = 1e9f;
        for (int q = 0; q < 8; ++q) {
            const float dist = std::fabs(static_cast<float>(v[i]) - pal[q]);
            if (dist < best_d) {
                best_d = dist;
                best = q;
            }
        }
        bits |= static_cast<std::uint64_t>(best) << (3 * i);
    }
    for (int i = 0; i < 6; ++i) {
        dst[2 + i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
}

}  // namespace

bool Texels::pack(D3DFORMAT f, std::uint32_t c, std::uint8_t* dst) {
    const unsigned a = c >> 24;
    const unsigned r = (c >> 16) & 255;
    const unsigned g = (c >> 8) & 255;
    const unsigned b = c & 255;
    const auto put16 = [&](unsigned v) { std::memcpy(dst, &v, 2); };
    const auto put32 = [&](std::uint32_t v) { std::memcpy(dst, &v, 4); };
    const auto putf = [&](float v, int i) { std::memcpy(dst + i * 4, &v, 4); };
    const auto puth = [&](float v, int i) {
        const std::uint16_t h = half(v);
        std::memcpy(dst + i * 2, &h, 2);
    };
    const float fr = static_cast<float>(r) / 255.0f;
    const float fg = static_cast<float>(g) / 255.0f;
    const float fb = static_cast<float>(b) / 255.0f;
    const float fa = static_cast<float>(a) / 255.0f;
    switch (f) {
        case D3DFMT_A8R8G8B8:
            put32(c);
            return true;
        case D3DFMT_X8R8G8B8:
            put32(c | 0xFF000000u);
            return true;
        case D3DFMT_A8B8G8R8:
            put32((a << 24) | (b << 16) | (g << 8) | r);
            return true;
        case D3DFMT_X8B8G8R8:
            put32(0xFF000000u | (b << 16) | (g << 8) | r);
            return true;
        case D3DFMT_R5G6B5:
            put16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            return true;
        case D3DFMT_X1R5G5B5:
            put16(0x8000u | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
            return true;
        case D3DFMT_A1R5G5B5:
            put16(((a >> 7) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
            return true;
        case D3DFMT_A4R4G4B4:
            put16(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
            return true;
        case D3DFMT_A8:
            dst[0] = static_cast<std::uint8_t>(a);
            return true;
        case D3DFMT_L8:
            dst[0] = static_cast<std::uint8_t>(r);
            return true;
        case D3DFMT_A8L8:
            put16((a << 8) | r);
            return true;
        case D3DFMT_L16:
            put16(r * 257);
            return true;
        case D3DFMT_A2R10G10B10:
            put32(((a >> 6) << 30) | ((r * 1023 / 255) << 20) | ((g * 1023 / 255) << 10) | (b * 1023 / 255));
            return true;
        case D3DFMT_A2B10G10R10:
            put32(((a >> 6) << 30) | ((b * 1023 / 255) << 20) | ((g * 1023 / 255) << 10) | (r * 1023 / 255));
            return true;
        case D3DFMT_A16B16G16R16: {
            const std::uint16_t v[4] = {static_cast<std::uint16_t>(r * 257), static_cast<std::uint16_t>(g * 257), static_cast<std::uint16_t>(b * 257),
                                        static_cast<std::uint16_t>(a * 257)};
            std::memcpy(dst, v, 8);
            return true;
        }
        case D3DFMT_G16R16: {
            const std::uint16_t v[2] = {static_cast<std::uint16_t>(r * 257), static_cast<std::uint16_t>(g * 257)};
            std::memcpy(dst, v, 4);
            return true;
        }
        case D3DFMT_R16F:
            puth(fr, 0);
            return true;
        case D3DFMT_G16R16F:
            puth(fr, 0);
            puth(fg, 1);
            return true;
        case D3DFMT_A16B16G16R16F:
            puth(fr, 0);
            puth(fg, 1);
            puth(fb, 2);
            puth(fa, 3);
            return true;
        case D3DFMT_R32F:
            putf(fr, 0);
            return true;
        case D3DFMT_G32R32F:
            putf(fr, 0);
            putf(fg, 1);
            return true;
        case D3DFMT_A32B32G32R32F:
            putf(fr, 0);
            putf(fg, 1);
            putf(fb, 2);
            putf(fa, 3);
            return true;
        case D3DFMT_V8U8:
            dst[0] = static_cast<std::uint8_t>(static_cast<int>(r) - 128);
            dst[1] = static_cast<std::uint8_t>(static_cast<int>(g) - 128);
            return true;
        case D3DFMT_X8L8V8U8:
            dst[0] = static_cast<std::uint8_t>(static_cast<int>(r) - 128);
            dst[1] = static_cast<std::uint8_t>(static_cast<int>(g) - 128);
            dst[2] = static_cast<std::uint8_t>(b);
            dst[3] = 0;
            return true;
        case D3DFMT_Q8W8V8U8:
            dst[0] = static_cast<std::uint8_t>(static_cast<int>(r) - 128);
            dst[1] = static_cast<std::uint8_t>(static_cast<int>(g) - 128);
            dst[2] = static_cast<std::uint8_t>(static_cast<int>(b) - 128);
            dst[3] = static_cast<std::uint8_t>(static_cast<int>(a) - 128);
            return true;
        default:
            return false;
    }
}

void Texels::encode_block(D3DFORMAT f, const std::uint32_t t[16], std::uint8_t* dst) {
    if (f == kAti1 || f == kAti2) {
        std::uint8_t r[16];
        std::uint8_t g[16];
        for (int i = 0; i < 16; ++i) {
            r[i] = static_cast<std::uint8_t>((t[i] >> 16) & 255);
            g[i] = static_cast<std::uint8_t>((t[i] >> 8) & 255);
        }
        bc4_block(r, dst);
        if (f == kAti2) {
            bc4_block(g, dst + 8);
        }
        return;
    }
    const auto luma = [](std::uint32_t c) { return ((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255); };
    std::uint32_t lo = t[0];
    std::uint32_t hi = t[0];
    for (int i = 1; i < 16; ++i) {
        if (luma(t[i]) < luma(lo)) {
            lo = t[i];
        }
        if (luma(t[i]) > luma(hi)) {
            hi = t[i];
        }
    }
    const auto to565 = [](std::uint32_t c) {
        return static_cast<std::uint16_t>((((c >> 16) & 255) >> 3 << 11) | (((c >> 8) & 255) >> 2 << 5) | ((c & 255) >> 3));
    };
    std::uint16_t c0 = to565(hi);
    std::uint16_t c1 = to565(lo);
    if (c0 < c1) {
        std::swap(c0, c1);
    }
    const auto from565 = [](std::uint16_t v, int out[3]) {
        out[0] = ((v >> 11) & 31) * 255 / 31;
        out[1] = ((v >> 5) & 63) * 255 / 63;
        out[2] = (v & 31) * 255 / 31;
    };
    int p[4][3];
    from565(c0, p[0]);
    from565(c1, p[1]);
    for (int k = 0; k < 3; ++k) {
        p[2][k] = (2 * p[0][k] + p[1][k]) / 3;
        p[3][k] = (p[0][k] + 2 * p[1][k]) / 3;
    }
    std::uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0;
        int best_d = 1 << 30;
        for (int q = 0; q < 4; ++q) {
            const int dr = static_cast<int>((t[i] >> 16) & 255) - p[q][0];
            const int dg = static_cast<int>((t[i] >> 8) & 255) - p[q][1];
            const int db = static_cast<int>(t[i] & 255) - p[q][2];
            const int dist = dr * dr + dg * dg + db * db;
            if (dist < best_d) {
                best_d = dist;
                best = q;
            }
        }
        indices |= static_cast<std::uint32_t>(best) << (2 * i);
    }
    std::uint8_t* color = dst;
    if (f == D3DFMT_DXT1) {
        if (c0 == c1) {
            indices = 0;
        }
    } else if (f == D3DFMT_DXT3 || f == D3DFMT_DXT2) {
        for (int i = 0; i < 8; ++i) {
            dst[i] = static_cast<std::uint8_t>(((t[i * 2] >> 28) & 15) | (((t[i * 2 + 1] >> 28) & 15) << 4));
        }
        color = dst + 8;
    } else {
        unsigned amin = 255;
        unsigned amax = 0;
        for (int i = 0; i < 16; ++i) {
            amin = std::min(amin, t[i] >> 24);
            amax = std::max(amax, t[i] >> 24);
        }
        dst[0] = static_cast<std::uint8_t>(amax);
        dst[1] = static_cast<std::uint8_t>(amin);
        float pal[8];
        pal[0] = static_cast<float>(amax);
        pal[1] = static_cast<float>(amin);
        if (amax > amin) {
            for (int k = 1; k < 7; ++k) {
                pal[1 + k] = (static_cast<float>(7 - k) * pal[0] + static_cast<float>(k) * pal[1]) / 7.0f;
            }
        } else {
            for (int k = 2; k < 8; ++k) {
                pal[k] = pal[0];
            }
        }
        std::uint64_t bits = 0;
        for (int i = 0; i < 16; ++i) {
            int best = 0;
            float best_d = 1e9f;
            for (int q = 0; q < 8; ++q) {
                const float dist = std::fabs(static_cast<float>(t[i] >> 24) - pal[q]);
                if (dist < best_d) {
                    best_d = dist;
                    best = q;
                }
            }
            bits |= static_cast<std::uint64_t>(best) << (3 * i);
        }
        for (int i = 0; i < 6; ++i) {
            dst[2 + i] = static_cast<std::uint8_t>(bits >> (8 * i));
        }
        color = dst + 8;
    }
    std::memcpy(color, &c0, 2);
    std::memcpy(color + 2, &c1, 2);
    std::memcpy(color + 4, &indices, 4);
}

void Ctx::material(const D3DMATERIAL9& value) {
    chk(d->SetMaterial(&value), "SetMaterial");
    rs(D3DRS_COLORVERTEX, FALSE);
    rs(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_MATERIAL);
    rs(D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL);
    rs(D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL);
    rs(D3DRS_SPECULARMATERIALSOURCE, D3DMCS_MATERIAL);
}

bool Ctx::open(UINT extent, D3DFORMAT format, D3DFORMAT depth_format, bool use_back) {
    size = extent;
    if (use_back) {
        chk(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, target.surface.put()), "GetBackBuffer");
        D3DSURFACE_DESC desc{};
        if (target.surface) {
            target.surface->GetDesc(&desc);
        }
        target.format = desc.Format;
        target.width = desc.Width;
        target.height = desc.Height;
        if (desc.Width != extent || desc.Height != extent) {
            fail("the back buffer is not the scene size");
        }
    } else if (!kit.make_target(target, extent, extent, format, error)) {
        good = false;
        return false;
    }
    if (depth_format != D3DFMT_UNKNOWN) {
        const HRESULT result = d->CreateDepthStencilSurface(extent, extent, depth_format, D3DMULTISAMPLE_NONE, 0, FALSE, depth.surface.put(), nullptr);
        if (FAILED(result)) {
            error = "CreateDepthStencilSurface: " + D3d9Tools::hr(result);
            good = false;
            return false;
        }
        depth.format = depth_format;
    }
    chk(d->SetRenderTarget(0, target.surface.get()), "SetRenderTarget");
    chk(d->SetDepthStencilSurface(depth.surface.get()), "SetDepthStencilSurface");
    base_state();
    return good;
}

void Ctx::fail(const std::string& message) {
    if (good) {
        error = message;
    }
    good = false;
}

void Ctx::chk(HRESULT result, const char* what) {
    if (FAILED(result)) {
        fail(std::string(what) + ": " + D3d9Tools::hr(result));
    }
}

void Ctx::rsf(D3DRENDERSTATETYPE type, float value) { rs(type, Dx::fbits(value)); }

void Ctx::begin() {
    if (!in_scene) {
        chk(d->BeginScene(), "BeginScene");
        in_scene = true;
    }
}

void Ctx::end() {
    if (in_scene) {
        chk(d->EndScene(), "EndScene");
        in_scene = false;
    }
}

void Ctx::clear(D3DCOLOR color, DWORD flags, float z, DWORD stencil) {
    Com<IDirect3DSurface9> bound;
    d->GetDepthStencilSurface(bound.put());
    if (!bound) {
        flags &= ~(D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL);
    } else {
        D3DSURFACE_DESC desc{};
        bound->GetDesc(&desc);
        if (desc.Format != D3DFMT_D24S8 && desc.Format != D3DFMT_D24X4S4 && desc.Format != D3DFMT_D15S1) {
            flags &= ~D3DCLEAR_STENCIL;
        }
    }
    chk(d->Clear(0, nullptr, flags, color, z, stencil), "Clear");
}

void Ctx::snap(const std::string& label) { snap_surface(target, label); }

void Ctx::snap_surface(Surface& surface, const std::string& label) {
    const bool was = in_scene;
    end();
    rr::Image image;
    std::string problem;
    if (!kit.read_color(surface, image, problem)) {
        fail("readback: " + problem);
    } else {
        pending.add(label, std::move(image));
    }
    if (was) {
        begin();
    }
}

void Ctx::snap_on(IDirect3DDevice9* source, Surface& surface, const std::string& label) {
    rr::Image image;
    std::string problem;
    if (!kit.read_color(source, surface, image, problem)) {
        fail("readback: " + problem);
    } else {
        pending.add(label, std::move(image));
    }
}

void Ctx::base_state() {
    rs(D3DRS_LIGHTING, FALSE);
    rs(D3DRS_CULLMODE, D3DCULL_NONE);
    rs(D3DRS_ZENABLE, D3DZB_FALSE);
    rs(D3DRS_ALPHABLENDENABLE, FALSE);
}

void Ctx::camera(float distance, float fovy_degrees) {
    xf(D3DTS_WORLD, Xf());
    xf(D3DTS_VIEW, Xf::translate(0, 0, distance));
    xf(D3DTS_PROJECTION, Xf::perspective(fovy_degrees * 3.14159265f / 180.0f, 1.0f, 0.5f, 40.0f));
}

void Ctx::draw(D3DPRIMITIVETYPE type, UINT primitives, const void* vertices, UINT stride) {
    chk(d->DrawPrimitiveUP(type, primitives, vertices, stride), "DrawPrimitiveUP");
}

void Ctx::draw_mesh(const Mesh& mesh) {
    chk(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, static_cast<UINT>(mesh.vertices.size()), mesh.triangles(), mesh.indices.data(), D3DFMT_INDEX16,
                                  mesh.vertices.data(), sizeof(Vtx)),
        "DrawIndexedPrimitiveUP");
}

void Ctx::quad(float x0, float y0, float x1, float y1, float z, DWORD tl, DWORD tr, DWORD bl, DWORD br) {
    struct V {
        float x, y, z, rhw;
        DWORD color;
    };
    const V v[4] = {{x0, y0, z, 1.0f, tl}, {x1, y0, z, 1.0f, tr}, {x0, y1, z, 1.0f, bl}, {x1, y1, z, 1.0f, br}};
    fvf(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    draw(D3DPT_TRIANGLESTRIP, 2, v, sizeof(V));
}

void Ctx::draw_idx(D3DPRIMITIVETYPE type, UINT vertex_count, UINT primitives, const void* indices, D3DFORMAT index_format, const void* vertices,
                   UINT stride) {
    chk(d->DrawIndexedPrimitiveUP(type, 0, vertex_count, primitives, indices, index_format, vertices, stride), "DrawIndexedPrimitiveUP");
}

void Ctx::light(DWORD index, const D3DLIGHT9& value, bool enable) {
    chk(d->SetLight(index, &value), "SetLight");
    chk(d->LightEnable(index, enable ? TRUE : FALSE), "LightEnable");
}

bool Ctx::fill_level(IDirect3DTexture9* texture, UINT level, D3DFORMAT format, const Fill& fill) {
    D3DSURFACE_DESC desc{};
    texture->GetLevelDesc(level, &desc);
    D3DLOCKED_RECT locked{};
    const HRESULT result = texture->LockRect(level, &locked, nullptr, (desc.Usage & D3DUSAGE_DYNAMIC) ? D3DLOCK_DISCARD : 0);
    if (FAILED(result)) {
        fail("LockRect: " + D3d9Tools::hr(result));
        return false;
    }
    if (Texels::is_block(format)) {
        const UINT bw = (desc.Width + 3) / 4;
        const UINT bh = (desc.Height + 3) / 4;
        const UINT block_bytes = Texels::block_bytes(format);
        for (UINT by = 0; by < bh; ++by) {
            for (UINT bx = 0; bx < bw; ++bx) {
                std::uint32_t t[16];
                for (UINT k = 0; k < 16; ++k) {
                    t[k] = fill(std::min(bx * 4 + (k & 3), desc.Width - 1), std::min(by * 4 + (k >> 2), desc.Height - 1), desc.Width, desc.Height);
                }
                Texels::encode_block(format, t, static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(by) * locked.Pitch + bx * block_bytes);
            }
        }
    } else {
        const UINT bpt = Texels::bytes_per_texel(format);
        for (UINT y = 0; y < desc.Height; ++y) {
            std::uint8_t* row = static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * locked.Pitch;
            for (UINT x = 0; x < desc.Width; ++x) {
                if (!Texels::pack(format, fill(x, y, desc.Width, desc.Height), row + x * bpt)) {
                    texture->UnlockRect(level);
                    fail("no texel packer for the format");
                    return false;
                }
            }
        }
    }
    texture->UnlockRect(level);
    return true;
}

Com<IDirect3DTexture9> Ctx::texture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool, const Fill& fill) {
    const bool staged = pool == D3DPOOL_DEFAULT && (usage & (D3DUSAGE_DYNAMIC | D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) == 0;
    Com<IDirect3DTexture9> out;
    HRESULT result = d->CreateTexture(width, height, levels, usage, format, staged ? D3DPOOL_SYSTEMMEM : pool, out.put(), nullptr);
    if (FAILED(result)) {
        fail("CreateTexture: " + D3d9Tools::hr(result));
        return {};
    }
    if (!fill) {
        return out;
    }
    const UINT count = (usage & D3DUSAGE_AUTOGENMIPMAP) ? 1 : out->GetLevelCount();
    for (UINT level = 0; level < count; ++level) {
        if (!fill_level(out.get(), level, format, fill)) {
            return {};
        }
    }
    if (staged) {
        Com<IDirect3DTexture9> device_copy;
        result = d->CreateTexture(width, height, levels, usage, format, D3DPOOL_DEFAULT, device_copy.put(), nullptr);
        if (FAILED(result)) {
            fail("CreateTexture default: " + D3d9Tools::hr(result));
            return {};
        }
        chk(d->UpdateTexture(out.get(), device_copy.get()), "UpdateTexture");
        return device_copy;
    }
    if (usage & D3DUSAGE_AUTOGENMIPMAP) {
        out->GenerateMipSubLevels();
    }
    return out;
}

Com<IDirect3DVertexShader9> Ctx::vs(const std::string& source, const char* profile) {
    std::string problem;
    Com<IDirect3DVertexShader9> out = D3d9Tools::vs(d, source, profile, problem);
    if (!out) {
        fail("vertex shader " + std::string(profile) + ": " + problem);
    }
    return out;
}

Com<IDirect3DPixelShader9> Ctx::ps(const std::string& source, const char* profile) {
    std::string problem;
    Com<IDirect3DPixelShader9> out = D3d9Tools::ps(d, source, profile, problem);
    if (!out) {
        fail("pixel shader " + std::string(profile) + ": " + problem);
    }
    return out;
}

Com<IDirect3DVertexBuffer9> Ctx::vbuffer(const void* data, UINT bytes, DWORD usage, D3DPOOL pool) {
    Com<IDirect3DVertexBuffer9> out;
    chk(d->CreateVertexBuffer(bytes, usage, 0, pool, out.put(), nullptr), "CreateVertexBuffer");
    void* mapped = nullptr;
    if (out && data != nullptr) {
        chk(out->Lock(0, bytes, &mapped, 0), "Lock");
        if (mapped != nullptr) {
            std::memcpy(mapped, data, bytes);
            out->Unlock();
        }
    }
    return out;
}

Com<IDirect3DIndexBuffer9> Ctx::ibuffer(const void* data, UINT bytes, D3DFORMAT format) {
    Com<IDirect3DIndexBuffer9> out;
    chk(d->CreateIndexBuffer(bytes, D3DUSAGE_WRITEONLY, format, D3DPOOL_DEFAULT, out.put(), nullptr), "CreateIndexBuffer");
    void* mapped = nullptr;
    if (out && data != nullptr) {
        chk(out->Lock(0, bytes, &mapped, 0), "Lock");
        if (mapped != nullptr) {
            std::memcpy(mapped, data, bytes);
            out->Unlock();
        }
    }
    return out;
}

Com<IDirect3DVertexDeclaration9> Ctx::decl(const D3DVERTEXELEMENT9* elements) {
    Com<IDirect3DVertexDeclaration9> out;
    chk(d->CreateVertexDeclaration(elements, out.put()), "CreateVertexDeclaration");
    return out;
}

bool Ctx::format_ok(DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format) const {
    Com<IDirect3D9> factory;
    d->GetDirect3D(factory.put());
    D3DDEVICE_CREATION_PARAMETERS params{};
    d->GetCreationParameters(&params);
    return factory && SUCCEEDED(factory->CheckDeviceFormat(params.AdapterOrdinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, type, format));
}

D3DCAPS9 Ctx::caps() const {
    D3DCAPS9 out{};
    d->GetDeviceCaps(&out);
    return out;
}

bool RttOps::make(Ctx& c, Rtt& out, UINT width, UINT height, D3DFORMAT format, bool with_depth) {
    c.chk(c.d->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, out.tex.put(), nullptr), "CreateTexture render target");
    if (!out.tex) {
        return false;
    }
    out.tex->GetSurfaceLevel(0, out.color.put());
    if (with_depth) {
        c.chk(c.d->CreateDepthStencilSurface(width, height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, out.depth.put(), nullptr), "CreateDepthStencilSurface");
    }
    out.width = width;
    out.height = height;
    return c.good;
}

void RttOps::push(Ctx& c, Rtt& target) {
    c.chk(c.d->SetRenderTarget(0, target.color.get()), "SetRenderTarget");
    c.chk(c.d->SetDepthStencilSurface(target.depth.get()), "SetDepthStencilSurface");
}

void RttOps::pop(Ctx& c) {
    c.chk(c.d->SetRenderTarget(0, c.target.surface.get()), "SetRenderTarget");
    c.chk(c.d->SetDepthStencilSurface(c.depth.surface.get()), "SetDepthStencilSurface");
}

namespace {

class FnScene : public D3d9Scene {
public:
    FnScene(FrameBody body, Layout layout, Need need) : body_(std::move(body)), layout_(layout), need_(std::move(need)) {}

    std::string unsupported(Kit& kit) override { return need_ ? need_(kit.device()) : std::string(); }

    bool setup(Kit& kit, std::string& error) override {
        ctx_ = std::make_unique<Ctx>(kit);
        if (!ctx_->open(layout_.extent, layout_.format, layout_.depth, layout_.back)) {
            error = ctx_->error;
            return false;
        }
        ctx_->clear(0xFF000000);
        return report(error);
    }

    bool frame(Kit&, unsigned index, std::string& error) override {
        ctx_->pending = rr::CaptureSet();
        ctx_->begin();
        body_(*ctx_, index);
        ctx_->end();
        return report(error);
    }

    bool capture(Kit& kit, unsigned, rr::CaptureSet& out, std::string& error) override {
        if (ctx_->pending.items().empty()) {
            ctx_->snap("");
        }
        if (!report(error)) {
            return false;
        }
        out = std::move(ctx_->pending);
        ctx_->pending = rr::CaptureSet();
        return true;
    }

private:
    bool report(std::string& error) {
        if (!ctx_->good) {
            error = ctx_->error;
        }
        return ctx_->good;
    }

    FrameBody body_;
    Layout layout_;
    Need need_;
    std::unique_ptr<Ctx> ctx_;
};

}  // namespace

void Cases::single(rr::CaseSpec spec, Body body, Layout layout, Need need) {
    rr::CaseTable<Kit>::instance().add(std::move(spec), [body = std::move(body), layout, need] {
        return std::unique_ptr<rr::Scene<Kit>>(new FnScene([body](Ctx& ctx, unsigned) { body(ctx); }, layout, need));
    });
}

void Cases::frames(rr::CaseSpec spec, FrameMaker maker, Layout layout, Need need) {
    rr::CaseTable<Kit>::instance().add(std::move(spec), [maker = std::move(maker), layout, need] {
        return std::unique_ptr<rr::Scene<Kit>>(new FnScene(maker(), layout, need));
    });
}

Need Needs::caps(std::function<bool(const D3DCAPS9&)> test, std::string what) {
    return [test = std::move(test), what = std::move(what)](IDirect3DDevice9* device) {
        D3DCAPS9 c{};
        device->GetDeviceCaps(&c);
        return test(c) ? std::string() : what;
    };
}

Need Needs::format(DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format, std::string what) {
    return [=](IDirect3DDevice9* device) {
        Com<IDirect3D9> factory;
        device->GetDirect3D(factory.put());
        D3DDEVICE_CREATION_PARAMETERS params{};
        device->GetCreationParameters(&params);
        const bool ok = factory && SUCCEEDED(factory->CheckDeviceFormat(params.AdapterOrdinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, type, format));
        return ok ? std::string() : what;
    };
}

Need Needs::all(std::vector<Need> parts) {
    return [parts = std::move(parts)](IDirect3DDevice9* device) {
        for (const Need& part : parts) {
            if (!part) {
                continue;
            }
            const std::string why = part(device);
            if (!why.empty()) {
                return why;
            }
        }
        return std::string();
    };
}

Need Needs::shader(unsigned vertex_major, unsigned pixel_major) {
    return caps(
        [=](const D3DCAPS9& c) {
            return ((c.VertexShaderVersion >> 8) & 0xFF) >= vertex_major && ((c.PixelShaderVersion >> 8) & 0xFF) >= pixel_major;
        },
        "shader model " + std::to_string(vertex_major) + "/" + std::to_string(pixel_major));
}

D3DLIGHT9 Dx::directional(float dx, float dy, float dz, float r, float g, float b) {
    D3DLIGHT9 l{};
    l.Type = D3DLIGHT_DIRECTIONAL;
    l.Direction = {dx, dy, dz};
    l.Diffuse = {r, g, b, 1};
    l.Specular = {r, g, b, 1};
    return l;
}

D3DLIGHT9 Dx::point(float x, float y, float z, float range, float a0, float a1, float a2, float r, float g, float b) {
    D3DLIGHT9 l{};
    l.Type = D3DLIGHT_POINT;
    l.Position = {x, y, z};
    l.Range = range;
    l.Attenuation0 = a0;
    l.Attenuation1 = a1;
    l.Attenuation2 = a2;
    l.Diffuse = {r, g, b, 1};
    l.Specular = {r, g, b, 1};
    return l;
}

D3DLIGHT9 Dx::spot(float x, float y, float z, float dx, float dy, float dz, float range, float falloff, float theta, float phi, float r, float g, float b) {
    D3DLIGHT9 l{};
    l.Type = D3DLIGHT_SPOT;
    l.Position = {x, y, z};
    l.Direction = {dx, dy, dz};
    l.Range = range;
    l.Falloff = falloff;
    l.Theta = theta;
    l.Phi = phi;
    l.Attenuation0 = 1.0f;
    l.Diffuse = {r, g, b, 1};
    l.Specular = {r, g, b, 1};
    return l;
}

D3DMATERIAL9 Dx::material(float dr, float dg, float db, float ar, float ag, float ab, float er, float eg, float eb, float sr, float sg, float sb, float power) {
    D3DMATERIAL9 m{};
    m.Diffuse = {dr, dg, db, 1};
    m.Ambient = {ar, ag, ab, 1};
    m.Emissive = {er, eg, eb, 1};
    m.Specular = {sr, sg, sb, 1};
    m.Power = power;
    return m;
}

}  // namespace lsw::suites::render::d3d9
