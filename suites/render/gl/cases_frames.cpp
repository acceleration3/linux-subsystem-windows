#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/gl/gl.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::gl {

namespace {

constexpr unsigned kRingSlots = 4;
constexpr unsigned kStampVertices = 12;

void stamp(ColoredVertex* out, unsigned frame) {
    const float angle = static_cast<float>(frame) * 0.7854f;
    const float cx = 0.6f * std::cos(angle);
    const float cy = 0.6f * std::sin(angle);
    for (unsigned t = 0; t < 4; ++t) {
        const float a0 = angle + static_cast<float>(t) * 1.5708f;
        const float r = 0.16f + 0.01f * static_cast<float>(frame);
        ColoredVertex* v = out + t * 3;
        v[0] = {{cx, cy, 0.0f}, {1.0f, static_cast<float>(frame) / 7.0f, 0.2f}};
        v[1] = {{cx + r * std::cos(a0), cy + r * std::sin(a0), 0.0f}, {0.2f, 1.0f, static_cast<float>(frame) / 7.0f}};
        v[2] = {{cx + r * std::cos(a0 + 1.5708f), cy + r * std::sin(a0 + 1.5708f), 0.0f}, {static_cast<float>(frame) / 7.0f, 0.3f, 1.0f}};
    }
}

class RingAccumulate : public GlScene {
public:
    bool setup(Kit& kit, std::string& error) override {
        if (!target_.create(kit, TargetDesc{}, error)) {
            return false;
        }
        program_ = kit.program(GlShaders::kMeshVertex, GlShaders::kMeshFragment, error);
        if (program_ == 0) {
            return false;
        }
        slot_bytes_ = kStampVertices * sizeof(ColoredVertex);
        vao_ = kit.vertex_array();
        ring_ = kit.buffer(glc::ARRAY_BUFFER, nullptr, slot_bytes_ * kRingSlots, glc::DYNAMIC_DRAW);
        Api& gl = kit.gl;
        gl.glVertexAttribPointer(0, 3, glc::FLOAT, glc::FALSE_, sizeof(ColoredVertex), nullptr);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(ColoredVertex), reinterpret_cast<const void*>(12));
        gl.glEnableVertexAttribArray(1);
        return kit.check("ring setup", error);
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        Api& gl = kit.gl;
        const unsigned slot = index % kRingSlots;
        target_.bind(kit);
        if (index == 0) {
            gl.glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
            gl.glClear(glc::COLOR_BUFFER_BIT);
        }
        gl.glBindVertexArray(vao_);
        gl.glBindBuffer(glc::ARRAY_BUFFER, ring_);
        void* mapped = gl.glMapBufferRange(glc::ARRAY_BUFFER, static_cast<GLintptr>(slot * slot_bytes_), static_cast<GLsizeiptr>(slot_bytes_),
                                           glc::MAP_WRITE_BIT | glc::MAP_INVALIDATE_RANGE_BIT);
        if (mapped == nullptr) {
            error = "glMapBufferRange returned null";
            return false;
        }
        stamp(static_cast<ColoredVertex*>(mapped), index);
        gl.glUnmapBuffer(glc::ARRAY_BUFFER);
        gl.glUseProgram(program_);
        const Mat4 transform = Mat4::rotate_z(0.05f * static_cast<float>(index));
        const GLint location = kit.uniform(program_, "u_transform", error);
        if (location < 0) {
            return false;
        }
        gl.glUniformMatrix4fv(location, 1, glc::FALSE_, transform.m);
        gl.glDrawArrays(glc::TRIANGLES, static_cast<GLint>(slot * kStampVertices), kStampVertices);
        return kit.check("ring frame", error);
    }

    bool capture(Kit& kit, unsigned, CaptureSet& out, std::string& error) override {
        Image color;
        if (!target_.read_color(kit, 0, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }

private:
    Target target_;
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint ring_ = 0;
    std::size_t slot_bytes_ = 0;
};

const GlRegister<RingAccumulate> kRingAccumulate{CaseSpec("frames", "ring_accumulate").frames(8, {0, 3, 7}).tolerance(Tolerance::tight(1))};

class SwapchainCube : public GlScene {
public:
    bool setup(Kit& kit, std::string& error) override {
        program_ = kit.program(GlShaders::kMeshVertex, GlShaders::kMeshFragment, error);
        if (program_ == 0) {
            return false;
        }
        const std::vector<ColoredVertex> vertices = Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = Mesh::cube_indices();
        index_count_ = static_cast<GLsizei>(indices.size());
        vao_ = kit.vertex_array();
        kit.buffer(glc::ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(ColoredVertex), glc::STATIC_DRAW);
        Api& gl = kit.gl;
        gl.glVertexAttribPointer(0, 3, glc::FLOAT, glc::FALSE_, sizeof(ColoredVertex), nullptr);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(ColoredVertex), reinterpret_cast<const void*>(12));
        gl.glEnableVertexAttribArray(1);
        kit.buffer(glc::ELEMENT_ARRAY_BUFFER, indices.data(), indices.size() * sizeof(std::uint16_t), glc::STATIC_DRAW);
        return kit.check("swap setup", error);
    }

    bool frame(Kit& kit, unsigned index, std::string& error) override {
        Api& gl = kit.gl;
        gl.glBindFramebuffer(glc::FRAMEBUFFER, kit.platform->back_framebuffer());
        gl.glViewport(0, 0, 128, 128);
        gl.glClearColor(0.1f, 0.12f, 0.2f, 1.0f);
        gl.glClearDepth(1.0);
        gl.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
        gl.glEnable(glc::DEPTH_TEST);
        gl.glDepthFunc(glc::LESS);
        gl.glEnable(glc::CULL_FACE);
        gl.glCullFace(glc::BACK);
        gl.glUseProgram(program_);
        const Mat4 transform = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, false) * Mat4::translate(0, 0, -5.0f) *
                               Mat4::rotate_y(0.5f * static_cast<float>(index)) * Mat4::rotate_x(0.4f);
        const GLint location = kit.uniform(program_, "u_transform", error);
        if (location < 0) {
            return false;
        }
        gl.glUniformMatrix4fv(location, 1, glc::FALSE_, transform.m);
        gl.glBindVertexArray(vao_);
        gl.glDrawElements(glc::TRIANGLES, index_count_, glc::UNSIGNED_SHORT, nullptr);
        gl.glFinish();
        if (!kit.read_color(kit.platform->back_framebuffer(), kit.platform->back_read_buffer(), glc::RGBA8, 128, 128, last_, error)) {
            return false;
        }
        return kit.platform->swap(error);
    }

    bool capture(Kit&, unsigned, CaptureSet& out, std::string&) override {
        out.add("", last_);
        return true;
    }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLsizei index_count_ = 0;
    Image last_;
};

const GlRegister<SwapchainCube> kSwapchainCube{
    CaseSpec("frames", "swapchain_cube").frames(4, {0, 3}).swapchain().tolerance(Tolerance::tight(1))};

}  // namespace

}  // namespace lsw::render::gl
