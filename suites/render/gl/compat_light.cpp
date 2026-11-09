#include "render/gl/legacy.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec compat(const char* name) { return CaseSpec("compat", name).compat_profile(); }

constexpr float kWhite[4] = {1, 1, 1, 1};

void lit_setup(Kit& kit, float tilt = 20.0f, float spin = 30.0f) {
    Api& gl = kit.gl;
    Legacy::perspective(kit, tilt, spin);
    gl.glEnable(glc::DEPTH_TEST);
    gl.glEnable(glc::LIGHTING);
    gl.glEnable(glc::NORMALIZE);
    gl.glShadeModel(glc::SMOOTH);
}

void material(Kit& kit, const float* diffuse, const float* specular, float shininess, GLenum face = glc::FRONT_AND_BACK) {
    const float ambient[4] = {diffuse[0] * 0.2f, diffuse[1] * 0.2f, diffuse[2] * 0.2f, 1};
    kit.gl.glMaterialfv(face, glc::AMBIENT, ambient);
    kit.gl.glMaterialfv(face, glc::DIFFUSE, diffuse);
    kit.gl.glMaterialfv(face, glc::SPECULAR, specular);
    kit.gl.glMaterialf(face, glc::SHININESS, shininess);
}

struct LightCase {
    const char* name;
    std::function<void(Kit&)> setup;
};

