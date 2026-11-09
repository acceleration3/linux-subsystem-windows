#include "render/gl/legacy.hpp"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec compat(const char* name) { return CaseSpec("compat", name).compat_profile(); }

NeedFn needs(std::initializer_list<const char*> extensions) {
    std::vector<std::string> list(extensions.begin(), extensions.end());
    return [list](Kit& kit) -> std::string {
        for (const std::string& e : list) {
            if (!kit.has_extension(e.c_str())) {
                return e + " is not exposed";
            }
        }
        return {};
    };
}

const char* kLegacyVertex110 =
    "#version 110\n"
    "varying vec3 normal;\n"
    "varying vec3 eye;\n"
    "void main() {\n"
    "    normal = gl_NormalMatrix * gl_Normal;\n"
    "    eye = vec3(gl_ModelViewMatrix * gl_Vertex);\n"
    "    gl_TexCoord[0] = gl_TextureMatrix[0] * gl_MultiTexCoord0;\n"
    "    gl_FrontColor = gl_Color;\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

const char* kLegacyFragment110 =
    "#version 110\n"
    "uniform sampler2D tex;\n"
    "varying vec3 normal;\n"
    "varying vec3 eye;\n"
    "void main() {\n"
    "    vec3 l = normalize(gl_LightSource[0].position.xyz - eye * gl_LightSource[0].position.w);\n"
    "    float d = max(dot(normalize(normal), l), 0.0);\n"
    "    vec4 t = texture2D(tex, gl_TexCoord[0].st);\n"
    "    gl_FragColor = vec4(t.rgb * gl_Color.rgb * (0.2 + 0.8 * d) * gl_FrontMaterial.diffuse.rgb, 1.0);\n"
    "}\n";

const char* kLegacyVertex120 =
    "#version 120\n"
    "attribute float weight;\n"
    "varying float fog_amount;\n"
    "void main() {\n"
    "    vec4 p = gl_Vertex;\n"
    "    p.xyz += gl_Normal * weight * 0.15;\n"
    "    vec4 e = gl_ModelViewMatrix * p;\n"
    "    gl_FogFragCoord = abs(e.z);\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "    gl_FrontColor = vec4(gl_Normal * 0.5 + 0.5, 1.0);\n"
    "    gl_Position = gl_ProjectionMatrix * e;\n"
    "}\n";

const char* kLegacyFragment120 =
    "#version 120\n"
    "uniform sampler2D tex;\n"
    "void main() {\n"
    "    float f = clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0);\n"
    "    vec4 c = texture2D(tex, gl_TexCoord[0].st) * gl_Color;\n"
    "    gl_FragColor = mix(gl_Fog.color, c, f);\n"
    "}\n";

const char* kCompatibilityVertex =
    "#version 450 compatibility\n"
    "layout(location = 0) in vec4 in_unused;\n"
    "out vec4 v_color;\n"
    "void main() {\n"
    "    v_color = gl_Color * 0.5 + 0.5 * vec4(gl_Vertex.xy * 0.5 + 0.5, 0.3, 1.0);\n"
    "    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
    "}\n";

const char* kCompatibilityFragment =
    "#version 450 compatibility\n"
    "in vec4 v_color;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() { out_color = v_color; }\n";

const char* kArbVertex =
    "!!ARBvp1.0\n"
    "ATTRIB pos = vertex.position;\n"
    "ATTRIB col = vertex.color;\n"
    "ATTRIB tc = vertex.texcoord[0];\n"
    "PARAM mvp[4] = { state.matrix.mvp };\n"
    "PARAM scale = program.local[0];\n"
    "TEMP p;\n"
    "MUL p, pos, scale;\n"
    "DP4 result.position.x, mvp[0], p;\n"
    "DP4 result.position.y, mvp[1], p;\n"
    "DP4 result.position.z, mvp[2], p;\n"
    "DP4 result.position.w, mvp[3], p;\n"
    "MOV result.color, col;\n"
    "MOV result.texcoord[0], tc;\n"
    "END\n";

