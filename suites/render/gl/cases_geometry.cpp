#include "render/common/mat4.hpp"
#include "render/common/mesh.hpp"
#include "render/gl/gl.hpp"

namespace lsw::render::gl {

namespace {

class CubeBuffers {
public:
    void create(Kit& kit) {
        const std::vector<ColoredVertex> vertices = Mesh::cube_vertices();
        const std::vector<std::uint16_t> indices = Mesh::cube_indices();
        index_count = static_cast<GLsizei>(indices.size());
        vao = kit.vertex_array();
        kit.buffer(glc::ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(ColoredVertex), glc::STATIC_DRAW);
        kit.gl.glVertexAttribPointer(0, 3, glc::FLOAT, glc::FALSE_, sizeof(ColoredVertex), nullptr);
        kit.gl.glEnableVertexAttribArray(0);
        kit.gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(ColoredVertex), reinterpret_cast<const void*>(12));
        kit.gl.glEnableVertexAttribArray(1);
        kit.buffer(glc::ELEMENT_ARRAY_BUFFER, indices.data(), indices.size() * sizeof(std::uint16_t), glc::STATIC_DRAW);
    }

    bool draw(Kit& kit, GLuint program, const Mat4& transform, std::string& error) const {
        kit.gl.glUseProgram(program);
        const GLint location = kit.uniform(program, "u_transform", error);
        if (location < 0) {
            return false;
        }
        kit.gl.glUniformMatrix4fv(location, 1, glc::FALSE_, transform.m);
        kit.gl.glBindVertexArray(vao);
        kit.gl.glDrawElements(glc::TRIANGLES, index_count, glc::UNSIGNED_SHORT, nullptr);
        return true;
    }

    GLuint vao = 0;
    GLsizei index_count = 0;
};

class IndexedCubeDepth : public GlSingleScene {
public:
    bool draw(Kit& kit, CaptureSet& out, std::string& error) override {
        Target target;
        TargetDesc desc;
        desc.depth = glc::DEPTH_COMPONENT32F;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        const GLuint program = kit.program(GlShaders::kMeshVertex, GlShaders::kMeshFragment, error);
        if (program == 0) {
            return false;
        }
        CubeBuffers cube;
        cube.create(kit);
        Api& gl = kit.gl;
        target.bind(kit);
        gl.glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        gl.glClearDepth(1.0);
        gl.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
        gl.glEnable(glc::DEPTH_TEST);
        gl.glDepthFunc(glc::LESS);
        gl.glEnable(glc::CULL_FACE);
        gl.glCullFace(glc::BACK);
        const Mat4 projection = Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, false);
        const Mat4 view = Mat4::translate(0, 0, -6.0f);
        if (!cube.draw(kit, program, projection * view * Mat4::translate(-0.6f, 0.0f, 0.0f) * Mat4::rotate_y(0.6f) * Mat4::rotate_x(0.4f), error) ||
            !cube.draw(kit, program,
                       projection * view * Mat4::translate(0.7f, 0.2f, 0.4f) * Mat4::rotate_y(-0.9f) * Mat4::rotate_z(0.5f) *
                           Mat4::scale(0.9f, 0.9f, 0.9f),
                       error)) {
            return false;
        }
        gl.glFinish();
        Image color;
        if (!kit.check("scene", error) || !target.read_color(kit, 0, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const GlRegister<IndexedCubeDepth> kIndexedCubeDepth{CaseSpec("geometry", "indexed_cube_depth").tolerance(Tolerance::tight(1))};

class MrtDepthCapture : public GlSingleScene {
public:
    std::string unsupported(Kit& kit) override {
        return kit.integer(glc::MAX_COLOR_ATTACHMENTS) < 2 ? "fewer than two colour attachments" : std::string();
    }

    bool draw(Kit& kit, CaptureSet& out, std::string& error) override {
        Target target;
        TargetDesc desc;
        desc.colors = {glc::RGBA8, glc::RGBA16F};
        desc.depth = glc::DEPTH_COMPONENT32F;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        const GLuint program = kit.program(GlShaders::kMeshVertex, GlShaders::kMrtFragment, error);
        if (program == 0) {
            return false;
        }
        CubeBuffers cube;
        cube.create(kit);
        Api& gl = kit.gl;
        target.bind(kit);
        gl.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        gl.glClearDepth(1.0);
        gl.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
        gl.glEnable(glc::DEPTH_TEST);
        gl.glEnable(glc::CULL_FACE);
        gl.glCullFace(glc::BACK);
        if (!cube.draw(kit, program,
                       Mat4::perspective(1.0f, 1.0f, 0.5f, 20.0f, false) * Mat4::translate(0, 0, -5.0f) * Mat4::rotate_y(0.7f) *
                           Mat4::rotate_x(-0.5f),
                       error)) {
            return false;
        }
        gl.glFinish();
        Image c0;
        Image c1;
        Image depth;
        if (!kit.check("scene", error) || !target.read_color(kit, 0, c0, error) || !target.read_color(kit, 1, c1, error) ||
            !target.read_depth(kit, depth, error)) {
            return false;
        }
        out.add("color0", std::move(c0));
        out.add("color1", std::move(c1));
        out.add("depth", std::move(depth));
        return true;
    }
};

const GlRegister<MrtDepthCapture> kMrtDepthCapture{CaseSpec("targets", "mrt_depth_capture").tolerance(Tolerance::tight(1))};

}  // namespace

}  // namespace lsw::render::gl
