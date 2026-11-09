#include "render/gl/flat2d.hpp"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec prim(const char* name) { return CaseSpec("prim", name).min_version(4, 6); }

struct ArraysCommand {
    GLuint count, instances, first, base_instance;
};

struct ElementsCommand {
    GLuint count, instances, first_index;
    GLint base_vertex;
    GLuint base_instance;
};

const GLushort kTileIndices[6] = {0, 1, 2, 0, 2, 3};

struct DrawRig {
    Target target;
    GLuint program = 0;
    GLuint vao_indexed = 0;
    GLuint vao_arrays = 0;
    GLuint ibo = 0;
    GLuint index_bytes = 0;

    bool open(Kit& kit, std::string& error, unsigned sets = 3) {
        if (!target.create(kit, TargetDesc{}, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.08f, 0.08f, 0.12f);
        program = kit.program(Flat2d::kParamsVertex, Flat2d::kFragment, error);
        if (program == 0) {
            return false;
        }
        const std::vector<V2> tiles = Flat2d::tile(sets);
        vao_indexed = Flat2d::upload(kit, tiles);
        ibo = kit.buffer(glc::ELEMENT_ARRAY_BUFFER, kTileIndices, sizeof kTileIndices, glc::STATIC_DRAW);
        std::vector<V2> flat;
        for (unsigned s = 0; s < sets; ++s) {
            for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u}) {
                flat.push_back(tiles[s * 4 + i]);
            }
        }
        vao_arrays = Flat2d::upload(kit, flat);
        kit.u1i(program, "u_cell", 0);
        return kit.check("draw rig", error);
    }
};

using DrawBody = std::function<void(Kit&, DrawRig&)>;

void draw_case(const char* name, DrawBody body, NeedFn need = {}) {
    Cases::single(prim(name), [body](Kit& kit, CaptureSet& out, std::string& error) {
        DrawRig rig;
        if (!rig.open(kit, error)) {
            return false;
        }
        body(kit, rig);
        return Gx::shot(kit, rig.target, out, error);
    }, std::move(need));
}

