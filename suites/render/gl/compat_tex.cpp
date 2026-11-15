#include "render/gl/legacy.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec compat(const char* name) { return CaseSpec("compat", name).compat_profile(); }

void colored_quad(Kit& kit, float x0, float y0, float x1, float y1, unsigned units = 1) {
    Api& gl = kit.gl;
    const float u[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const float px[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float colors[4][4] = {{1, 0.3f, 0.2f, 0.9f}, {0.3f, 1, 0.2f, 0.7f}, {0.2f, 0.4f, 1, 0.5f}, {1, 1, 0.3f, 0.8f}};
    gl.glBegin(glc::QUADS);
    for (int i = 0; i < 4; ++i) {
        gl.glColor4fv(colors[i]);
        for (unsigned t = 0; t < units; ++t) {
            gl.glMultiTexCoord2f(glc::TEXTURE0 + t, u[i][0], u[i][1]);
        }
        gl.glVertex2f(px[i][0], px[i][1]);
    }
    gl.glEnd();
}

struct CombineDef {
    const char* name;
    GLenum func;
    GLenum src0, op0, src1, op1, src2, op2;
    float scale;
};

const struct Batch3 {
    Batch3() {
        const auto env_case = [](const char* name, GLenum mode) {
            Cases::single(compat(name).tolerance(Tolerance::tight(2)), [=](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                Gx::clear(kit, 0.3f, 0.3f, 0.35f);
                gl.glBindTexture(glc::TEXTURE_2D, Legacy::alpha_texture(kit));
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(mode));
                const float env[4] = {0.9f, 0.2f, 0.5f, 0.6f};
                gl.glTexEnvfv(glc::TEXTURE_ENV, glc::TEXTURE_ENV_COLOR, env);
                gl.glEnable(glc::BLEND);
                gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                colored_quad(kit, -0.9f, -0.9f, 0.9f, 0.9f);
                return Gx::shot(kit, target, out, error);
            });
        };
        env_case("env_modulate", glc::MODULATE);
        env_case("env_decal", glc::DECAL);
        env_case("env_blend", glc::BLEND);
        env_case("env_replace", glc::REPLACE);
        env_case("env_add", glc::ADD);

        const CombineDef combines[] = {
            {"combine_replace", glc::REPLACE, glc::TEXTURE, glc::SRC_COLOR, glc::PREVIOUS, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_modulate", glc::MODULATE, glc::PREVIOUS, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_modulate_scale4", glc::MODULATE, glc::TEXTURE, glc::SRC_COLOR, glc::PRIMARY_COLOR, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 4},
            {"combine_add", glc::ADD, glc::TEXTURE, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_add_signed", glc::ADD_SIGNED, glc::TEXTURE, glc::SRC_COLOR, glc::PRIMARY_COLOR, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_interpolate", glc::INTERPOLATE, glc::TEXTURE, glc::SRC_COLOR, glc::PRIMARY_COLOR, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_COLOR, 1},
            {"combine_interpolate_texalpha", glc::INTERPOLATE, glc::CONSTANT, glc::SRC_COLOR, glc::TEXTURE, glc::SRC_COLOR, glc::TEXTURE, glc::SRC_ALPHA, 1},
            {"combine_subtract", glc::SUBTRACT, glc::TEXTURE, glc::SRC_COLOR, glc::PRIMARY_COLOR, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_dot3_rgb", glc::DOT3_RGB, glc::TEXTURE, glc::SRC_COLOR, glc::PRIMARY_COLOR, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_dot3_rgba", glc::DOT3_RGBA, glc::TEXTURE, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_COLOR, glc::CONSTANT, glc::SRC_ALPHA, 1},
            {"combine_oneminus_operands", glc::MODULATE, glc::TEXTURE, glc::ONE_MINUS_SRC_COLOR, glc::PRIMARY_COLOR, glc::ONE_MINUS_SRC_ALPHA, glc::CONSTANT, glc::SRC_ALPHA, 2},
        };
        for (const CombineDef& def : combines) {
            Cases::single(compat(def.name).tolerance(Tolerance::tight(2)), [def](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                Gx::clear(kit, 0.3f, 0.3f, 0.35f);
                gl.glBindTexture(glc::TEXTURE_2D, Legacy::alpha_texture(kit));
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::COMBINE));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::COMBINE_RGB, static_cast<GLint>(def.func));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::SOURCE0_RGB, static_cast<GLint>(def.src0));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::OPERAND0_RGB, static_cast<GLint>(def.op0));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::SOURCE1_RGB, static_cast<GLint>(def.src1));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::OPERAND1_RGB, static_cast<GLint>(def.op1));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::SOURCE2_RGB, static_cast<GLint>(def.src2));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::OPERAND2_RGB, static_cast<GLint>(def.op2));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::COMBINE_ALPHA, static_cast<GLint>(glc::MODULATE));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::SOURCE0_ALPHA, static_cast<GLint>(glc::TEXTURE));
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::SOURCE1_ALPHA, static_cast<GLint>(glc::PRIMARY_COLOR));
                gl.glTexEnvf(glc::TEXTURE_ENV, glc::RGB_SCALE, def.scale);
                const float env[4] = {0.7f, 0.4f, 0.2f, 0.5f};
                gl.glTexEnvfv(glc::TEXTURE_ENV, glc::TEXTURE_ENV_COLOR, env);
                gl.glEnable(glc::BLEND);
                gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                colored_quad(kit, -0.9f, -0.9f, 0.9f, 0.9f);
                return Gx::shot(kit, target, out, error);
            });
        }

        Cases::single(compat("multitexture_four_units").tolerance(Tolerance::tight(2)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLenum modes[4] = {glc::REPLACE, glc::MODULATE, glc::ADD, glc::MODULATE};
            for (unsigned unit = 0; unit < 4; ++unit) {
                gl.glActiveTexture(glc::TEXTURE0 + unit);
                gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, unit, false));
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(modes[unit]));
                gl.glMatrixMode(glc::TEXTURE);
                gl.glLoadIdentity();
                gl.glScalef(1.0f + static_cast<float>(unit), 1.0f + static_cast<float>(unit), 1.0f);
                gl.glMatrixMode(glc::MODELVIEW);
            }
            colored_quad(kit, -0.9f, -0.9f, 0.9f, 0.9f, 4);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("multitexture_client_arrays").tolerance(Tolerance::tight(2)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const float pos[] = {-0.9f, -0.9f, 0.9f, -0.9f, 0.9f, 0.9f, -0.9f, 0.9f};
            const float uv0[] = {0, 0, 1, 0, 1, 1, 0, 1};
            const float uv1[] = {0, 0, 3, 0, 3, 3, 0, 3};
            for (unsigned unit = 0; unit < 2; ++unit) {
                gl.glActiveTexture(glc::TEXTURE0 + unit);
                gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, unit * 3, false));
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(unit == 0 ? glc::REPLACE : glc::MODULATE));
                gl.glClientActiveTexture(glc::TEXTURE0 + unit);
                gl.glEnableClientState(glc::TEXTURE_COORD_ARRAY);
                gl.glTexCoordPointer(2, glc::FLOAT, 0, unit == 0 ? uv0 : uv1);
            }
            gl.glEnableClientState(glc::VERTEX_ARRAY);
            gl.glVertexPointer(2, glc::FLOAT, 0, pos);
            gl.glDrawArrays(glc::QUADS, 0, 4);
            return Gx::shot(kit, target, out, error);
        });

        struct TexGenDef {
            const char* name;
            GLenum mode;
        };
        const TexGenDef gens[] = {{"texgen_object_linear", glc::OBJECT_LINEAR}, {"texgen_eye_linear", glc::EYE_LINEAR},
                                  {"texgen_sphere_map", glc::SPHERE_MAP},       {"texgen_reflection_map", glc::REFLECTION_MAP},
                                  {"texgen_normal_map", glc::NORMAL_MAP}};
        for (const TexGenDef& def : gens) {
            Cases::single(compat(def.name).tolerance(Tolerance::tight(2)), [def](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                gl.glEnable(glc::DEPTH_TEST);
                gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, 0, false));
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::REPLACE));
                const bool planes = def.mode == glc::OBJECT_LINEAR || def.mode == glc::EYE_LINEAR;
                const float sp[4] = {0.8f, 0.0f, 0.3f, 0.5f};
                const float tp[4] = {0.0f, 0.8f, 0.3f, 0.5f};
                for (GLenum coord : {glc::S, glc::T}) {
                    gl.glTexGeni(coord, glc::TEXTURE_GEN_MODE, static_cast<GLint>(def.mode));
                    if (planes) {
                        gl.glTexGenfv(coord, def.mode == glc::OBJECT_LINEAR ? glc::OBJECT_PLANE : glc::EYE_PLANE, coord == glc::S ? sp : tp);
                    }
                }
                gl.glEnable(glc::TEXTURE_GEN_S);
                gl.glEnable(glc::TEXTURE_GEN_T);
                gl.glEnable(glc::NORMALIZE);
                Legacy::perspective(kit, 20.0f, 25.0f);
                Legacy::draw_shape(kit, Shape::sphere(20, 28, 1.1f));
                return Gx::shot(kit, target, out, error);
            });
        }

        Cases::single(compat("texture_matrix_rotate").tolerance(Tolerance::tight(2)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            gl.glBindTexture(glc::TEXTURE_2D, Gx::make_texture(kit, 64, 3, false));
            gl.glEnable(glc::TEXTURE_2D);
            gl.glMatrixMode(glc::TEXTURE);
            gl.glLoadIdentity();
            gl.glTranslatef(0.5f, 0.5f, 0);
            gl.glRotatef(30, 0, 0, 1);
            gl.glScalef(2, 2, 1);
            gl.glTranslatef(-0.5f, -0.5f, 0);
            gl.glMatrixMode(glc::MODELVIEW);
            gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::REPLACE));
            colored_quad(kit, -0.9f, -0.9f, 0.9f, 0.9f);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("matrix_stack_nested"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            gl.glScalef(0.8f, 0.8f, 1.0f);
            for (int level = 0; level < 6; ++level) {
                gl.glPushMatrix();
                gl.glRotatef(12.0f * static_cast<float>(level), 0, 0, 1);
                gl.glTranslatef(0.1f * static_cast<float>(level), -0.1f * static_cast<float>(level), 0);
                gl.glScalef(1.0f - 0.12f * static_cast<float>(level), 1.0f - 0.12f * static_cast<float>(level), 1.0f);
                float c[3];
                Legacy::rainbow(static_cast<unsigned>(level), 6, c);
                gl.glColor3fv(c);
                gl.glRectf(-0.5f, -0.5f, 0.5f, 0.5f);
            }
            for (int level = 0; level < 6; ++level) {
                gl.glPopMatrix();
            }
            gl.glColor3f(1, 1, 1);
            gl.glBegin(glc::LINE_LOOP);
            gl.glVertex2f(-0.95f, -0.95f);
            gl.glVertex2f(0.95f, -0.95f);
            gl.glVertex2f(0.95f, 0.95f);
            gl.glVertex2f(-0.95f, 0.95f);
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("load_and_mult_matrix"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const Mat4 rot = Mat4::rotate_z(0.4f);
            const Mat4 sc = Mat4::scale(0.6f, 0.6f, 1.0f);
            gl.glLoadMatrixf(rot.m);
            gl.glMultMatrixf(sc.m);
            gl.glColor3f(0.9f, 0.5f, 0.2f);
            gl.glRectf(-0.5f, -0.5f, 0.5f, 0.5f);
            gl.glLoadTransposeMatrixf(Mat4::identity().m);
            gl.glMultMatrixf(Mat4::translate(0.5f, 0.5f, 0).m);
            gl.glMultMatrixf(Mat4::scale(0.3f, 0.3f, 1).m);
            gl.glColor3f(0.2f, 0.7f, 0.9f);
            gl.glRectf(-0.5f, -0.5f, 0.5f, 0.5f);
            return Gx::shot(kit, target, out, error);
        });
    }
} kBatch3;

}  // namespace

}  // namespace lsw::render::gl