const char* kArbFragment =
    "!!ARBfp1.0\n"
    "TEMP t;\n"
    "PARAM tint = program.env[1];\n"
    "TEX t, fragment.texcoord[0], texture[0], 2D;\n"
    "MUL t, t, fragment.color;\n"
    "MUL result.color, t, tint;\n"
    "END\n";

GLuint arb_program(Kit& kit, GLenum target, const char* text, std::string& error) {
    Api& gl = kit.gl;
    GLuint id = 0;
    gl.glGenProgramsARB(1, &id);
    gl.glBindProgramARB(target, id);
    gl.glProgramStringARB(target, glc::PROGRAM_FORMAT_ASCII_ARB, static_cast<GLsizei>(std::strlen(text)), text);
    if (gl.glGetError() != glc::NO_ERROR_) {
        error = "ARB program failed to load";
        return 0;
    }
    kit.defer([&kit, id] { kit.gl.glDeleteProgramsARB(1, &id); });
    return id;
}

const struct Batch4 {
    Batch4() {
        Cases::single(compat("clip_planes"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            Legacy::perspective(kit, 25.0f, 20.0f, 4.0f);
            gl.glEnable(glc::DEPTH_TEST);
            const double p0[4] = {1, 0.5, 0, 0.3};
            const double p1[4] = {-0.3, 1, 0.4, 0.5};
            gl.glClipPlane(glc::CLIP_PLANE0, p0);
            gl.glClipPlane(glc::CLIP_PLANE1, p1);
            gl.glEnable(glc::CLIP_PLANE0);
            gl.glEnable(glc::CLIP_PLANE1);
            gl.glColor3f(0.9f, 0.6f, 0.2f);
            Legacy::draw_shape(kit, Shape::torus(24, 12, 0.8f, 0.4f), false);
            gl.glDisable(glc::CLIP_PLANE0);
            gl.glDisable(glc::CLIP_PLANE1);
            return Gx::shot(kit, target, out, error);
        });

        struct AlphaDef {
            const char* name;
            GLenum func;
            float ref;
        };
        for (const AlphaDef& def : {AlphaDef{"alpha_test_greater", glc::GREATER, 0.5f}, AlphaDef{"alpha_test_less", glc::LESS, 0.3f},
                                    AlphaDef{"alpha_test_gequal", glc::GEQUAL, 0.8f}, AlphaDef{"alpha_test_notequal", glc::NOTEQUAL, 0.5f}}) {
            Cases::single(compat(def.name), [def](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                gl.glBindTexture(glc::TEXTURE_2D, def.func == glc::NOTEQUAL ? Legacy::alpha_steps_texture(kit) : Legacy::alpha_texture(kit));
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::REPLACE));
                gl.glEnable(glc::ALPHA_TEST);
                gl.glAlphaFunc(def.func, def.ref);
                gl.glBegin(glc::QUADS);
                gl.glTexCoord2f(0, 0);
                gl.glVertex2f(-0.9f, -0.9f);
                gl.glTexCoord2f(1, 0);
                gl.glVertex2f(0.9f, -0.9f);
                gl.glTexCoord2f(1, 1);
                gl.glVertex2f(0.9f, 0.9f);
                gl.glTexCoord2f(0, 1);
                gl.glVertex2f(-0.9f, 0.9f);
                gl.glEnd();
                return Gx::shot(kit, target, out, error);
            });
        }

        Cases::single(compat("polygon_stipple"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            GLubyte mask[128];
            for (int row = 0; row < 32; ++row) {
                for (int col = 0; col < 4; ++col) {
                    mask[row * 4 + col] = static_cast<GLubyte>(((row / 4) % 2) ? 0xF0 : 0x0F) ^ static_cast<GLubyte>(row % 3 == 0 ? 0xFF : 0);
                }
            }
            gl.glPolygonStipple(mask);
            gl.glEnable(glc::POLYGON_STIPPLE);
            gl.glColor3f(0.9f, 0.8f, 0.2f);
            gl.glRectf(-0.9f, -0.9f, 0.3f, 0.5f);
            gl.glColor3f(0.2f, 0.8f, 0.9f);
            gl.glBegin(glc::TRIANGLES);
            gl.glVertex2f(-0.2f, -0.2f);
            gl.glVertex2f(0.9f, -0.5f);
            gl.glVertex2f(0.5f, 0.9f);
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });

        struct StippleDef {
            const char* name;
            int factor;
            unsigned pattern;
            float width;
        };
        for (const StippleDef& def : {StippleDef{"line_stipple_1_0f0f", 1, 0x0F0F, 1.0f}, StippleDef{"line_stipple_3_aaaa", 3, 0xAAAA, 1.0f},
                                      StippleDef{"line_stipple_2_00ff_wide", 2, 0x00FF, 3.0f}, StippleDef{"line_stipple_4_3333", 4, 0x3333, 2.0f}}) {
            Cases::single(compat(def.name).tolerance(Tolerance::raster(2, 1)), [def](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                gl.glEnable(glc::LINE_STIPPLE);
                gl.glLineStipple(def.factor, static_cast<GLushort>(def.pattern));
                gl.glLineWidth(def.width);
                for (int i = 0; i < 10; ++i) {
                    float c[3];
                    Legacy::rainbow(static_cast<unsigned>(i), 10, c);
                    gl.glColor3fv(c);
                    gl.glBegin(glc::LINE_STRIP);
                    gl.glVertex2f(-0.9f, -0.9f + 0.18f * static_cast<float>(i));
                    gl.glVertex2f(0.0f, -0.5f + 0.1f * static_cast<float>(i));
                    gl.glVertex2f(0.9f, -0.9f + 0.18f * static_cast<float>(10 - i) * 0.5f);
                    gl.glEnd();
                }
                return Gx::shot(kit, target, out, error);
            });
        }

        Cases::single(compat("wide_lines").tolerance(Tolerance::raster(2, 1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const float widths[] = {1, 2, 3, 4, 6, 8};
            for (int i = 0; i < 6; ++i) {
                gl.glLineWidth(widths[i]);
                gl.glColor3f(0.4f + 0.1f * static_cast<float>(i), 0.9f - 0.1f * static_cast<float>(i), 0.5f);
                gl.glBegin(glc::LINES);
                gl.glVertex2f(-0.9f, -0.8f + 0.3f * static_cast<float>(i));
                gl.glVertex2f(0.9f, -0.7f + 0.3f * static_cast<float>(i));
                gl.glEnd();
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("smooth_lines_blend").tolerance(Tolerance::tight(1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            gl.glEnable(glc::LINE_SMOOTH);
            gl.glHint(glc::LINE_SMOOTH_HINT, glc::NICEST);
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
            gl.glLineWidth(2.5f);
            gl.glColor4f(1, 0.8f, 0.2f, 1);
            for (int i = 0; i < 14; ++i) {
                const float a = 0.22f * static_cast<float>(i);
                gl.glBegin(glc::LINES);
                gl.glVertex2f(0, 0);
                gl.glVertex2f(0.9f * std::cos(a), 0.9f * std::sin(a));
                gl.glEnd();
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("point_sprites").tolerance(Tolerance::raster(2, 1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            gl.glBindTexture(glc::TEXTURE_2D, Legacy::alpha_texture(kit));
            gl.glEnable(glc::TEXTURE_2D);
            gl.glEnable(glc::POINT_SPRITE);
            gl.glTexEnvi(glc::POINT_SPRITE, glc::COORD_REPLACE, 1);
            gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::MODULATE));
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
            gl.glPointSize(28.0f);
            gl.glBegin(glc::POINTS);
            for (int i = 0; i < 6; ++i) {
                float c[3];
                Legacy::rainbow(static_cast<unsigned>(i), 6, c);
                gl.glColor3fv(c);
                gl.glVertex2f(-0.7f + 0.28f * static_cast<float>(i), -0.5f + 0.2f * static_cast<float>(i % 3));
            }
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("point_parameters").tolerance(Tolerance::raster(2, 1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            Legacy::perspective(kit, 40.0f, 0.0f, 3.0f);
            const float attenuation[3] = {0.0f, 0.0f, 0.08f};
            gl.glPointParameterfv(glc::POINT_DISTANCE_ATTENUATION, attenuation);
            gl.glPointParameterf(glc::POINT_SIZE_MIN, 3.0f);
            gl.glPointParameterf(glc::POINT_SIZE_MAX, 22.0f);
            gl.glPointSize(40.0f);
            gl.glBegin(glc::POINTS);
            for (int i = 0; i < 12; ++i) {
                float c[3];
                Legacy::rainbow(static_cast<unsigned>(i), 12, c);
                gl.glColor3fv(c);
                gl.glVertex3f(-0.9f + 0.16f * static_cast<float>(i), -0.8f + 0.12f * static_cast<float>(i), -0.8f * static_cast<float>(i));
            }
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("draw_pixels_zoom"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const Pattern::Bytes pixels = Pattern::checker(32, 32, 4, Pattern::pack(250, 120, 40), Pattern::pack(30, 60, 200));
            gl.glRasterPos2f(-0.9f, -0.9f);
            gl.glDrawPixels(32, 32, glc::RGBA, glc::UNSIGNED_BYTE, pixels.data());
            gl.glPixelZoom(2.0f, 2.0f);
            gl.glRasterPos2f(-0.2f, -0.2f);
            gl.glDrawPixels(32, 32, glc::RGBA, glc::UNSIGNED_BYTE, pixels.data());
            gl.glPixelZoom(1.0f, -1.0f);
            gl.glPixelTransferf(glc::RED_SCALE, 0.5f);
            gl.glRasterPos2f(0.4f, 0.9f);
            gl.glDrawPixels(32, 32, glc::RGBA, glc::UNSIGNED_BYTE, pixels.data());
            gl.glPixelTransferf(glc::RED_SCALE, 1.0f);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("bitmap_text"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLubyte glyph[16] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x7E, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00};
            gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
            for (int row = 0; row < 3; ++row) {
                gl.glColor3f(1.0f - 0.3f * static_cast<float>(row), 0.5f + 0.2f * static_cast<float>(row), 0.3f);
                gl.glRasterPos2f(-0.9f, 0.6f - 0.5f * static_cast<float>(row));
                for (int i = 0; i < 10; ++i) {
                    gl.glBitmap(8, 16, 0, 0, 12.0f, 0, glyph);
                }
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("copy_pixels"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            gl.glBegin(glc::TRIANGLES);
            gl.glColor3f(1, 0, 0);
            gl.glVertex2f(-0.9f, -0.9f);
            gl.glColor3f(0, 1, 0);
            gl.glVertex2f(-0.1f, -0.9f);
            gl.glColor3f(0, 0, 1);
            gl.glVertex2f(-0.5f, -0.1f);
            gl.glEnd();
            gl.glRasterPos2f(0.1f, -0.9f);
            gl.glCopyPixels(4, 4, 56, 56, glc::COLOR);
            gl.glPixelZoom(0.5f, 0.5f);
            gl.glRasterPos2f(0.1f, 0.1f);
            gl.glCopyPixels(4, 4, 56, 56, glc::COLOR);
            gl.glPixelZoom(1, 1);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("rect_and_attrib_stack"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            gl.glColor3f(0.2f, 0.9f, 0.4f);
            gl.glRectf(-0.9f, -0.9f, -0.1f, -0.1f);
            gl.glPushAttrib(glc::ALL_ATTRIB_BITS);
            gl.glColor3f(0.9f, 0.2f, 0.4f);
            gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
            gl.glLineWidth(3);
            gl.glEnable(glc::SCISSOR_TEST);
            gl.glScissor(0, 0, 100, 128);
            gl.glRecti(0, 0, 1, 1);
            gl.glPopAttrib();
            gl.glRectf(0.1f, -0.9f, 0.9f, -0.1f);
            gl.glRectf(-0.9f, 0.1f, 0.9f, 0.9f);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("evaluator_curve"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const float points[4][3] = {{-0.9f, -0.7f, 0}, {-0.5f, 1.2f, 0}, {0.5f, -1.4f, 0}, {0.9f, 0.7f, 0}};
            gl.glMap1f(glc::MAP1_VERTEX_3, 0, 1, 3, 4, &points[0][0]);
            gl.glEnable(glc::MAP1_VERTEX_3);
            gl.glColor3f(0.9f, 0.9f, 0.3f);
            gl.glLineWidth(2);
            gl.glMapGrid1f(24, 0, 1);
            gl.glEvalMesh1(glc::LINE, 0, 24);
            gl.glColor3f(0.9f, 0.3f, 0.3f);
            gl.glPointSize(5);
            gl.glBegin(glc::POINTS);
            for (int i = 0; i <= 8; ++i) {
                gl.glEvalCoord1f(static_cast<float>(i) / 8.0f);
            }
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("evaluator_surface").tolerance(Tolerance::tight(2)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            Legacy::perspective(kit, 40.0f, 20.0f, 4.0f);
            gl.glEnable(glc::DEPTH_TEST);
            float grid[4][4][3];
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    grid[i][j][0] = -1.0f + 2.0f * static_cast<float>(i) / 3.0f;
                    grid[i][j][1] = (i == 1 || i == 2) && (j == 1 || j == 2) ? 1.2f : 0.0f;
                    grid[i][j][2] = -1.0f + 2.0f * static_cast<float>(j) / 3.0f;
                }
            }
            gl.glMap2f(glc::MAP2_VERTEX_3, 0, 1, 3, 4, 0, 1, 12, 4, &grid[0][0][0]);
            gl.glEnable(glc::MAP2_VERTEX_3);
            gl.glColor3f(0.3f, 0.8f, 0.9f);
            gl.glMapGrid2f(12, 0, 1, 12, 0, 1);
            gl.glEvalMesh2(glc::FILL, 0, 12, 0, 12);
            gl.glColor3f(1.0f, 0.9f, 0.2f);
            gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
            gl.glTranslatef(0.0f, 0.02f, 0.0f);
            gl.glEvalMesh2(glc::LINE, 0, 12, 0, 12);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("arb_vertex_fragment_program"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint vp = arb_program(kit, glc::VERTEX_PROGRAM_ARB, kArbVertex, error);
            if (vp == 0) {
                return false;
            }
            gl.glProgramLocalParameter4fARB(glc::VERTEX_PROGRAM_ARB, 0, 0.9f, 0.8f, 1.0f, 1.0f);
            const GLuint fp = arb_program(kit, glc::FRAGMENT_PROGRAM_ARB, kArbFragment, error);
            if (fp == 0) {
                return false;
            }
            gl.glProgramEnvParameter4fARB(glc::FRAGMENT_PROGRAM_ARB, 1, 1.0f, 0.9f, 0.8f, 1.0f);
            gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, 0, false));
            gl.glEnable(glc::TEXTURE_2D);
            gl.glEnable(glc::VERTEX_PROGRAM_ARB);
            gl.glEnable(glc::FRAGMENT_PROGRAM_ARB);
            gl.glBindProgramARB(glc::VERTEX_PROGRAM_ARB, vp);
            gl.glBindProgramARB(glc::FRAGMENT_PROGRAM_ARB, fp);
            gl.glBegin(glc::QUADS);
            const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int i = 0; i < 4; ++i) {
                gl.glColor3f(i == 0 ? 1.0f : 0.5f, i == 1 ? 1.0f : 0.5f, i == 2 ? 1.0f : 0.7f);
                gl.glTexCoord2f(corners[i][0] * 0.5f + 0.5f, corners[i][1] * 0.5f + 0.5f);
                gl.glVertex2f(corners[i][0], corners[i][1]);
            }
            gl.glEnd();
            gl.glDisable(glc::VERTEX_PROGRAM_ARB);
            gl.glDisable(glc::FRAGMENT_PROGRAM_ARB);
            return Gx::shot(kit, target, out, error);
        }, needs({"GL_ARB_vertex_program", "GL_ARB_fragment_program"}));

        Cases::single(compat("glsl110_builtins"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint program = kit.program(kLegacyVertex110, kLegacyFragment110, error);
            if (program == 0) {
                return false;
            }
            Legacy::perspective(kit, 25.0f, 30.0f);
            gl.glEnable(glc::DEPTH_TEST);
            const float pos[4] = {2, 2, 2, 1};
            const float white[4] = {1, 1, 1, 1};
            const float diffuse[4] = {0.9f, 0.7f, 0.5f, 1};
            gl.glLightfv(glc::LIGHT0, glc::POSITION, pos);
            gl.glMaterialfv(glc::FRONT_AND_BACK, glc::DIFFUSE, diffuse);
            gl.glColor4fv(white);
            gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, 0, false));
            gl.glUseProgram(program);
            kit.u1i(program, "tex", 0);
            Legacy::draw_shape(kit, Shape::sphere(24, 32, 1.1f), true, true);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("glsl120_attributes_fog"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint vs = kit.shader(glc::VERTEX_SHADER, kLegacyVertex120, error);
            const GLuint fs = vs != 0 ? kit.shader(glc::FRAGMENT_SHADER, kLegacyFragment120, error) : 0;
            if (fs == 0) {
                return false;
            }
            const GLuint program = gl.glCreateProgram();
            gl.glAttachShader(program, vs);
            gl.glAttachShader(program, fs);
            gl.glBindAttribLocation(program, 6, "weight");
            gl.glLinkProgram(program);
            GLint ok = 0;
            gl.glGetProgramiv(program, glc::LINK_STATUS, &ok);
            if (ok == 0) {
                error = "legacy program failed to link";
                return false;
            }
            Legacy::perspective(kit, 25.0f, 30.0f, 4.0f);
            gl.glEnable(glc::DEPTH_TEST);
            const float fog_color[4] = {0.5f, 0.55f, 0.65f, 1};
            gl.glFogfv(glc::FOG_COLOR, fog_color);
            gl.glFogf(glc::FOG_START, 2.5f);
            gl.glFogf(glc::FOG_END, 6.0f);
            gl.glClearColor(0.5f, 0.55f, 0.65f, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
            gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, 3, false));
            gl.glUseProgram(program);
            kit.u1i(program, "tex", 0);
            const Shape shape = Shape::torus(24, 12, 0.8f, 0.4f);
            std::vector<float> weights(shape.vertices.size() / 8);
            for (std::size_t i = 0; i < weights.size(); ++i) {
                weights[i] = static_cast<float>(i % 5) / 4.0f;
            }
            gl.glEnableVertexAttribArray(6);
            gl.glVertexAttribPointer(6, 1, glc::FLOAT, glc::FALSE_, 0, weights.data());
            Legacy::draw_shape(kit, shape, true, true);
            gl.glDisableVertexAttribArray(6);
            gl.glUseProgram(0);
            gl.glDeleteProgram(program);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("glsl_compatibility_profile_source"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint program = kit.program(kCompatibilityVertex, kCompatibilityFragment, error);
            if (program == 0) {
                return false;
            }
            gl.glUseProgram(program);
            gl.glRotatef(15, 0, 0, 1);
            gl.glBegin(glc::TRIANGLE_STRIP);
            const float pts[6][2] = {{-0.8f, -0.8f}, {-0.8f, 0.8f}, {0.0f, -0.6f}, {0.0f, 0.9f}, {0.8f, -0.8f}, {0.8f, 0.7f}};
            for (int i = 0; i < 6; ++i) {
                gl.glColor3f(i % 2 ? 1.0f : 0.2f, 0.5f, i % 2 ? 0.2f : 1.0f);
                gl.glVertex2f(pts[i][0], pts[i][1]);
            }
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });
    }
} kBatch4;

}  // namespace

}  // namespace lsw::render::gl
