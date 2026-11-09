#include "render/gl/flat2d.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec stage(const std::string& name) { return CaseSpec("stages", name).min_version(4, 6); }

const char* kVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "void main() { gl_Position = vec4(in_pos, 0.0, 1.0); }\n";

const char* kControl =
    "#version 460 core\n"
    "layout(vertices = 4) out;\n"
    "void main() {\n"
    "    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
    "    if (gl_InvocationID == 0) {\n"
    "        gl_TessLevelOuter[0] = 3.3; gl_TessLevelOuter[1] = 2.7; gl_TessLevelOuter[2] = 4.6; gl_TessLevelOuter[3] = 3.1;\n"
    "        gl_TessLevelInner[0] = 2.4; gl_TessLevelInner[1] = 3.6;\n"
    "    }\n"
    "}\n";

const char* kControlIso =
    "#version 460 core\n"
    "layout(vertices = 4) out;\n"
    "void main() {\n"
    "    gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;\n"
    "    if (gl_InvocationID == 0) {\n"
    "        gl_TessLevelOuter[0] = 3.2; gl_TessLevelOuter[1] = 7.4;\n"
    "    }\n"
    "}\n";

std::string evaluation(const std::string& domain, const std::string& spacing, const std::string& winding, bool points) {
    std::string layout = "layout(" + domain + ", " + spacing + ", " + winding + (points ? ", point_mode" : "") + ") in;\n";
    std::string body;
    if (domain == "triangles") {
        body = "    vec3 w = gl_TessCoord;\n"
               "    gl_Position = w.x * gl_in[0].gl_Position + w.y * gl_in[1].gl_Position + w.z * gl_in[2].gl_Position;\n"
               "    uv = w.xy;\n";
    } else if (domain == "quads") {
        body = "    vec2 t = gl_TessCoord.xy;\n"
               "    gl_Position = mix(mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x), mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x), t.y);\n"
               "    uv = t;\n";
    } else {
        body = "    float u = gl_TessCoord.x;\n"
               "    float v = gl_TessCoord.y;\n"
               "    vec4 a = mix(gl_in[0].gl_Position, gl_in[1].gl_Position, u);\n"
               "    vec4 b = mix(gl_in[3].gl_Position, gl_in[2].gl_Position, u);\n"
               "    gl_Position = mix(a, b, v) + vec4(0.0, 0.15 * sin(u * 6.2831853), 0.0, 0.0);\n"
               "    uv = vec2(u, v);\n";
    }
    return "#version 460 core\n" + layout + "out vec2 uv;\nvoid main() {\n" + body + "    gl_PointSize = 6.0;\n}\n";
}

const char* kFragment =
    "#version 460 core\n"
    "in vec2 uv;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec3 c = vec3(uv, 1.0 - uv.x);\n"
    "    out_color = vec4(gl_FrontFacing ? c : c.bgr * vec3(0.5, 0.5, 1.0), 1.0);\n"
    "}\n";

struct TessRun {
    std::string domain, spacing, winding;
    bool points;
    bool tcs;
};

bool run_tess(Kit& kit, const TessRun& run, CaptureSet& out, std::string& error) {
    Target target;
    if (!target.create(kit, TargetDesc{}, error)) {
        return false;
    }
    target.bind(kit);
    Gx::clear(kit, 0.08f, 0.08f, 0.12f);
    Api& gl = kit.gl;
    const std::string eval = evaluation(run.domain, run.spacing, run.winding, run.points);
    const bool iso = run.domain == "isolines";
    const GLuint program = kit.program({{glc::VERTEX_SHADER, kVertex}, {glc::TESS_CONTROL_SHADER, iso ? kControlIso : kControl},
                                        {glc::TESS_EVALUATION_SHADER, eval.c_str()}, {glc::FRAGMENT_SHADER, kFragment}}, error);
    if (program == 0) {
        return false;
    }
    std::vector<V2> verts;
    GLint count = 4;
    if (run.domain == "triangles") {
        verts = {{-0.8f, -0.7f, 0, 0, 0}, {0.8f, -0.7f, 0, 0, 0}, {0.0f, 0.8f, 0, 0, 0}};
        count = 3;
    } else {
        verts = {{-0.8f, -0.7f, 0, 0, 0}, {0.8f, -0.6f, 0, 0, 0}, {0.7f, 0.7f, 0, 0, 0}, {-0.7f, 0.6f, 0, 0, 0}};
    }
    const GLuint vao = Flat2d::upload(kit, verts);
    gl.glUseProgram(program);
    gl.glBindVertexArray(vao);
    gl.glPatchParameteri(glc::PATCH_VERTICES, count);
    gl.glEnable(glc::PROGRAM_POINT_SIZE);
    if (!run.points && !iso) {
        gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
    }
    gl.glDrawArrays(glc::PATCHES, 0, count);
    return Gx::shot(kit, target, out, error);
}

