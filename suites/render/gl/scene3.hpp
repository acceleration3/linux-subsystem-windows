#pragma once

#include "render/gl/toolkit.hpp"

namespace lsw::render::gl {

class Scene3 {
public:
    Target target;
    GLuint program = 0;
    GLuint texture = 0;
    GLuint lit = 0;
    GpuShape sphere;
    GpuShape torus;
    GpuShape cube;
    GpuShape quad;
    Mat4 view = Gx::camera(1.0f, 4.0f, 0.6f, 0.45f);

    bool open(Kit& kit, std::string& error, int samples = 1, GLenum depth = glc::DEPTH24_STENCIL8, unsigned size = 128) {
        TargetDesc desc;
        desc.width = size;
        desc.height = size;
        desc.depth = depth;
        desc.samples = samples;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1.0f, true, true);
        program = kit.program(Sources::kLitVertex, Sources::kLitFragment, error);
        if (program == 0) {
            return false;
        }
        lit = program;
        texture = Gx::make_texture(kit, 64, 0);
        sphere = Gx::upload(kit, Shape::sphere(20, 28, 1.0f));
        torus = Gx::upload(kit, Shape::torus(28, 14, 0.9f, 0.4f));
        cube = Gx::upload(kit, Shape::cube(0.8f));
        quad = Gx::upload(kit, Shape::quad(-1.0f, -1.0f, 1.0f, 1.0f));
        kit.gl.glActiveTexture(glc::TEXTURE0);
        kit.gl.glBindTexture(glc::TEXTURE_2D, texture);
        kit.u1i(program, "u_tex", 0);
        kit.u3f(program, "u_light", 0.5f, 0.8f, 0.7f);
        kit.gl.glEnable(glc::DEPTH_TEST);
        return kit.check("scene3 setup", error);
    }

    void draw(Kit& kit, const GpuShape& shape, const Mat4& model, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f, GLenum mode = glc::TRIANGLES) {
        kit.gl.glUseProgram(program);
        kit.umat4(program, "u_mvp", (view * model).m);
        const GLint model_location = kit.gl.glGetUniformLocation(program, "u_model");
        if (model_location >= 0) {
            kit.gl.glUniformMatrix4fv(model_location, 1, glc::FALSE_, model.m);
        }
        if (program == lit) {
            kit.u4f(program, "u_tint", r, g, b, a);
        }
        shape.draw(kit, mode);
    }
};

}  // namespace lsw::render::gl
