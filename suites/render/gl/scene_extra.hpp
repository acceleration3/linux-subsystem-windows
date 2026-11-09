#pragma once

#include "render/gl/flat2d.hpp"
#include "render/gl/scene3.hpp"
#include "render/gl/texrig.hpp"

#include <array>
#include <cmath>
#include <cstring>

namespace lsw::render::gl {

class Xf {
public:
    static Mat4 look_at(const float* eye, const float* target, const float* up) {
        float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
        normalize(f);
        float s[3] = {f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0]};
        normalize(s);
        const float u[3] = {s[1] * f[2] - s[2] * f[1], s[2] * f[0] - s[0] * f[2], s[0] * f[1] - s[1] * f[0]};
        Mat4 out;
        out.m[0] = s[0];
        out.m[4] = s[1];
        out.m[8] = s[2];
        out.m[1] = u[0];
        out.m[5] = u[1];
        out.m[9] = u[2];
        out.m[2] = -f[0];
        out.m[6] = -f[1];
        out.m[10] = -f[2];
        out.m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
        out.m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
        out.m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
        return out;
    }

    static Mat4 inverse(const Mat4& a) {
        const float* m = a.m;
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
        const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        Mat4 out;
        const float k = det != 0.0f ? 1.0f / det : 0.0f;
        for (int i = 0; i < 16; ++i) {
            out.m[i] = inv[i] * k;
        }
        return out;
    }

private:
    static void normalize(float* v) {
        const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0.0f) {
            v[0] /= l;
            v[1] /= l;
            v[2] /= l;
        }
    }
};

struct RenderTarget {
    GLuint fbo = 0;
    GLuint color[4] = {};
    GLuint depth = 0;
    unsigned width = 0;
    unsigned height = 0;
    unsigned count = 0;

    bool create(Kit& kit, unsigned w, unsigned h, std::initializer_list<GLenum> formats, GLenum depth_format, std::string& error, GLenum filter = glc::NEAREST) {
        Api& gl = kit.gl;
        width = w;
        height = h;
        gl.glGenFramebuffers(1, &fbo);
        gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        kit.defer([&kit, f = fbo] { kit.gl.glDeleteFramebuffers(1, &f); });
        GLenum draw[4] = {};
        for (GLenum format : formats) {
            gl.glGenTextures(1, &color[count]);
            gl.glBindTexture(glc::TEXTURE_2D, color[count]);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, format, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            kit.defer([&kit, t = color[count]] { kit.gl.glDeleteTextures(1, &t); });
            gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0 + count, glc::TEXTURE_2D, color[count], 0);
            draw[count] = glc::COLOR_ATTACHMENT0 + count;
            ++count;
        }
        if (depth_format != 0) {
            gl.glGenTextures(1, &depth);
            gl.glBindTexture(glc::TEXTURE_2D, depth);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, depth_format, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::CLAMP_TO_EDGE));
            kit.defer([&kit, t = depth] { kit.gl.glDeleteTextures(1, &t); });
            gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::TEXTURE_2D, depth, 0);
        }
        if (count > 0) {
            gl.glDrawBuffers(static_cast<GLsizei>(count), draw);
        } else {
            gl.glDrawBuffer(glc::NONE);
            gl.glReadBuffer(glc::NONE);
        }
        if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
            error = "render target framebuffer incomplete";
            return false;
        }
        return true;
    }

    void bind(Kit& kit) const {
        kit.gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
        kit.gl.glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    }
};

struct Painter {
    Kit& kit;

    void matrix(GLuint program, const char* name, const Mat4& m) const {
        const GLint location = kit.gl.glGetUniformLocation(program, name);
        if (location >= 0) {
            kit.gl.glUniformMatrix4fv(location, 1, glc::FALSE_, m.m);
        }
    }
    void vec(GLuint program, const char* name, float x, float y, float z, float w = 1.0f) const {
        const GLint location = kit.gl.glGetUniformLocation(program, name);
        if (location >= 0) {
            kit.gl.glUniform4f(location, x, y, z, w);
        }
    }
    void vec3(GLuint program, const char* name, float x, float y, float z) const {
        const GLint location = kit.gl.glGetUniformLocation(program, name);
        if (location >= 0) {
            kit.gl.glUniform3f(location, x, y, z);
        }
    }
    void scalar(GLuint program, const char* name, float v) const {
        const GLint location = kit.gl.glGetUniformLocation(program, name);
        if (location >= 0) {
            kit.gl.glUniform1f(location, v);
        }
    }
    void integer(GLuint program, const char* name, GLint v) const {
        const GLint location = kit.gl.glGetUniformLocation(program, name);
        if (location >= 0) {
            kit.gl.glUniform1i(location, v);
        }
    }
    void draw(GLuint program, const GpuShape& shape, const Mat4& vp, const Mat4& model, float r, float g, float b, float a = 1.0f) const {
        kit.gl.glUseProgram(program);
        matrix(program, "u_vp", vp);
        matrix(program, "u_model", model);
        matrix(program, "u_mvp", vp * model);
        vec(program, "u_color", r, g, b, a);
        shape.draw(kit);
    }
    void bind_texture(unsigned unit, GLenum target, GLuint texture) const {
        kit.gl.glActiveTexture(glc::TEXTURE0 + unit);
        kit.gl.glBindTexture(target, texture);
    }
};

struct SceneMeshes {
    GpuShape sphere;
    GpuShape torus;
    GpuShape cube;
    GpuShape quad;
    GpuShape cone;

    void create(Kit& kit) {
        sphere = Gx::upload(kit, Shape::sphere(20, 28, 1.0f));
        torus = Gx::upload(kit, Shape::torus(28, 14, 0.9f, 0.35f));
        cube = Gx::upload(kit, Shape::cube(1.0f));
        quad = Gx::upload(kit, Shape::quad(-1.0f, -1.0f, 1.0f, 1.0f));
        cone = Gx::upload(kit, Shape::cone(24, 0.8f, 1.6f));
    }
};

class ShadowSource {
public:
    static constexpr const char* kDepthVertex =
        "#version 460 core\n"
        "layout(location = 0) in vec3 in_pos;\n"
        "uniform mat4 u_mvp;\n"
        "void main() { gl_Position = u_mvp * vec4(in_pos, 1.0); }\n";
    static constexpr const char* kDepthFragment =
        "#version 460 core\n"
        "void main() { }\n";
    static constexpr const char* kLitVertex =
        "#version 460 core\n"
        "layout(location = 0) in vec3 in_pos;\n"
        "layout(location = 1) in vec3 in_nrm;\n"
        "layout(location = 2) in vec2 in_uv;\n"
        "uniform mat4 u_vp;\n"
        "uniform mat4 u_model;\n"
        "uniform mat4 u_view;\n"
        "out vec3 v_nrm;\n"
        "out vec3 v_world;\n"
        "out vec2 v_uv;\n"
        "out float v_depth;\n"
        "void main() {\n"
        "    vec4 w = u_model * vec4(in_pos, 1.0);\n"
        "    v_world = w.xyz;\n"
        "    v_nrm = mat3(u_model) * in_nrm;\n"
        "    v_uv = in_uv;\n"
        "    v_depth = -(u_view * w).z;\n"
        "    gl_Position = u_vp * w;\n"
        "}\n";
};

}  // namespace lsw::render::gl
