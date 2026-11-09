#include "render/gl/flat2d.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec prim(const char* name) { return CaseSpec("prim", name).min_version(4, 6); }

const char* kAdjacencyGeometry =
    "#version 460 core\n"
    "layout(%s) in;\n"
    "layout(%s, max_vertices = 3) out;\n"
    "in vec3 vc[];\n"
    "out vec3 gc;\n"
    "void main() {\n"
    "    for (int i = 0; i < %d; ++i) {\n"
    "        int k = %s;\n"
    "        gl_Position = gl_in[k].gl_Position;\n"
    "        gc = vc[k] * 0.6 + 0.4 * vc[(k + 1) %% gl_in.length()];\n"
    "        EmitVertex();\n"
    "    }\n"
    "    EndPrimitive();\n"
    "}\n";

const char* kAdjacencyVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "out vec3 vc;\n"
    "void main() { vc = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";

const char* kAdjacencyFragment =
    "#version 460 core\n"
    "in vec3 gc;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() { out_color = vec4(gc, 1.0); }\n";

const char* kPatchVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "out vec3 cv_col;\n"
    "void main() { cv_col = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";

const char* kPatchFragment =
    "#version 460 core\n"
    "in vec3 te_col;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() { out_color = vec4(te_col, 1.0); }\n";

std::string patch_control(unsigned n) {
    return "#version 460 core\nlayout(vertices = " + std::to_string(n) +
           ") out;\nin vec3 cv_col[];\nout vec3 tc_col[];\n"
           "void main() {\n"
           "    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
           "    tc_col[gl_InvocationID] = cv_col[gl_InvocationID];\n"
           "    if (gl_InvocationID == 0) {\n"
           "        gl_TessLevelOuter[0] = 4.0; gl_TessLevelOuter[1] = 4.0; gl_TessLevelOuter[2] = 4.0; gl_TessLevelOuter[3] = 4.0;\n"
           "        gl_TessLevelInner[0] = 5.0; gl_TessLevelInner[1] = 5.0;\n"
           "    }\n"
           "}\n";
}

const char* kEvalTriangles =
    "#version 460 core\n"
    "layout(triangles, equal_spacing, ccw) in;\n"
    "in vec3 tc_col[];\n"
    "out vec3 te_col;\n"
    "void main() {\n"
    "    vec3 w = gl_TessCoord;\n"
    "    gl_Position = w.x * gl_in[0].gl_Position + w.y * gl_in[1].gl_Position + w.z * gl_in[2].gl_Position;\n"
    "    te_col = w.x * tc_col[0] + w.y * tc_col[1] + w.z * tc_col[2];\n"
    "}\n";

const char* kEvalQuads =
    "#version 460 core\n"
    "layout(quads, fractional_odd_spacing, cw) in;\n"
    "in vec3 tc_col[];\n"
    "out vec3 te_col;\n"
    "void main() {\n"
    "    vec2 t = gl_TessCoord.xy;\n"
    "    vec4 a = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x);\n"
    "    vec4 b = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x);\n"
    "    gl_Position = mix(a, b, t.y);\n"
    "    te_col = mix(mix(tc_col[0], tc_col[1], t.x), mix(tc_col[3], tc_col[2], t.x), t.y);\n"
    "}\n";

const char* kEvalBezier =
    "#version 460 core\n"
    "layout(quads, equal_spacing, ccw) in;\n"
    "in vec3 tc_col[];\n"
    "out vec3 te_col;\n"
    "vec4 basis(float t) {\n"
    "    float u = 1.0 - t;\n"
    "    return vec4(u * u * u, 3.0 * t * u * u, 3.0 * t * t * u, t * t * t);\n"
    "}\n"
    "void main() {\n"
    "    vec4 bu = basis(gl_TessCoord.x);\n"
    "    vec4 bv = basis(gl_TessCoord.y);\n"
    "    vec4 p = vec4(0.0);\n"
    "    vec3 c = vec3(0.0);\n"
    "    for (int j = 0; j < 4; ++j) {\n"
    "        for (int i = 0; i < 4; ++i) {\n"
    "            float w = bu[i] * bv[j];\n"
    "            p += w * gl_in[j * 4 + i].gl_Position;\n"
    "            c += w * tc_col[j * 4 + i];\n"
    "        }\n"
    "    }\n"
    "    gl_Position = p;\n"
    "    te_col = c;\n"
    "}\n";