const struct Batch2 {
    Batch2() {
        const float orange[4] = {0.9f, 0.5f, 0.2f, 1};
        const float teal[4] = {0.2f, 0.7f, 0.8f, 1};
        const float dim[4] = {0.3f, 0.3f, 0.3f, 1};
        const auto sphere_case = [&](const char* name, std::function<void(Kit&)> setup, bool plane = false, Tolerance tol = Tolerance::tight(2)) {
            Cases::single(compat(name).tolerance(tol), [setup, plane](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                lit_setup(kit);
                setup(kit);
                if (plane) {
                    kit.gl.glRotatef(-60, 1, 0, 0);
                    Legacy::draw_shape(kit, Shape::grid(24, 1.4f));
                } else {
                    Legacy::draw_shape(kit, Shape::sphere(24, 32, 1.1f));
                }
                return Gx::shot(kit, target, out, error);
            });
        };
        sphere_case("light_directional", [=](Kit& kit) {
            const float pos[4] = {1, 1, 1, 0};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            material(kit, orange, kWhite, 20.0f);
        });
        sphere_case("light_point", [=](Kit& kit) {
            const float pos[4] = {1.5f, 1.2f, 1.5f, 1};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            material(kit, teal, kWhite, 40.0f);
        });
        sphere_case("light_spot", [=](Kit& kit) {
            const float pos[4] = {0.0f, 0.4f, 2.0f, 1};
            const float dir[3] = {0.0f, -0.2f, -1.0f};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            kit.gl.glLightfv(glc::LIGHT0, glc::SPOT_DIRECTION, dir);
            kit.gl.glLightf(glc::LIGHT0, glc::SPOT_CUTOFF, 28.0f);
            kit.gl.glLightf(glc::LIGHT0, glc::SPOT_EXPONENT, 12.0f);
            material(kit, orange, kWhite, 30.0f);
        }, true);
        sphere_case("light_attenuation", [=](Kit& kit) {
            const float pos[4] = {0.5f, 0.5f, 1.8f, 1};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            kit.gl.glLightf(glc::LIGHT0, glc::CONSTANT_ATTENUATION, 0.4f);
            kit.gl.glLightf(glc::LIGHT0, glc::LINEAR_ATTENUATION, 0.3f);
            kit.gl.glLightf(glc::LIGHT0, glc::QUADRATIC_ATTENUATION, 0.2f);
            material(kit, teal, dim, 10.0f);
        }, true);
        sphere_case("light_local_viewer", [=](Kit& kit) {
            const float pos[4] = {0.8f, 0.8f, 1.5f, 1};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            kit.gl.glLightModeli(glc::LIGHT_MODEL_LOCAL_VIEWER, 1);
            material(kit, dim, kWhite, 60.0f);
        }, true);
        sphere_case("light_global_ambient", [=](Kit& kit) {
            const float amb[4] = {0.5f, 0.2f, 0.2f, 1};
            kit.gl.glLightModelfv(glc::LIGHT_MODEL_AMBIENT, amb);
            const float pos[4] = {-1, 1, 1, 0};
            Legacy::light(kit, 0, pos, dim, dim);
            material(kit, orange, dim, 5.0f);
        });
        sphere_case("light_eight", [=](Kit& kit) {
            for (unsigned i = 0; i < 8; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / 8.0f;
                const float pos[4] = {2.0f * std::cos(a), 2.0f * std::sin(a), 1.0f + 0.2f * static_cast<float>(i), 1};
                float diffuse[4] = {0, 0, 0, 1};
                Legacy::rainbow(i, 8, diffuse);
                for (float& d : diffuse) d *= 0.6f;
                diffuse[3] = 1;
                Legacy::light(kit, i, pos, diffuse, diffuse);
            }
            material(kit, kWhite, dim, 25.0f);
        });
        sphere_case("material_shininess", [=](Kit& kit) {
            const float pos[4] = {1, 1, 1.5f, 0};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            material(kit, orange, kWhite, 128.0f);
        });

        const auto colormat = [&](const char* name, GLenum mode, float r, float g, float b) {
            Cases::single(compat(name).tolerance(Tolerance::tight(2)), [=](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                lit_setup(kit);
                Api& gl = kit.gl;
                const float pos[4] = {1, 1, 1, 0};
                Legacy::light(kit, 0, pos, kWhite, kWhite);
                material(kit, dim, dim, 16.0f);
                gl.glColorMaterial(glc::FRONT_AND_BACK, mode);
                gl.glEnable(glc::COLOR_MATERIAL);
                gl.glColor3f(r, g, b);
                Legacy::draw_shape(kit, Shape::torus(24, 12, 0.8f, 0.35f));
                gl.glColor3f(b, r, g);
                gl.glTranslatef(0.0f, 0.0f, 0.0f);
                Legacy::draw_shape(kit, Shape::sphere(12, 16, 0.4f));
                gl.glDisable(glc::COLOR_MATERIAL);
                return Gx::shot(kit, target, out, error);
            });
        };
        colormat("colormaterial_ambient_diffuse", glc::AMBIENT_AND_DIFFUSE, 0.9f, 0.4f, 0.2f);
        colormat("colormaterial_diffuse", glc::DIFFUSE, 0.2f, 0.8f, 0.4f);
        colormat("colormaterial_specular", glc::SPECULAR, 1.0f, 1.0f, 0.4f);
        colormat("colormaterial_emission", glc::EMISSION, 0.1f, 0.3f, 0.7f);
        colormat("colormaterial_ambient", glc::AMBIENT, 0.8f, 0.1f, 0.5f);

        Cases::single(compat("light_two_sided"), [=](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            lit_setup(kit, 25.0f, 200.0f);
            Api& gl = kit.gl;
            const float pos[4] = {0.5f, 1, 2, 0};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            gl.glLightModeli(glc::LIGHT_MODEL_TWO_SIDE, 1);
            material(kit, orange, dim, 10.0f, glc::FRONT);
            material(kit, teal, dim, 10.0f, glc::BACK);
            gl.glDisable(glc::CULL_FACE);
            Legacy::draw_shape(kit, Shape::cone(24, 1.0f, 1.8f));
            gl.glRotatef(90, 1, 0, 0);
            gl.glTranslatef(0, 0, 1.0f);
            Legacy::draw_shape(kit, Shape::grid(8, 0.8f));
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("light_separate_specular"), [=](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            lit_setup(kit);
            Api& gl = kit.gl;
            const float pos[4] = {1, 1, 1.5f, 0};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            material(kit, kWhite, kWhite, 30.0f);
            gl.glLightModeli(glc::LIGHT_MODEL_COLOR_CONTROL, static_cast<GLint>(glc::SEPARATE_SPECULAR_COLOR));
            const GLuint texture = Gx::make_texture(kit, 64, 0);
            gl.glBindTexture(glc::TEXTURE_2D, texture);
            gl.glEnable(glc::TEXTURE_2D);
            gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::MODULATE));
            Legacy::draw_shape(kit, Shape::sphere(24, 32, 1.1f), true, true);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("light_flat_shade"), [=](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            lit_setup(kit);
            kit.gl.glShadeModel(glc::FLAT);
            const float pos[4] = {1, 1, 1, 0};
            Legacy::light(kit, 0, pos, kWhite, kWhite);
            material(kit, orange, dim, 10.0f);
            Legacy::draw_shape(kit, Shape::sphere(10, 14, 1.1f));
            return Gx::shot(kit, target, out, error);
        });

        const auto fog_case = [](const char* name, GLenum mode, bool coord) {
            Cases::single(compat(name).tolerance(Tolerance::tight(2)), [=](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!Legacy::open(kit, target, error)) {
                    return false;
                }
                Api& gl = kit.gl;
                Legacy::perspective(kit, 25.0f, 20.0f, 4.0f);
                gl.glEnable(glc::DEPTH_TEST);
                gl.glEnable(glc::FOG);
                const float fog_color[4] = {0.6f, 0.65f, 0.7f, 1};
                gl.glFogfv(glc::FOG_COLOR, fog_color);
                gl.glFogi(glc::FOG_MODE, static_cast<GLint>(mode));
                gl.glFogf(glc::FOG_DENSITY, 0.35f);
                gl.glFogf(glc::FOG_START, 2.5f);
                gl.glFogf(glc::FOG_END, 7.0f);
                if (coord) {
                    gl.glFogi(glc::FOG_COORD_SRC, static_cast<GLint>(glc::FOG_COORD));
                }
                gl.glClearColor(fog_color[0], fog_color[1], fog_color[2], 1);
                gl.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
                for (int i = 0; i < 6; ++i) {
                    const float z = -static_cast<float>(i) * 1.2f;
                    gl.glBegin(glc::QUADS);
                    gl.glColor3f(0.9f, 0.3f + 0.1f * static_cast<float>(i), 0.2f);
                    const float x = -1.2f + 0.45f * static_cast<float>(i);
                    for (int c = 0; c < 4; ++c) {
                        const float px = x + ((c == 1 || c == 2) ? 0.9f : 0.0f);
                        const float py = (c >= 2) ? 0.9f : -0.9f;
                        if (coord) {
                            gl.glFogCoordf(-z * 1.1f + 0.3f);
                        }
                        gl.glVertex3f(px, py, z);
                    }
                    gl.glEnd();
                }
                return Gx::shot(kit, target, out, error);
            });
        };
        fog_case("fog_linear", glc::LINEAR, false);
        fog_case("fog_exp", glc::EXP, false);
        fog_case("fog_exp2", glc::EXP2, false);
        fog_case("fog_coord_linear", glc::LINEAR, true);
        fog_case("fog_coord_exp2", glc::EXP2, true);
    }
} kBatch2;

}  // namespace

}  // namespace lsw::render::gl
