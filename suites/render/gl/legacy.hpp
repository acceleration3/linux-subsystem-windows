#pragma once

#include "render/gl/toolkit.hpp"

#include <array>
#include <cmath>

namespace lsw::render::gl {

class Legacy {
public:
    static bool open(Kit& kit, Target& target, std::string& error, GLenum depth = glc::DEPTH24_STENCIL8, int samples = 1) {
        TargetDesc desc;
        desc.depth = depth;
        desc.samples = samples;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1.0f, true, true);
        Api& gl = kit.gl;
        gl.glMatrixMode(glc::PROJECTION);
        gl.glLoadIdentity();
        gl.glMatrixMode(glc::MODELVIEW);
        gl.glLoadIdentity();
        return true;
    }

    static void perspective(Kit& kit, float tilt = 0.0f, float spin = 0.0f, float distance = 3.5f) {
        Api& gl = kit.gl;
        gl.glMatrixMode(glc::PROJECTION);
        gl.glLoadIdentity();
        gl.glFrustum(-0.5, 0.5, -0.5, 0.5, 1.0, 30.0);
        gl.glMatrixMode(glc::MODELVIEW);
        gl.glLoadIdentity();
        gl.glTranslatef(0, 0, -distance);
        gl.glRotatef(tilt, 1, 0, 0);
        gl.glRotatef(spin, 0, 1, 0);
    }

    static void rainbow(unsigned i, unsigned n, float* c) {
        const float h = 6.0f * static_cast<float>(i) / static_cast<float>(n);
        const auto clamp = [](float v) { return v < 0 ? 0.0f : (v > 1 ? 1.0f : v); };
        c[0] = clamp(std::fabs(h - 3) - 1);
        c[1] = clamp(2 - std::fabs(h - 2));
        c[2] = clamp(2 - std::fabs(h - 4));
    }

    static void draw_shape(Kit& kit, const Shape& s, bool normals = true, bool texcoords = false, GLenum mode = glc::TRIANGLES) {
        Api& gl = kit.gl;
        const float* v = s.vertices.data();
        gl.glEnableClientState(glc::VERTEX_ARRAY);
        gl.glVertexPointer(3, glc::FLOAT, Gx::kStride, v);
        if (normals) {
            gl.glEnableClientState(glc::NORMAL_ARRAY);
            gl.glNormalPointer(glc::FLOAT, Gx::kStride, v + 3);
        }
        if (texcoords) {
            gl.glEnableClientState(glc::TEXTURE_COORD_ARRAY);
            gl.glTexCoordPointer(2, glc::FLOAT, Gx::kStride, v + 6);
        }
        gl.glDrawElements(mode, static_cast<GLsizei>(s.indices.size()), glc::UNSIGNED_INT, s.indices.data());
        gl.glDisableClientState(glc::VERTEX_ARRAY);
        gl.glDisableClientState(glc::NORMAL_ARRAY);
        gl.glDisableClientState(glc::TEXTURE_COORD_ARRAY);
    }

    static void light(Kit& kit, unsigned index, const float* position, const float* diffuse, const float* specular = nullptr) {
        Api& gl = kit.gl;
        const GLenum id = glc::LIGHT0 + index;
        static const float zero[4] = {0, 0, 0, 1};
        gl.glEnable(id);
        gl.glLightfv(id, glc::POSITION, position);
        gl.glLightfv(id, glc::DIFFUSE, diffuse);
        gl.glLightfv(id, glc::SPECULAR, specular != nullptr ? specular : zero);
        gl.glLightfv(id, glc::AMBIENT, zero);
    }

    static GLuint alpha_texture(Kit& kit, unsigned size = 64) {
        Pattern::Bytes bytes = Pattern::checker(size, size, size / 8, Pattern::pack(250, 200, 60), Pattern::pack(40, 120, 230));
        for (unsigned y = 0; y < size; ++y) {
            for (unsigned x = 0; x < size; ++x) {
                bytes[(y * size + x) * 4 + 3] = static_cast<std::uint8_t>(255 * x / (size - 1));
            }
        }
        const GLuint id = kit.texture2d(size, size, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, {bytes}, false);
        kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
        kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
        return id;
    }

    static GLuint alpha_steps_texture(Kit& kit, unsigned size = 64) {
        const Pattern::Bytes colors = Pattern::checker(size, size, size / 8, Pattern::pack(250, 200, 60), Pattern::pack(40, 120, 230));
        const float steps[4] = {0.25f, 0.5f, 0.75f, 0.5f};
        std::vector<std::uint16_t> texels(static_cast<std::size_t>(size) * size * 4);
        for (unsigned y = 0; y < size; ++y) {
            for (unsigned x = 0; x < size; ++x) {
                for (unsigned c = 0; c < 3; ++c) {
                    texels[(y * size + x) * 4 + c] = PixelInfo::float_to_half(static_cast<float>(colors[(y * size + x) * 4 + c]) / 255.0f);
                }
                texels[(y * size + x) * 4 + 3] = PixelInfo::float_to_half(steps[(x / 8) % 4]);
            }
        }
        GLuint id = 0;
        kit.gl.glGenTextures(1, &id);
        kit.gl.glBindTexture(glc::TEXTURE_2D, id);
        kit.defer([&kit, id] { kit.gl.glDeleteTextures(1, &id); });
        kit.gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<GLint>(glc::RGBA16F), static_cast<GLsizei>(size), static_cast<GLsizei>(size), 0, glc::RGBA, glc::HALF_FLOAT, texels.data());
        kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
        kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
        kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
        kit.gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
        return id;
    }
};

}  // namespace lsw::render::gl