const struct Batch12 {
    Batch12() {
        const char* domains[3] = {"triangles", "quads", "isolines"};
        const char* spacings[3] = {"equal_spacing", "fractional_even_spacing", "fractional_odd_spacing"};
        const char* windings[2] = {"cw", "ccw"};
        for (const char* domain : domains) {
            for (const char* spacing : spacings) {
                for (const char* winding : windings) {
                    if (std::string(domain) == "isolines" && std::string(winding) == "cw") {
                        continue;
                    }
                    const TessRun run{domain, spacing, winding, false, true};
                    const std::string name = std::string("tess_") + domain + "_" + (spacing + 0) + "_" + winding;
                    Cases::single(stage(name).tolerance(Tolerance::raster(2, 1)), [run](Kit& kit, CaptureSet& out, std::string& error) { return run_tess(kit, run, out, error); });
                }
            }
            const TessRun run{domain, "equal_spacing", "ccw", true, true};
            Cases::single(stage(std::string("tess_") + domain + "_point_mode").tolerance(Tolerance::raster(2, 1)),
                          [run](Kit& kit, CaptureSet& out, std::string& error) { return run_tess(kit, run, out, error); });
        }

        for (const bool quads : {false, true}) {
            Cases::single(stage(quads ? "tess_default_levels_quads" : "tess_default_levels_triangles").tolerance(Tolerance::raster(2, 1)),
                          [quads](Kit& kit, CaptureSet& out, std::string& error) {
                              Target target;
                              if (!target.create(kit, TargetDesc{}, error)) {
                                  return false;
                              }
                              target.bind(kit);
                              Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                              Api& gl = kit.gl;
                              const std::string eval = evaluation(quads ? "quads" : "triangles", "equal_spacing", "ccw", false);
                              const GLuint program = kit.program({{glc::VERTEX_SHADER, kVertex}, {glc::TESS_EVALUATION_SHADER, eval.c_str()}, {glc::FRAGMENT_SHADER, kFragment}}, error);
                              if (program == 0) {
                                  return false;
                              }
                              const float outer[4] = {5.0f, 3.0f, 4.0f, 6.0f};
                              const float inner[2] = {4.0f, 2.0f};
                              gl.glPatchParameterfv(glc::PATCH_DEFAULT_OUTER_LEVEL, outer);
                              gl.glPatchParameterfv(glc::PATCH_DEFAULT_INNER_LEVEL, inner);
                              const std::vector<V2> verts = quads ? std::vector<V2>{{-0.8f, -0.7f, 0, 0, 0}, {0.8f, -0.6f, 0, 0, 0}, {0.7f, 0.7f, 0, 0, 0}, {-0.7f, 0.6f, 0, 0, 0}}
                                                                  : std::vector<V2>{{-0.8f, -0.7f, 0, 0, 0}, {0.8f, -0.7f, 0, 0, 0}, {0.0f, 0.8f, 0, 0, 0}};
                              const GLuint vao = Flat2d::upload(kit, verts);
                              gl.glUseProgram(program);
                              gl.glBindVertexArray(vao);
                              gl.glPatchParameteri(glc::PATCH_VERTICES, static_cast<GLint>(verts.size()));
                              gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
                              gl.glDrawArrays(glc::PATCHES, 0, static_cast<GLsizei>(verts.size()));
                              return Gx::shot(kit, target, out, error);
                          });
        }
    }
} kBatch12;

}  // namespace

}  // namespace lsw::render::gl
