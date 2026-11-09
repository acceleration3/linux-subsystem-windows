#include "render/gl/toolkit.hpp"

#include <cmath>

namespace lsw::render::gl {

void Cases::single(CaseSpec spec, SingleBody body, NeedFn need) {
    spec.source_hash(LSW_RENDER_SOURCE_HASH);
    CaseTable<Kit>::instance().add(std::move(spec), [body = std::move(body), need = std::move(need)] {
        Fns fns;
        fns.unsupported = need;
        fns.capture = [body](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return body(kit, out, error); };
        return std::unique_ptr<GlScene>(new FnScene(std::move(fns)));
    });
}

void Cases::multi(CaseSpec spec, std::function<Fns()> factory) {
    spec.source_hash(LSW_RENDER_SOURCE_HASH);
    CaseTable<Kit>::instance().add(std::move(spec), [factory = std::move(factory)] { return std::unique_ptr<GlScene>(new FnScene(factory())); });
}

namespace {
void push(Shape& s, float px, float py, float pz, float nx, float ny, float nz, float u, float v) {
    const float values[8] = {px, py, pz, nx, ny, nz, u, v};
    s.vertices.insert(s.vertices.end(), values, values + 8);
}
}  // namespace

Shape Shape::sphere(unsigned rings, unsigned segments, float radius) {
    Shape s;
    for (unsigned r = 0; r <= rings; ++r) {
        const float phi = 3.14159265f * static_cast<float>(r) / static_cast<float>(rings);
        for (unsigned g = 0; g <= segments; ++g) {
            const float theta = 6.2831853f * static_cast<float>(g) / static_cast<float>(segments);
            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::cos(phi);
            const float z = std::sin(phi) * std::sin(theta);
            push(s, x * radius, y * radius, z * radius, x, y, z, static_cast<float>(g) / static_cast<float>(segments),
                 static_cast<float>(r) / static_cast<float>(rings));
        }
    }
    for (unsigned r = 0; r < rings; ++r) {
        for (unsigned g = 0; g < segments; ++g) {
            const std::uint32_t a = r * (segments + 1) + g;
            const std::uint32_t b = a + segments + 1;
            s.indices.insert(s.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return s;
}

Shape Shape::torus(unsigned rings, unsigned segments, float major, float minor) {
    Shape s;
    for (unsigned r = 0; r <= rings; ++r) {
        const float u = 6.2831853f * static_cast<float>(r) / static_cast<float>(rings);
        for (unsigned g = 0; g <= segments; ++g) {
            const float v = 6.2831853f * static_cast<float>(g) / static_cast<float>(segments);
            const float nx = std::cos(v) * std::cos(u);
            const float ny = std::sin(v);
            const float nz = std::cos(v) * std::sin(u);
            push(s, (major + minor * std::cos(v)) * std::cos(u), minor * ny, (major + minor * std::cos(v)) * std::sin(u), nx, ny, nz,
                 static_cast<float>(r) / static_cast<float>(rings) * 4.0f, static_cast<float>(g) / static_cast<float>(segments));
        }
    }
    for (unsigned r = 0; r < rings; ++r) {
        for (unsigned g = 0; g < segments; ++g) {
            const std::uint32_t a = r * (segments + 1) + g;
            const std::uint32_t b = a + segments + 1;
            s.indices.insert(s.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
    }
    return s;
}

Shape Shape::cube(float h) {
    Shape s;
    const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const float t[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    for (unsigned f = 0; f < 6; ++f) {
        const float* nn = n[f];
        const float* tt = t[f];
        const float bb[3] = {nn[1] * tt[2] - nn[2] * tt[1], nn[2] * tt[0] - nn[0] * tt[2], nn[0] * tt[1] - nn[1] * tt[0]};
        const float us[4] = {-1, 1, 1, -1};
        const float vs[4] = {-1, -1, 1, 1};
        const std::uint32_t base = static_cast<std::uint32_t>(s.vertices.size() / 8);
        for (unsigned k = 0; k < 4; ++k) {
            push(s, h * (nn[0] + us[k] * tt[0] + vs[k] * bb[0]), h * (nn[1] + us[k] * tt[1] + vs[k] * bb[1]),
                 h * (nn[2] + us[k] * tt[2] + vs[k] * bb[2]), nn[0], nn[1], nn[2], (us[k] + 1) * 0.5f, (vs[k] + 1) * 0.5f);
        }
        s.indices.insert(s.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return s;
}

Shape Shape::quad(float x0, float y0, float x1, float y1, float z) {
    Shape s;
    push(s, x0, y0, z, 0, 0, 1, 0, 0);
    push(s, x1, y0, z, 0, 0, 1, 1, 0);
    push(s, x1, y1, z, 0, 0, 1, 1, 1);
    push(s, x0, y1, z, 0, 0, 1, 0, 1);
    s.indices = {0, 1, 2, 0, 2, 3};
    return s;
}

Shape Shape::grid(unsigned cells, float half, float z) {
    Shape s;
    for (unsigned y = 0; y <= cells; ++y) {
        for (unsigned x = 0; x <= cells; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(cells);
            const float v = static_cast<float>(y) / static_cast<float>(cells);
            push(s, (u * 2 - 1) * half, (v * 2 - 1) * half, z, 0, 0, 1, u, v);
        }
    }
    for (unsigned y = 0; y < cells; ++y) {
        for (unsigned x = 0; x < cells; ++x) {
            const std::uint32_t a = y * (cells + 1) + x;
            const std::uint32_t b = a + cells + 1;
            s.indices.insert(s.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
        }
    }
    return s;
}

Shape Shape::cone(unsigned segments, float radius, float height) {
    Shape s;
    push(s, 0, height, 0, 0, 1, 0, 0.5f, 1);
    for (unsigned g = 0; g <= segments; ++g) {
        const float theta = 6.2831853f * static_cast<float>(g) / static_cast<float>(segments);
        const float x = std::cos(theta);
        const float z = std::sin(theta);
        const float len = std::sqrt(height * height + radius * radius);
        push(s, x * radius, 0, z * radius, x * height / len, radius / len, z * height / len, static_cast<float>(g) / static_cast<float>(segments), 0);
    }
    for (unsigned g = 0; g < segments; ++g) {
        s.indices.insert(s.indices.end(), {0, g + 2, g + 1});
    }
    return s;
}

GpuShape Gx::upload(Kit& kit, const Shape& shape) {
    GpuShape out;
    Api& gl = kit.gl;
    out.vao = kit.vertex_array();
    out.vbo = kit.buffer(glc::ARRAY_BUFFER, shape.vertices.data(), shape.vertices.size() * sizeof(float), glc::STATIC_DRAW);
    gl.glVertexAttribPointer(0, 3, glc::FLOAT, glc::FALSE_, kStride, nullptr);
    gl.glEnableVertexAttribArray(0);
    gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, kStride, reinterpret_cast<const void*>(12));
    gl.glEnableVertexAttribArray(1);
    gl.glVertexAttribPointer(2, 2, glc::FLOAT, glc::FALSE_, kStride, reinterpret_cast<const void*>(24));
    gl.glEnableVertexAttribArray(2);
    if (!shape.indices.empty()) {
        out.ibo = kit.buffer(glc::ELEMENT_ARRAY_BUFFER, shape.indices.data(), shape.indices.size() * sizeof(std::uint32_t), glc::STATIC_DRAW);
    }
    out.count = static_cast<GLsizei>(shape.indices.size());
    out.vertex_count = static_cast<GLsizei>(shape.vertices.size() / 8);
    return out;
}

void GpuShape::draw(Kit& kit, GLenum mode) const {
    kit.gl.glBindVertexArray(vao);
    if (ibo != 0) {
        kit.gl.glDrawElements(mode, count, glc::UNSIGNED_INT, nullptr);
    } else {
        kit.gl.glDrawArrays(mode, 0, vertex_count);
    }
}

void GpuShape::draw_instanced(Kit& kit, GLsizei instances, GLenum mode) const {
    kit.gl.glBindVertexArray(vao);
    if (ibo != 0) {
        kit.gl.glDrawElementsInstanced(mode, count, glc::UNSIGNED_INT, nullptr, instances);
    } else {
        kit.gl.glDrawArraysInstanced(mode, 0, vertex_count, instances);
    }
}

GLuint Gx::make_texture(Kit& kit, unsigned size, unsigned kind, bool mips) {
    Pattern::Bytes base;
    switch (kind) {
        case 0: base = Pattern::checker(size, size, size / 8, Pattern::pack(240, 230, 210), Pattern::pack(200, 60, 50)); break;
        case 1: base = Pattern::gradient(size, size); break;
        case 2: base = Pattern::noise(size, size, 7); break;
        default: base = Pattern::checker(size, size, size / 4, Pattern::pack(40, 160, 220), Pattern::pack(250, 220, 40)); break;
    }
    const unsigned levels = mips ? Pattern::level_count(size, size) : 1;
    const GLuint texture = kit.texture2d(size, size, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, Pattern::mip_chain(base, size, size, levels), false);
    kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(mips ? glc::LINEAR_MIPMAP_LINEAR : glc::LINEAR));
    kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
    kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::REPEAT));
    kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::REPEAT));
    return texture;
}

Mat4 Gx::camera(float aspect, float distance, float yaw, float pitch) {
    return Mat4::perspective(1.0f, aspect, 0.5f, 30.0f, false) * Mat4::translate(0, 0, -distance) * Mat4::rotate_x(pitch) * Mat4::rotate_y(yaw);
}

Mat4 Gx::ortho(float l, float r, float b, float t, float n, float f) {
    Mat4 m;
    m.m[0] = 2.0f / (r - l);
    m.m[5] = 2.0f / (t - b);
    m.m[10] = -2.0f / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    return m;
}

void Gx::clear(Kit& kit, float r, float g, float b, float a, bool depth, bool stencil) {
    kit.gl.glClearColor(r, g, b, a);
    GLbitfield mask = glc::COLOR_BUFFER_BIT;
    if (depth) {
        kit.gl.glClearDepth(1.0);
        kit.gl.glDepthMask(1);
        mask |= glc::DEPTH_BUFFER_BIT;
    }
    if (stencil) {
        kit.gl.glClearStencil(0);
        kit.gl.glStencilMask(0xFF);
        mask |= glc::STENCIL_BUFFER_BIT;
    }
    kit.gl.glColorMask(1, 1, 1, 1);
    kit.gl.glClear(mask);
}

bool Gx::shot(Kit& kit, Target& target, CaptureSet& out, std::string& error, const char* label, std::size_t index) {
    kit.gl.glFinish();
    Image image;
    if (!kit.check("scene", error) || !target.read_color(kit, index, image, error)) {
        return false;
    }
    out.add(label, std::move(image));
    return true;
}

bool Gx::shot_depth(Kit& kit, Target& target, CaptureSet& out, std::string& error, const char* label) {
    Image image;
    if (!kit.check("scene", error) || !target.read_depth(kit, image, error)) {
        return false;
    }
    out.add(label, std::move(image));
    return true;
}

bool Gx::shot_stencil(Kit& kit, Target& target, CaptureSet& out, std::string& error, const char* label) {
    Image image;
    if (!kit.check("scene", error) || !target.read_stencil(kit, image, error)) {
        return false;
    }
    out.add(label, std::move(image));
    return true;
}

}  // namespace lsw::render::gl