const char* kEvalIsolines =
    "#version 460 core\n"
    "layout(isolines, equal_spacing, ccw) in;\n"
    "in vec3 tc_col[];\n"
    "out vec3 te_col;\n"
    "void main() {\n"
    "    float a = 6.2831853 * gl_TessCoord.x;\n"
    "    float r = 0.15 + 0.5 * gl_TessCoord.y;\n"
    "    gl_Position = gl_in[0].gl_Position + vec4(r * cos(a), r * sin(a), 0.0, 0.0);\n"
    "    te_col = tc_col[0] * (0.5 + gl_TessCoord.y);\n"
    "}\n";

const char* kEvalPoints =
    "#version 460 core\n"
    "layout(triangles, equal_spacing, ccw, point_mode) in;\n"
    "in vec3 tc_col[];\n"
    "out vec3 te_col;\n"
    "void main() {\n"
    "    vec3 w = gl_TessCoord;\n"
    "    gl_Position = w.x * gl_in[0].gl_Position + w.y * gl_in[1].gl_Position + w.z * gl_in[2].gl_Position;\n"
    "    gl_PointSize = 5.0;\n"
    "    te_col = w;\n"
    "}\n";

struct SimpleDef {
    const char* name;
    GLenum mode;
    unsigned count;
    float inner;
    float size;
};

