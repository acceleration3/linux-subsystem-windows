#include "render/gl/flat2d.hpp"
#include "render/gl/scene3.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec rs(const std::string& name) { return CaseSpec("rs", name).min_version(4, 6); }

using Body = std::function<bool(Kit&, Scene3&, std::string&)>;

void scene_case(const std::string& name, Body body, Tolerance tolerance = Tolerance::tight(1), NeedFn need = {}, int samples = 1, GLenum depth = glc::DEPTH24_STENCIL8, bool with_depth = false) {
    Cases::single(rs(name).tolerance(std::move(tolerance)), [body, samples, depth, with_depth](Kit& kit, CaptureSet& out, std::string& error) {
        Scene3 scene;
        if (!scene.open(kit, error, samples, depth)) {
            return false;
        }
        if (!body(kit, scene, error)) {
            return false;
        }
        if (!Gx::shot(kit, scene.target, out, error)) {
            return false;
        }
        return !with_depth || Gx::shot_depth(kit, scene.target, out, error);
    }, std::move(need));
}

const char* kClipVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_nrm;\n"
    "layout(location = 2) in vec2 in_uv;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_nrm;\n"
    "out vec2 v_uv;\n"
    "out gl_PerVertex { vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[8]; };\n"
    "void main() {\n"
    "    v_nrm = in_nrm;\n"
    "    v_uv = in_uv;\n"
    "    for (int i = 0; i < 8; ++i) {\n"
    "        float a = 0.7853982 * float(i);\n"
    "        gl_ClipDistance[i] = dot(in_pos, vec3(cos(a), 0.3 * sin(a * 2.0), sin(a))) + 0.55;\n"
    "    }\n"
    "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n"
    "}\n";

const char* kCullVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_nrm;\n"
    "layout(location = 2) in vec2 in_uv;\n"
    "uniform mat4 u_mvp;\n"
    "out vec3 v_nrm;\n"
    "out vec2 v_uv;\n"
    "out gl_PerVertex { vec4 gl_Position; float gl_PointSize; float gl_ClipDistance[1]; float gl_CullDistance[1]; };\n"
    "void main() {\n"
    "    v_nrm = in_nrm;\n"
    "    v_uv = in_uv;\n"
    "    gl_CullDistance[0] = in_pos.y + 0.2;\n"
    "    gl_Position = u_mvp * vec4(in_pos, 1.0);\n"
    "}\n";

const char* kClipFragment =
    "#version 460 core\n"
    "in vec3 v_nrm;\n"
    "in vec2 v_uv;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    float d = max(dot(normalize(v_nrm), normalize(vec3(0.4, 0.8, 0.6))), 0.0);\n"
    "    out_color = vec4(vec3(0.9, 0.6, 0.3) * (0.25 + 0.75 * d) * (gl_FrontFacing ? 1.0 : 0.5), 1.0);\n"
    "}\n";

void overlap_quads(Kit& kit, Scene3& s) {
    const Mat4 a = Mat4::translate(-0.35f, 0.0f, 0.0f) * Mat4::scale(0.8f, 0.8f, 1.0f);
    const Mat4 b = Mat4::translate(0.35f, 0.1f, 0.0f) * Mat4::rotate_x(0.0f) * Mat4::scale(0.8f, 0.8f, 1.0f);
    s.view = Gx::camera(1.0f, 3.5f, 0.15f, 0.2f);
    kit.gl.glDepthFunc(glc::LESS);
    s.draw(kit, s.quad, a, 0.9f, 0.4f, 0.3f);
    s.draw(kit, s.quad, b, 0.3f, 0.5f, 0.9f);
}