const struct Batch6 {
    Batch6() {
        draw_case("draw_arrays_cells", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_arrays);
            for (int cell = 0; cell < 12; ++cell) {
                kit.u1i(r.program, "u_cell", cell);
                kit.gl.glDrawArrays(glc::TRIANGLES, static_cast<GLint>((cell % 3) * 6), 6);
            }
        });
        draw_case("draw_elements_ushort_offset", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            for (int cell = 0; cell < 6; ++cell) {
                kit.u1i(r.program, "u_cell", cell);
                kit.gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
            }
        });
        draw_case("draw_elements_ubyte", [](Kit& kit, DrawRig& r) {
            const GLubyte bytes[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
            kit.gl.glBindVertexArray(r.vao_indexed);
            const GLuint ibo = kit.buffer(glc::ELEMENT_ARRAY_BUFFER, bytes, sizeof bytes, glc::STATIC_DRAW);
            static_cast<void>(ibo);
            for (int cell = 0; cell < 4; ++cell) {
                kit.u1i(r.program, "u_cell", cell * 2);
                kit.gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_BYTE, reinterpret_cast<const void*>(static_cast<std::uintptr_t>((cell % 2) * 6)));
            }
        });
        draw_case("draw_elements_uint_offset", [](Kit& kit, DrawRig& r) {
            const GLuint ints[12] = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, ints, sizeof ints, glc::STATIC_DRAW);
            for (int cell = 0; cell < 6; ++cell) {
                kit.u1i(r.program, "u_cell", cell);
                kit.gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_INT, reinterpret_cast<const void*>(static_cast<std::uintptr_t>((cell % 2) * 24)));
            }
        });
        draw_case("draw_range_elements", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            for (int cell = 0; cell < 5; ++cell) {
                kit.u1i(r.program, "u_cell", cell);
                kit.gl.glDrawRangeElements(glc::TRIANGLES, 0, 3, 6, glc::UNSIGNED_SHORT, nullptr);
            }
        });
        draw_case("draw_elements_base_vertex", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            for (int set = 0; set < 3; ++set) {
                kit.u1i(r.program, "u_cell", set * 4);
                kit.gl.glDrawElementsBaseVertex(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr, set * 4);
            }
            kit.u1i(r.program, "u_cell", 12);
            kit.gl.glDrawRangeElementsBaseVertex(glc::TRIANGLES, 0, 3, 6, glc::UNSIGNED_SHORT, nullptr, 8);
        });
        draw_case("draw_arrays_instanced", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_arrays);
            kit.gl.glDrawArraysInstanced(glc::TRIANGLES, 0, 6, 7);
        });
        draw_case("draw_arrays_instanced_base_instance", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_arrays);
            kit.gl.glDrawArraysInstancedBaseInstance(glc::TRIANGLES, 6, 6, 5, 3);
        });
        draw_case("draw_elements_instanced", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.gl.glDrawElementsInstanced(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr, 9);
        });
        draw_case("draw_elements_instanced_base_instance", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.gl.glDrawElementsInstancedBaseInstance(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr, 4, 7);
        });
        draw_case("draw_elements_instanced_base_vertex", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.gl.glDrawElementsInstancedBaseVertex(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr, 5, 8);
        });
        draw_case("draw_elements_instanced_base_vertex_base_instance", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.gl.glDrawElementsInstancedBaseVertexBaseInstance(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr, 3, 4, 9);
        });
        draw_case("multi_draw_arrays", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_arrays);
            const GLint first[4] = {0, 6, 12, 0};
            const GLsizei count[4] = {6, 6, 6, 3};
            kit.gl.glMultiDrawArrays(glc::TRIANGLES, first, count, 4);
        });
        draw_case("multi_draw_elements", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            const GLsizei count[3] = {6, 3, 6};
            const void* offsets[3] = {nullptr, nullptr, reinterpret_cast<const void*>(0)};
            kit.gl.glMultiDrawElements(glc::TRIANGLES, count, glc::UNSIGNED_SHORT, offsets, 3);
        });
        draw_case("multi_draw_elements_base_vertex", [](Kit& kit, DrawRig& r) {
            kit.gl.glBindVertexArray(r.vao_indexed);
            const GLsizei count[3] = {6, 6, 6};
            const void* offsets[3] = {nullptr, nullptr, nullptr};
            const GLint bases[3] = {0, 4, 8};
            kit.gl.glMultiDrawElementsBaseVertex(glc::TRIANGLES, count, glc::UNSIGNED_SHORT, offsets, 3, bases);
        });
        draw_case("draw_arrays_indirect", [](Kit& kit, DrawRig& r) {
            const ArraysCommand cmd = {6, 6, 6, 2};
            kit.gl.glBindVertexArray(r.vao_arrays);
            kit.buffer(glc::DRAW_INDIRECT_BUFFER, &cmd, sizeof cmd, glc::STATIC_DRAW);
            kit.gl.glDrawArraysIndirect(glc::TRIANGLES, nullptr);
        });
        draw_case("draw_elements_indirect", [](Kit& kit, DrawRig& r) {
            const ElementsCommand cmd = {6, 5, 0, 4, 3};
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.buffer(glc::DRAW_INDIRECT_BUFFER, &cmd, sizeof cmd, glc::STATIC_DRAW);
            kit.gl.glDrawElementsIndirect(glc::TRIANGLES, glc::UNSIGNED_SHORT, nullptr);
        });
        draw_case("multi_draw_arrays_indirect", [](Kit& kit, DrawRig& r) {
            const ArraysCommand cmds[3] = {{6, 2, 0, 0}, {6, 3, 6, 2}, {6, 2, 12, 5}};
            kit.gl.glBindVertexArray(r.vao_arrays);
            kit.buffer(glc::DRAW_INDIRECT_BUFFER, cmds, sizeof cmds, glc::STATIC_DRAW);
            kit.gl.glMultiDrawArraysIndirect(glc::TRIANGLES, nullptr, 3, 0);
        });
        draw_case("multi_draw_elements_indirect_stride", [](Kit& kit, DrawRig& r) {
            struct Padded {
                ElementsCommand cmd;
                GLuint pad[3];
            };
            const Padded cmds[3] = {{{6, 2, 0, 0, 0}, {0, 0, 0}}, {{6, 3, 0, 4, 2}, {0, 0, 0}}, {{6, 2, 0, 8, 5}, {0, 0, 0}}};
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.buffer(glc::DRAW_INDIRECT_BUFFER, cmds, sizeof cmds, glc::STATIC_DRAW);
            kit.gl.glMultiDrawElementsIndirect(glc::TRIANGLES, glc::UNSIGNED_SHORT, nullptr, 3, sizeof(Padded));
        });
        draw_case("multi_draw_arrays_indirect_count", [](Kit& kit, DrawRig& r) {
            const ArraysCommand cmds[4] = {{6, 2, 0, 0}, {6, 3, 6, 2}, {6, 2, 12, 5}, {6, 4, 0, 9}};
            const GLsizei count = 2;
            kit.gl.glBindVertexArray(r.vao_arrays);
            kit.buffer(glc::DRAW_INDIRECT_BUFFER, cmds, sizeof cmds, glc::STATIC_DRAW);
            kit.buffer(glc::PARAMETER_BUFFER, &count, sizeof count, glc::STATIC_DRAW);
            kit.gl.glMultiDrawArraysIndirectCount(glc::TRIANGLES, nullptr, 0, 4, 0);
        });
        draw_case("multi_draw_elements_indirect_count", [](Kit& kit, DrawRig& r) {
            const ElementsCommand cmds[4] = {{6, 2, 0, 0, 0}, {6, 3, 0, 4, 2}, {6, 2, 0, 8, 5}, {6, 4, 0, 0, 9}};
            const GLsizei count = 2;
            kit.gl.glBindVertexArray(r.vao_indexed);
            kit.buffer(glc::DRAW_INDIRECT_BUFFER, cmds, sizeof cmds, glc::STATIC_DRAW);
            kit.buffer(glc::PARAMETER_BUFFER, &count, sizeof count, glc::STATIC_DRAW);
            kit.gl.glMultiDrawElementsIndirectCount(glc::TRIANGLES, glc::UNSIGNED_SHORT, nullptr, 0, 4, 0);
        });

        Cases::single(prim("restart_fixed_index"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            std::vector<V2> verts = Flat2d::ring(18, 0.3f, 0.85f);
            const GLuint vao = Flat2d::upload(kit, verts);
            kit.gl.glBindVertexArray(vao);
            const GLushort indices[] = {0, 1, 2, 3, 4, 0xFFFF, 5, 6, 7, 8, 9, 0xFFFF, 10, 11, 12, 13, 14, 15, 16, 17};
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, indices, sizeof indices, glc::STATIC_DRAW);
            kit.gl.glEnable(glc::PRIMITIVE_RESTART_FIXED_INDEX);
            kit.gl.glDrawElements(glc::TRIANGLE_STRIP, 20, glc::UNSIGNED_SHORT, nullptr);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(prim("restart_custom_index_lines").tolerance(Tolerance::raster(2, 1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(16, 0.3f, 0.85f));
            kit.gl.glBindVertexArray(vao);
            const GLuint indices[] = {0, 1, 2, 3, 99, 4, 5, 6, 7, 99, 8, 9, 10, 11, 12, 99, 13, 14, 15, 0};
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, indices, sizeof indices, glc::STATIC_DRAW);
            kit.gl.glEnable(glc::PRIMITIVE_RESTART);
            kit.gl.glPrimitiveRestartIndex(99);
            kit.gl.glDrawElements(glc::LINE_STRIP, 20, glc::UNSIGNED_INT, nullptr);
            return Gx::shot(kit, target, out, error);
        });

        const auto provoking = [](const char* name, GLenum convention) {
            Cases::single(prim(name), [convention](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                static const char* vs =
                    "#version 460 core\n"
                    "layout(location = 0) in vec2 in_pos;\n"
                    "layout(location = 1) in vec3 in_col;\n"
                    "flat out vec3 v_col;\n"
                    "void main() { v_col = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
                static const char* fs =
                    "#version 460 core\n"
                    "flat in vec3 v_col;\n"
                    "layout(location = 0) out vec4 out_color;\n"
                    "void main() { out_color = vec4(v_col, 1.0); }\n";
                const GLuint program = kit.program(vs, fs, error);
                if (program == 0) {
                    return false;
                }
                kit.gl.glUseProgram(program);
                const GLuint vao = Flat2d::upload(kit, Flat2d::ring(12, 0.5f, 0.85f));
                kit.gl.glBindVertexArray(vao);
                kit.gl.glProvokingVertex(convention);
                kit.gl.glDrawArrays(glc::TRIANGLE_FAN, 0, 12);
                kit.gl.glProvokingVertex(glc::LAST_VERTEX_CONVENTION);
                return Gx::shot(kit, target, out, error);
            });
        };
        provoking("provoking_first", glc::FIRST_VERTEX_CONVENTION);
        provoking("provoking_last", glc::LAST_VERTEX_CONVENTION);

        const auto feedback = [](const char* name, int variant) {
            Cases::single(prim(name), [variant](Kit& kit, CaptureSet& out, std::string& error) {
                DrawRig rig;
                if (!rig.open(kit, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                static const char* vs =
                    "#version 460 core\n"
                    "layout(location = 0) in vec2 in_pos;\n"
                    "layout(location = 1) in vec3 in_col;\n"
                    "out vec2 tf_pos;\n"
                    "out vec3 tf_col;\n"
                    "uniform float u_k;\nuniform float u_a;\n"
                    "void main() {\n"
                    "    vec2 p = in_pos * 0.5 + vec2(float(gl_InstanceID % 4) * 0.5 - 0.75, float(gl_InstanceID / 4) * 0.5 - 0.75);\n"
                    "    tf_pos = vec2(cos(u_a) * p.x - sin(u_a) * p.y, sin(u_a) * p.x + cos(u_a) * p.y) * u_k;\n"
                    "    tf_col = in_col;\n"
                    "    gl_Position = vec4(tf_pos, 0.0, 1.0);\n"
                    "}\n";
                const GLuint vertex = kit.shader(glc::VERTEX_SHADER, vs, error);
                if (vertex == 0) {
                    return false;
                }
                const GLuint capture = gl.glCreateProgram();
                gl.glAttachShader(capture, vertex);
                const char* varyings[2] = {"tf_pos", "tf_col"};
                gl.glTransformFeedbackVaryings(capture, 2, varyings, glc::INTERLEAVED_ATTRIBS);
                gl.glLinkProgram(capture);
                GLint ok = 0;
                gl.glGetProgramiv(capture, glc::LINK_STATUS, &ok);
                if (ok == 0) {
                    error = "feedback program failed to link";
                    return false;
                }
                kit.defer([&kit, capture] { kit.gl.glDeleteProgram(capture); });
                const GLsizei instances = 6;
                const GLuint feedback_buffer = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 6 * instances * sizeof(V2), glc::DYNAMIC_COPY);
                GLuint tfo = 0;
                gl.glGenTransformFeedbacks(1, &tfo);
                gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, tfo);
                gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, feedback_buffer);
                kit.defer([&kit, tfo] { kit.gl.glDeleteTransformFeedbacks(1, &tfo); });
                const float ks[4] = {1.0f, 0.8f, 0.9f, 0.7f};
                kit.u1f(capture, "u_k", ks[variant]);
                kit.u1f(capture, "u_a", 0.25f + 0.3f * static_cast<float>(variant));
                gl.glUseProgram(capture);
                gl.glBindVertexArray(rig.vao_arrays);
                gl.glEnable(glc::RASTERIZER_DISCARD);
                gl.glBeginTransformFeedback(glc::TRIANGLES);
                gl.glDrawArraysInstanced(glc::TRIANGLES, 0, 6, instances);
                gl.glEndTransformFeedback();
                gl.glDisable(glc::RASTERIZER_DISCARD);
                gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
                const GLuint draw_program = variant >= 2 ? kit.program(Flat2d::kParamsVertex, Flat2d::kFragment, error) : kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
                if (draw_program == 0) {
                    return false;
                }
                if (variant >= 2) {
                    kit.u1i(draw_program, "u_cell", 0);
                } else {
                    kit.u1f(draw_program, "u_size", 1.0f);
                }
                GLuint replay = 0;
                gl.glGenVertexArrays(1, &replay);
                gl.glBindVertexArray(replay);
                kit.defer([&kit, replay] { kit.gl.glDeleteVertexArrays(1, &replay); });
                gl.glBindBuffer(glc::ARRAY_BUFFER, feedback_buffer);
                gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
                gl.glEnableVertexAttribArray(0);
                gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
                gl.glEnableVertexAttribArray(1);
                if (variant == 0) {
                    gl.glDrawTransformFeedback(glc::TRIANGLES, tfo);
                } else if (variant == 1) {
                    gl.glDrawTransformFeedbackStream(glc::TRIANGLES, tfo, 0);
                } else if (variant == 2) {
                    gl.glDrawTransformFeedbackInstanced(glc::TRIANGLES, tfo, 3);
                } else {
                    gl.glDrawTransformFeedbackStreamInstanced(glc::TRIANGLES, tfo, 0, 2);
                }
                return Gx::shot(kit, rig.target, out, error);
            });
        };
        feedback("draw_transform_feedback", 0);
        feedback("draw_transform_feedback_stream", 1);
        feedback("draw_transform_feedback_instanced", 2);
        feedback("draw_transform_feedback_stream_instanced", 3);
    }
} kBatch6;

}  // namespace

}  // namespace lsw::render::gl