const struct Batch5 {
    Batch5() {
        const SimpleDef simple[] = {{"mode_points", glc::POINTS, 14, 0.4f, 9.0f},      {"mode_lines", glc::LINES, 16, 0.3f, 1.0f},
                                    {"mode_line_strip", glc::LINE_STRIP, 11, 0.35f, 1.0f}, {"mode_line_loop", glc::LINE_LOOP, 12, 0.3f, 1.0f},
                                    {"mode_triangles", glc::TRIANGLES, 12, 0.35f, 1.0f},   {"mode_triangle_strip", glc::TRIANGLE_STRIP, 12, 0.45f, 1.0f},
                                    {"mode_triangle_fan", glc::TRIANGLE_FAN, 12, 0.7f, 1.0f}};
        for (const SimpleDef& def : simple) {
            const bool raster = def.mode <= glc::LINE_STRIP;
            Cases::single(prim(def.name).tolerance(raster ? Tolerance::raster(2, 1) : Tolerance::tight(1)), [def](Kit& kit, CaptureSet& out, std::string& error) {
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
                kit.u1f(program, "u_size", def.size);
                kit.gl.glEnable(glc::PROGRAM_POINT_SIZE);
                const GLuint vao = Flat2d::upload(kit, Flat2d::ring(def.count, def.inner, 0.85f));
                kit.gl.glBindVertexArray(vao);
                kit.gl.glDrawArrays(def.mode, 0, static_cast<GLsizei>(def.count));
                return Gx::shot(kit, target, out, error);
            });
        }

        struct AdjDef {
            const char* name;
            GLenum mode;
            const char* in;
            const char* out;
            unsigned count;
            int emit;
            const char* index;
        };
        const AdjDef adjacency[] = {{"mode_lines_adjacency", glc::LINES_ADJACENCY, "lines_adjacency", "line_strip", 16, 2, "i + 1"},
                                    {"mode_line_strip_adjacency", glc::LINE_STRIP_ADJACENCY, "lines_adjacency", "line_strip", 10, 2, "i + 1"},
                                    {"mode_triangles_adjacency", glc::TRIANGLES_ADJACENCY, "triangles_adjacency", "triangle_strip", 18, 3, "2 * i"},
                                    {"mode_triangle_strip_adjacency", glc::TRIANGLE_STRIP_ADJACENCY, "triangles_adjacency", "triangle_strip", 14, 3, "2 * i"}};
        for (const AdjDef& def : adjacency) {
            Cases::single(prim(def.name).tolerance(Tolerance::raster(2, 1)), [def](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                char geometry[1024];
                std::snprintf(geometry, sizeof geometry, kAdjacencyGeometry, def.in, def.out, def.emit, def.index);
                const GLuint program = kit.program({{glc::VERTEX_SHADER, kAdjacencyVertex}, {glc::GEOMETRY_SHADER, geometry}, {glc::FRAGMENT_SHADER, kAdjacencyFragment}}, error);
                if (program == 0) {
                    return false;
                }
                kit.gl.glUseProgram(program);
                const GLuint vao = Flat2d::upload(kit, Flat2d::ring(def.count, 0.4f, 0.85f));
                kit.gl.glBindVertexArray(vao);
                kit.gl.glLineWidth(1.0f);
                kit.gl.glDrawArrays(def.mode, 0, static_cast<GLsizei>(def.count));
                return Gx::shot(kit, target, out, error);
            });
        }

        struct PatchDef {
            const char* name;
            unsigned vertices;
            const char* eval;
            bool wire;
            bool points;
        };
        const PatchDef patches[] = {{"patch_1_isolines", 1, kEvalIsolines, false, false}, {"patch_3_triangles", 3, kEvalTriangles, false, false},
                                    {"patch_3_points", 3, kEvalPoints, false, true},     {"patch_4_quads", 4, kEvalQuads, true, false},
                                    {"patch_16_bezier", 16, kEvalBezier, false, false},   {"patch_32_bezier_colors", 32, kEvalBezier, true, false}};
        for (const PatchDef& def : patches) {
            Cases::single(prim(def.name).tolerance(Tolerance::raster(2, 1)), [def](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                const std::string control = patch_control(def.vertices);
                const GLuint program = kit.program({{glc::VERTEX_SHADER, kPatchVertex}, {glc::TESS_CONTROL_SHADER, control.c_str()},
                                                    {glc::TESS_EVALUATION_SHADER, def.eval}, {glc::FRAGMENT_SHADER, kPatchFragment}}, error);
                if (program == 0) {
                    return false;
                }
                std::vector<V2> verts;
                if (def.vertices == 1) {
                    verts = {{0.0f, 0.0f, 0.9f, 0.6f, 0.2f}};
                } else if (def.vertices == 3) {
                    verts = {{-0.8f, -0.7f, 1, 0, 0}, {0.8f, -0.6f, 0, 1, 0}, {0.0f, 0.8f, 0, 0, 1}};
                } else if (def.vertices == 4) {
                    verts = {{-0.8f, -0.8f, 1, 0, 0}, {0.7f, -0.6f, 0, 1, 0}, {0.8f, 0.8f, 0, 0, 1}, {-0.6f, 0.7f, 1, 1, 0}};
                } else {
                    for (unsigned j = 0; j < 4; ++j) {
                        for (unsigned i = 0; i < 4; ++i) {
                            const float x = -0.8f + 1.6f * static_cast<float>(i) / 3.0f;
                            const float y = -0.8f + 1.6f * static_cast<float>(j) / 3.0f;
                            const float bulge = (i == 1 || i == 2) && (j == 1 || j == 2) ? 0.5f : 0.0f;
                            verts.push_back({x * (1 + bulge * 0.4f), y + (i == 1 ? 0.25f : (i == 2 ? -0.25f : 0.0f)), 0.2f + 0.2f * static_cast<float>(i), 0.3f + 0.2f * static_cast<float>(j), 0.9f - 0.2f * static_cast<float>(i)});
                        }
                    }
                    for (unsigned k = 16; k < def.vertices; ++k) {
                        verts.push_back({0, 0, static_cast<float>(k % 4) / 3.0f, static_cast<float>(k % 5) / 4.0f, 1.0f - static_cast<float>(k % 3) / 2.0f});
                    }
                    if (def.vertices == 32) {
                        for (unsigned k = 0; k < 16; ++k) {
                            verts[k].r = static_cast<float>(k % 4) / 3.0f;
                        }
                    }
                }
                kit.gl.glUseProgram(program);
                const GLuint vao = Flat2d::upload(kit, verts);
                kit.gl.glBindVertexArray(vao);
                kit.gl.glPatchParameteri(glc::PATCH_VERTICES, static_cast<GLint>(def.vertices));
                kit.gl.glEnable(glc::PROGRAM_POINT_SIZE);
                if (def.wire) {
                    kit.gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
                }
                kit.gl.glDrawArrays(glc::PATCHES, 0, static_cast<GLsizei>(verts.size()));
                return Gx::shot(kit, target, out, error);
            });
        }
    }
} kBatch5;

}  // namespace

}  // namespace lsw::render::gl