const struct Batch13 {
    Batch13() {
        scene_case("polygon_mode_fill", [](Kit& kit, Scene3& s, std::string&) {
            s.draw(kit, s.torus, Mat4::rotate_x(0.5f));
            return true;
        });
        scene_case("polygon_mode_line", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
            s.draw(kit, s.torus, Mat4::rotate_x(0.5f));
            return true;
        }, Tolerance::raster(2, 1));
        scene_case("polygon_mode_point", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glPolygonMode(glc::FRONT_AND_BACK, glc::POINT);
            kit.gl.glPointSize(3.0f);
            s.draw(kit, s.sphere, Mat4::rotate_x(0.5f));
            return true;
        }, Tolerance::raster(2, 1));
        scene_case("polygon_mode_line_cull_back", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
            kit.gl.glEnable(glc::CULL_FACE);
            kit.gl.glCullFace(glc::BACK);
            s.draw(kit, s.torus, Mat4::rotate_x(0.9f));
            return true;
        }, Tolerance::raster(2, 1));

        struct CullDef {
            const char* name;
            GLenum face;
            GLenum front;
        };
        for (const CullDef& def : {CullDef{"cull_back_ccw", glc::BACK, glc::CCW}, CullDef{"cull_front_ccw", glc::FRONT, glc::CCW}}) {
            scene_case(def.name, [def](Kit& kit, Scene3& s, std::string&) {
                kit.gl.glEnable(glc::CULL_FACE);
                kit.gl.glCullFace(def.face);
                kit.gl.glFrontFace(def.front);
                s.draw(kit, s.torus, Mat4::rotate_x(0.7f));
                s.draw(kit, s.cube, Mat4::translate(0.0f, 0.0f, 0.0f) * Mat4::scale(0.4f, 0.4f, 0.4f));
                return true;
            });
        }
        scene_case("cull_back_front_face_cw_mirrored_geometry", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glEnable(glc::CULL_FACE);
            kit.gl.glCullFace(glc::BACK);
            kit.gl.glFrontFace(glc::CW);
            s.draw(kit, s.torus, Mat4::scale(-1.0f, 1.0f, 1.0f) * Mat4::rotate_x(0.7f));
            s.draw(kit, s.cube, Mat4::scale(-0.4f, 0.4f, 0.4f));
            return true;
        });

        scene_case("cull_disabled_two_sided", [](Kit& kit, Scene3& s, std::string&) {
            kit.gl.glDisable(glc::CULL_FACE);
            kit.gl.glDepthFunc(glc::ALWAYS);
            s.draw(kit, s.torus, Mat4::rotate_x(0.7f));
            return true;
        });

        struct OffsetDef {
            const char* name;
            GLenum enable;
            GLenum mode;
            float factor;
            float units;
            bool clamp;
        };
        for (const OffsetDef& def : {OffsetDef{"polygon_offset_fill_negative", glc::POLYGON_OFFSET_FILL, glc::FILL, -1.0f, -2.0f, false},
                                     OffsetDef{"polygon_offset_fill_positive", glc::POLYGON_OFFSET_FILL, glc::FILL, 2.0f, 2.0f, false},
                                     OffsetDef{"polygon_offset_line", glc::POLYGON_OFFSET_LINE, glc::LINE, -1.0f, -4.0f, false},
                                     OffsetDef{"polygon_offset_point", glc::POLYGON_OFFSET_POINT, glc::POINT, -1.0f, -4.0f, false}}) {
            scene_case(def.name, [def](Kit& kit, Scene3& s, std::string&) {
                Api& gl = kit.gl;
                s.view = Gx::camera(1.0f, 3.0f, 0.2f, 0.35f);
                gl.glDepthFunc(def.factor < 0.0f && def.mode == glc::FILL ? glc::LESS : glc::LEQUAL);
                s.draw(kit, s.cube, Mat4::identity(), 0.8f, 0.4f, 0.3f);
                gl.glEnable(def.enable);
                if (def.clamp) {
                    gl.glPolygonOffsetClamp(def.factor, def.units, 0.0001f);
                } else {
                    gl.glPolygonOffset(def.factor, def.units);
                }
                gl.glPolygonMode(glc::FRONT_AND_BACK, def.mode);
                gl.glPointSize(4.0f);
                gl.glLineWidth(1.0f);
                s.draw(kit, s.cube, Mat4::identity(), 0.2f, 0.9f, 0.5f);
                return true;
            }, def.mode == glc::FILL ? Tolerance::tight(1) : Tolerance::raster(2, 1));
        }

        for (const bool clamp : {true, false}) {
            scene_case(clamp ? "polygon_offset_clamp" : "polygon_offset_unclamped", [clamp](Kit& kit, Scene3& s, std::string&) {
                Api& gl = kit.gl;
                s.view = Gx::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.0f, 1.0f);
                gl.glDepthFunc(glc::LESS);
                s.draw(kit, s.quad, Mat4::translate(0.0f, 0.0f, -0.5f), 0.8f, 0.4f, 0.3f);
                gl.glEnable(glc::POLYGON_OFFSET_FILL);
                if (clamp) {
                    gl.glPolygonOffsetClamp(-1000.0f, -1000.0f, -0.00001f);
                } else {
                    gl.glPolygonOffset(-1000.0f, -1000.0f);
                }
                s.draw(kit, s.quad, Mat4::translate(-0.55f, 0.0f, -0.50003f) * Mat4::scale(0.45f, 0.8f, 1.0f), 0.2f, 0.9f, 0.5f);
                s.draw(kit, s.quad, Mat4::translate(0.55f, 0.0f, -0.500003f) * Mat4::scale(0.45f, 0.8f, 1.0f), 0.3f, 0.5f, 0.95f);
                return true;
            }, Tolerance::tight(1), {}, 1, glc::DEPTH_COMPONENT24, true);
        }

        for (const bool clamp : {false, true}) {
            scene_case(clamp ? "depth_clamp_enabled" : "depth_clamp_disabled", [clamp](Kit& kit, Scene3& s, std::string&) {
                s.view = Gx::camera(1.0f, 1.6f, 0.4f, 0.3f);
                if (clamp) {
                    kit.gl.glEnable(glc::DEPTH_CLAMP);
                }
                s.draw(kit, s.cube, Mat4::rotate_y(0.3f) * Mat4::scale(1.3f, 1.3f, 1.3f));
                return true;
            });
        }

        struct ClipDef {
            const char* name;
            GLenum origin;
            GLenum depth;
        };
        for (const ClipDef& def : {ClipDef{"clip_control_lower_left_neg", glc::LOWER_LEFT, glc::NEGATIVE_ONE_TO_ONE}, ClipDef{"clip_control_upper_left_neg", glc::UPPER_LEFT, glc::NEGATIVE_ONE_TO_ONE},
                                   ClipDef{"clip_control_lower_left_zero", glc::LOWER_LEFT, glc::ZERO_TO_ONE}, ClipDef{"clip_control_upper_left_zero", glc::UPPER_LEFT, glc::ZERO_TO_ONE}}) {
            scene_case(def.name, [def](Kit& kit, Scene3& s, std::string&) {
                kit.gl.glClipControl(def.origin, def.depth);
                kit.gl.glClearDepth(def.depth == glc::ZERO_TO_ONE ? 1.0 : 1.0);
                s.draw(kit, s.torus, Mat4::rotate_x(0.6f));
                s.draw(kit, s.cube, Mat4::translate(0.9f, -0.7f, 0.5f) * Mat4::scale(0.35f, 0.35f, 0.35f));
                return true;
            }, Tolerance::tight(1), {}, 1, glc::DEPTH24_STENCIL8, true);
        }

        scene_case("clip_distance_eight_planes", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(kClipVertex, kClipFragment, error);
            if (program == 0) {
                return false;
            }
            for (GLenum i = 0; i < 8; ++i) {
                gl.glEnable(glc::CLIP_DISTANCE0 + i);
            }
            gl.glDisable(glc::CULL_FACE);
            gl.glUseProgram(program);
            kit.umat4(program, "u_mvp", (s.view * Mat4::rotate_x(0.4f)).m);
            s.sphere.draw(kit);
            return true;
        });

        scene_case("cull_distance_primitives", [](Kit& kit, Scene3& s, std::string& error) {
            const GLuint program = kit.program(kCullVertex, kClipFragment, error);
            if (program == 0) {
                return false;
            }
            kit.gl.glDisable(glc::CULL_FACE);
            kit.gl.glUseProgram(program);
            kit.umat4(program, "u_mvp", (s.view * Mat4::rotate_x(0.2f)).m);
            s.torus.draw(kit);
            return true;
        });

        scene_case("viewport_indexed_fractional", [](Kit& kit, Scene3& s, std::string&) {
            Api& gl = kit.gl;
            s.view = Gx::camera(1.0f, 3.5f, 0.3f, 0.3f);
            gl.glViewportIndexedf(0, 6.5f, 10.25f, 52.5f, 48.75f);
            s.draw(kit, s.cube, Mat4::identity(), 0.9f, 0.5f, 0.3f);
            gl.glViewportIndexedf(0, 60.75f, 61.5f, 60.25f, 60.0f);
            s.draw(kit, s.sphere, Mat4::identity(), 0.3f, 0.7f, 0.9f);
            return true;
        });

        scene_case("scissor_array_indexed", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            static const char* gs =
                "#version 460 core\n"
                "layout(triangles, invocations = 4) in;\n"
                "layout(triangle_strip, max_vertices = 3) out;\n"
                "in vec3 v_nrm[];\n"
                "in vec2 v_uv[];\n"
                "out vec3 g_nrm;\n"
                "out vec2 g_uv;\n"
                "void main() {\n"
                "    for (int i = 0; i < 3; ++i) {\n"
                "        gl_ViewportIndex = gl_InvocationID;\n"
                "        g_nrm = v_nrm[i]; g_uv = v_uv[i];\n"
                "        gl_Position = gl_in[i].gl_Position;\n"
                "        EmitVertex();\n"
                "    }\n"
                "    EndPrimitive();\n"
                "}\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, Sources::kLitVertex}, {glc::GEOMETRY_SHADER, gs},
                                                {glc::FRAGMENT_SHADER, "#version 460 core\nin vec3 g_nrm;\nin vec2 g_uv;\nlayout(location = 0) out vec4 out_color;\n"
                                                                       "void main() { out_color = vec4(abs(g_nrm) * 0.8 + 0.1, 1.0) * vec4(vec3(0.6 + 0.4 * fract(g_uv.x * 8.0)), 1.0); }\n"}}, error);
            if (program == 0) {
                return false;
            }
            const float views[16] = {0, 0, 64, 64, 64, 0, 64, 64, 0, 64, 64, 64, 64, 64, 64, 64};
            gl.glViewportArrayv(0, 4, views);
            const GLint boxes[16] = {4, 4, 56, 40, 70, 10, 50, 50, 10, 70, 100, 40, 80, 80, 30, 30};
            gl.glEnable(glc::SCISSOR_TEST);
            gl.glScissorArrayv(0, 4, boxes);
            gl.glUseProgram(program);
            kit.umat4(program, "u_mvp", (Gx::camera(1.0f, 3.2f, 0.3f, 0.3f) * Mat4::scale(0.6f, 0.6f, 0.6f)).m);
            kit.umat4(program, "u_model", Mat4::identity().m);
            s.sphere.draw(kit);
            gl.glDisable(glc::SCISSOR_TEST);
            return true;
        });

        scene_case("scissor_test_rect_clear_and_draw", [](Kit& kit, Scene3& s, std::string&) {
            Api& gl = kit.gl;
            gl.glEnable(glc::SCISSOR_TEST);
            gl.glScissor(20, 30, 70, 50);
            gl.glClearColor(0.9f, 0.9f, 0.2f, 1.0f);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            gl.glScissor(40, 10, 80, 80);
            s.draw(kit, s.torus, Mat4::rotate_x(0.5f));
            gl.glDisable(glc::SCISSOR_TEST);
            return true;
        });

        for (const bool enabled : {true, false}) {
            scene_case(enabled ? "multisample_enabled_4x" : "multisample_disabled_4x", [enabled](Kit& kit, Scene3& s, std::string&) {
                if (enabled) {
                    kit.gl.glEnable(glc::MULTISAMPLE);
                } else {
                    kit.gl.glDisable(glc::MULTISAMPLE);
                }
                s.draw(kit, s.torus, Mat4::rotate_x(0.5f));
                s.draw(kit, s.cube, Mat4::translate(0.9f, -0.9f, 0.0f) * Mat4::scale(0.35f, 0.35f, 0.35f));
                return true;
            }, Tolerance::tight(1), {}, 4);
        }

        scene_case("sample_mask_and_coverage", [](Kit& kit, Scene3& s, std::string&) {
            Api& gl = kit.gl;
            gl.glEnable(glc::SAMPLE_MASK);
            gl.glSampleMaski(0, 0x5);
            s.draw(kit, s.cube, Mat4::translate(-0.7f, 0.0f, 0.0f) * Mat4::scale(0.9f, 0.9f, 0.9f), 0.9f, 0.4f, 0.3f);
            gl.glSampleMaski(0, 0xA);
            s.draw(kit, s.cube, Mat4::translate(0.7f, 0.0f, 0.0f) * Mat4::scale(0.9f, 0.9f, 0.9f), 0.3f, 0.6f, 0.9f);
            gl.glDisable(glc::SAMPLE_MASK);
            gl.glEnable(glc::SAMPLE_COVERAGE);
            gl.glSampleCoverage(0.5f, glc::FALSE_);
            s.draw(kit, s.sphere, Mat4::translate(0.0f, -0.9f, 0.0f) * Mat4::scale(0.4f, 0.4f, 0.4f), 0.4f, 0.9f, 0.4f);
            return true;
        }, Tolerance::tight(1), {}, 4);

        struct ShadingDef {
            const char* name;
            bool enable;
            float fraction;
        };
        for (const ShadingDef& def : {ShadingDef{"sample_shading_per_sample", true, 1.0f}, ShadingDef{"sample_shading_disabled", false, 1.0f}, ShadingDef{"sample_shading_half", true, 0.5f}}) {
            scene_case(def.name, [def](Kit& kit, Scene3& s, std::string& error) {
                Api& gl = kit.gl;
                static const char* fs =
                    "#version 460 core\n"
                    "in vec3 v_nrm;\n"
                    "in vec2 v_uv;\n"
                    "layout(location = 0) out vec4 out_color;\n"
                    "void main() {\n"
                    "    float stripe = step(0.5, fract(v_uv.x * 47.0 + v_uv.y * 31.0));\n"
                    "    out_color = vec4(vec3(0.15, 0.3, 0.9) * (0.35 + 0.65 * stripe) + 0.1 * abs(v_nrm), 1.0);\n"
                    "}\n";
                const GLuint program = kit.program(Sources::kLitVertex, fs, error);
                if (program == 0) {
                    return false;
                }
                if (def.enable) {
                    gl.glEnable(glc::SAMPLE_SHADING);
                    gl.glMinSampleShading(def.fraction);
                }
                gl.glUseProgram(program);
                const Mat4 model = Mat4::rotate_x(0.3f);
                kit.umat4(program, "u_mvp", (s.view * model).m);
                kit.umat4(program, "u_model", model.m);
                s.sphere.draw(kit);
                return true;
            }, Tolerance::tight(1), {}, 4);
        }

        scene_case("line_width_aliased", [](Kit& kit, Scene3& s, std::string& error) {
            Api& gl = kit.gl;
            GLfloat range[2] = {1, 1};
            gl.glGetFloatv(glc::ALIASED_LINE_WIDTH_RANGE, range);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            gl.glUseProgram(program);
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(8, 0.5f, 0.8f));
            gl.glBindVertexArray(vao);
            gl.glLineWidth(range[1] >= 3.0f ? 3.0f : 1.0f);
            gl.glDrawArrays(glc::LINE_LOOP, 0, 8);
            gl.glLineWidth(1.0f);
            static_cast<void>(s);
            return true;
        }, Tolerance::raster(2, 1));

        for (const bool lower : {false, true}) {
            scene_case(lower ? "point_sprite_origin_lower_left" : "point_sprite_origin_upper_left", [lower](Kit& kit, Scene3&, std::string& error) {
                Api& gl = kit.gl;
                static const char* vs =
                    "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\nout vec3 v_col;\n"
                    "void main() { v_col = in_col; gl_PointSize = 24.0; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
                static const char* fs =
                    "#version 460 core\nin vec3 v_col;\nlayout(location = 0) out vec4 out_color;\n"
                    "void main() { out_color = vec4(v_col * vec3(gl_PointCoord, 1.0 - gl_PointCoord.x), 1.0); }\n";
                const GLuint program = kit.program(vs, fs, error);
                if (program == 0) {
                    return false;
                }
                gl.glPointParameteri(glc::POINT_SPRITE_COORD_ORIGIN, static_cast<GLint>(lower ? glc::LOWER_LEFT : glc::UPPER_LEFT));
                gl.glEnable(glc::PROGRAM_POINT_SIZE);
                gl.glUseProgram(program);
                const GLuint vao = Flat2d::upload(kit, Flat2d::ring(7, 0.5f, 0.6f));
                gl.glBindVertexArray(vao);
                gl.glDrawArrays(glc::POINTS, 0, 7);
                return true;
            }, Tolerance::raster(2, 1));
        }

        scene_case("point_size_fixed_function", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 20.0f);
            gl.glDisable(glc::PROGRAM_POINT_SIZE);
            gl.glPointSize(12.0f);
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(9, 0.5f, 0.8f));
            gl.glBindVertexArray(vao);
            gl.glDrawArrays(glc::POINTS, 0, 9);
            return true;
        }, Tolerance::raster(2, 1));

        scene_case("point_size_program_controlled", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 14.0f);
            gl.glEnable(glc::PROGRAM_POINT_SIZE);
            gl.glPointSize(2.0f);
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(9, 0.5f, 0.8f));
            gl.glBindVertexArray(vao);
            gl.glDrawArrays(glc::POINTS, 0, 9);
            return true;
        }, Tolerance::raster(2, 1));

        scene_case("rasterizer_discard_then_draw", [](Kit& kit, Scene3& s, std::string&) {
            Api& gl = kit.gl;
            gl.glEnable(glc::RASTERIZER_DISCARD);
            s.draw(kit, s.torus, Mat4::rotate_x(0.5f), 1, 0, 0);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            gl.glDisable(glc::RASTERIZER_DISCARD);
            s.draw(kit, s.sphere, Mat4::scale(0.7f, 0.7f, 0.7f), 0.3f, 0.8f, 0.5f);
            return true;
        });

        scene_case("conservative_raster_nv", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glEnable(glc::CONSERVATIVE_RASTERIZATION_NV);
            const std::vector<V2> tris = {{-0.5f, -0.5f, 1, 0.5f, 0.2f}, {-0.45f, -0.5f, 1, 0.5f, 0.2f}, {-0.5f, -0.45f, 1, 0.5f, 0.2f},
                                          {0.1f, 0.1f, 0.2f, 0.8f, 1}, {0.9f, 0.12f, 0.2f, 0.8f, 1}, {0.5f, 0.13f, 0.2f, 0.8f, 1}};
            const GLuint vao = Flat2d::upload(kit, tris);
            gl.glBindVertexArray(vao);
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            gl.glDisable(glc::CONSERVATIVE_RASTERIZATION_NV);
            return true;
        }, Tolerance::tight(1), [](Kit& kit) -> std::string { return kit.has_extension("GL_NV_conservative_raster") ? std::string() : "GL_NV_conservative_raster is not exposed"; });

        scene_case("top_left_rule_shared_edges_additive", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::ONE, glc::ONE);
            gl.glBlendEquation(glc::FUNC_ADD);
            std::vector<V2> verts;
            const float k = 0.25f;
            for (int y = 0; y < 8; ++y) {
                for (int x = 0; x < 8; ++x) {
                    const float x0 = -1.0f + 0.25f * static_cast<float>(x);
                    const float y0 = -1.0f + 0.25f * static_cast<float>(y);
                    const float x1 = x0 + 0.25f;
                    const float y1 = y0 + 0.25f;
                    const float r = k * (1.0f + 0.5f * static_cast<float>((x + 2 * y) % 3));
                    const float g = k * (1.0f + 0.5f * static_cast<float>((2 * x + y) % 4) * 0.5f);
                    for (const auto& p : {std::array<float, 2>{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}}) {
                        verts.push_back({p[0], p[1], r, g, k});
                    }
                }
            }
            const GLuint vao = Flat2d::upload(kit, verts);
            gl.glBindVertexArray(vao);
            gl.glDrawArrays(glc::TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
            return true;
        }, Tolerance::exact());

        scene_case("top_left_rule_triangle_fan_additive", [](Kit& kit, Scene3&, std::string& error) {
            Api& gl = kit.gl;
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::ONE, glc::ONE);
            std::vector<V2> verts = {{0.0f, 0.0f, 0.3f, 0.3f, 0.3f}};
            for (int i = 0; i <= 12; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / 12.0f;
                verts.push_back({0.9f * std::cos(a), 0.9f * std::sin(a), 0.3f, 0.3f, 0.3f});
            }
            const GLuint vao = Flat2d::upload(kit, verts);
            gl.glBindVertexArray(vao);
            gl.glDrawArrays(glc::TRIANGLE_FAN, 0, 14);
            return true;
        }, Tolerance::exact());
    }
} kBatch13;

}  // namespace

}  // namespace lsw::render::gl
