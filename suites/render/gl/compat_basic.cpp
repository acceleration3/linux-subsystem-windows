#include "render/gl/legacy.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec compat(const char* name) { return CaseSpec("compat", name).compat_profile(); }

struct PrimDef {
    const char* name;
    GLenum mode;
    bool lines;
};

void primitive_points(GLenum mode, std::vector<std::array<float, 2>>& out) {
    out.clear();
    const auto ring = [&](unsigned n, float inner, float outer) {
        for (unsigned i = 0; i < n; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / static_cast<float>(n);
            const float r = (i % 2 == 0) ? outer : inner;
            out.push_back({r * std::cos(a), r * std::sin(a)});
        }
    };
    switch (mode) {
        case glc::POINTS: ring(14, 0.5f, 0.85f); break;
        case glc::LINES: ring(12, 0.3f, 0.85f); break;
        case glc::LINE_STRIP: ring(9, 0.3f, 0.85f); break;
        case glc::LINE_LOOP: ring(10, 0.35f, 0.85f); break;
        case glc::TRIANGLES: ring(9, 0.45f, 0.85f); break;
        case glc::TRIANGLE_STRIP: out = {{-0.9f, -0.6f}, {-0.7f, 0.5f}, {-0.4f, -0.5f}, {-0.1f, 0.7f}, {0.2f, -0.4f}, {0.5f, 0.6f}, {0.7f, -0.7f}, {0.9f, 0.4f}}; break;
        case glc::TRIANGLE_FAN: ring(8, 0.8f, 0.8f); out.insert(out.begin(), {0.0f, 0.0f}); out.push_back(out[1]); break;
        case glc::QUADS: out = {{-0.9f, -0.9f}, {-0.1f, -0.8f}, {-0.2f, -0.1f}, {-0.8f, -0.2f}, {0.1f, 0.1f}, {0.9f, 0.2f}, {0.8f, 0.9f}, {0.2f, 0.8f}}; break;
        case glc::QUAD_STRIP: out = {{-0.9f, -0.5f}, {-0.9f, 0.5f}, {-0.3f, -0.8f}, {-0.3f, 0.2f}, {0.3f, -0.3f}, {0.3f, 0.8f}, {0.9f, -0.6f}, {0.9f, 0.4f}}; break;
        default: ring(7, 0.8f, 0.8f); break;
    }
}

void immediate_case(const PrimDef& def) {
    Cases::single(compat(def.name).tolerance(def.lines ? Tolerance::raster(2, 1) : Tolerance::tight(1)), [def](Kit& kit, CaptureSet& out, std::string& error) {
        Target target;
        if (!Legacy::open(kit, target, error)) {
            return false;
        }
        Api& gl = kit.gl;
        std::vector<std::array<float, 2>> points;
        primitive_points(def.mode, points);
        gl.glShadeModel(glc::SMOOTH);
        gl.glPointSize(7.0f);
        gl.glLineWidth(2.0f);
        gl.glBegin(def.mode);
        for (std::size_t i = 0; i < points.size(); ++i) {
            float c[3];
            Legacy::rainbow(static_cast<unsigned>(i), static_cast<unsigned>(points.size()), c);
            gl.glColor3f(c[0], c[1], c[2]);
            gl.glVertex2f(points[i][0], points[i][1]);
        }
        gl.glEnd();
        return Gx::shot(kit, target, out, error);
    });
}

struct ListDef {
    const char* name;
};

const struct Batch1 {
    Batch1() {
        const PrimDef prims[] = {{"im_points", glc::POINTS, true},         {"im_lines", glc::LINES, true},
                                 {"im_line_strip", glc::LINE_STRIP, true}, {"im_line_loop", glc::LINE_LOOP, true},
                                 {"im_triangles", glc::TRIANGLES, false},  {"im_tri_strip", glc::TRIANGLE_STRIP, false},
                                 {"im_tri_fan", glc::TRIANGLE_FAN, false}, {"im_quads", glc::QUADS, false},
                                 {"im_quad_strip", glc::QUAD_STRIP, false}, {"im_polygon", glc::POLYGON, false}};
        for (const PrimDef& def : prims) {
            immediate_case(def);
        }

        Cases::single(compat("list_call"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint list = gl.glGenLists(1);
            gl.glNewList(list, glc::COMPILE);
            gl.glBegin(glc::TRIANGLES);
            gl.glColor3f(1, 0.2f, 0.2f);
            gl.glVertex2f(-0.2f, -0.2f);
            gl.glColor3f(0.2f, 1, 0.2f);
            gl.glVertex2f(0.2f, -0.2f);
            gl.glColor3f(0.2f, 0.2f, 1);
            gl.glVertex2f(0.0f, 0.2f);
            gl.glEnd();
            gl.glEndList();
            for (int i = 0; i < 12; ++i) {
                gl.glPushMatrix();
                gl.glRotatef(30.0f * static_cast<float>(i), 0, 0, 1);
                gl.glTranslatef(0.55f, 0.0f, 0.0f);
                gl.glScalef(1.0f - 0.04f * static_cast<float>(i), 1.0f, 1.0f);
                gl.glCallList(list);
                gl.glPopMatrix();
            }
            gl.glDeleteLists(list, 1);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("list_nested"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint base = gl.glGenLists(2);
            gl.glNewList(base, glc::COMPILE);
            gl.glBegin(glc::QUADS);
            gl.glColor3f(0.9f, 0.7f, 0.2f);
            gl.glVertex2f(-0.1f, -0.1f);
            gl.glVertex2f(0.1f, -0.1f);
            gl.glColor3f(0.2f, 0.7f, 0.9f);
            gl.glVertex2f(0.1f, 0.1f);
            gl.glVertex2f(-0.1f, 0.1f);
            gl.glEnd();
            gl.glEndList();
            gl.glNewList(base + 1, glc::COMPILE);
            gl.glCallList(base);
            gl.glTranslatef(0.25f, 0.0f, 0.0f);
            gl.glRotatef(15, 0, 0, 1);
            gl.glCallList(base);
            gl.glTranslatef(0.25f, 0.0f, 0.0f);
            gl.glRotatef(15, 0, 0, 1);
            gl.glCallList(base);
            gl.glEndList();
            for (int row = 0; row < 3; ++row) {
                gl.glPushMatrix();
                gl.glTranslatef(-0.6f, -0.6f + 0.6f * static_cast<float>(row), 0.0f);
                gl.glCallList(base + 1);
                gl.glPopMatrix();
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("list_calllists_base"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint first = gl.glGenLists(4);
            for (unsigned k = 0; k < 4; ++k) {
                gl.glNewList(first + k, glc::COMPILE);
                gl.glColor3f(k == 0 ? 1.0f : 0.3f, k == 1 ? 1.0f : 0.3f, k == 2 ? 1.0f : 0.3f);
                gl.glBegin(glc::POLYGON);
                for (unsigned s = 0; s < 3 + k; ++s) {
                    const float a = 6.2831853f * static_cast<float>(s) / static_cast<float>(3 + k);
                    gl.glVertex2f(0.15f * std::cos(a), 0.15f * std::sin(a));
                }
                gl.glEnd();
                gl.glTranslatef(0.4f, 0.0f, 0.0f);
                gl.glEndList();
            }
            gl.glTranslatef(-0.6f, 0.4f, 0.0f);
            gl.glListBase(first);
            const GLubyte sequence[] = {0, 1, 2, 3};
            gl.glCallLists(4, glc::UNSIGNED_BYTE, sequence);
            gl.glLoadIdentity();
            gl.glTranslatef(-0.6f, -0.4f, 0.0f);
            const GLushort wide[] = {3, 2, 1, 0};
            gl.glCallLists(4, glc::UNSIGNED_SHORT, wide);
            gl.glListBase(0);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("list_compile_execute"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const GLuint list = gl.glGenLists(1);
            gl.glNewList(list, glc::COMPILE_AND_EXECUTE);
            gl.glColor3f(0.9f, 0.3f, 0.6f);
            gl.glRectf(-0.8f, -0.8f, -0.2f, -0.2f);
            gl.glEndList();
            gl.glTranslatef(1.0f, 1.0f, 0.0f);
            gl.glCallList(list);
            if (gl.glIsList(list) == 0) {
                error = "glIsList is false for a compiled list";
                return false;
            }
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("arrays_client_draw_arrays"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            float pos[18 * 2];
            float col[18 * 4];
            for (unsigned i = 0; i < 18; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / 18.0f;
                const float r = 0.5f + 0.35f * static_cast<float>(i % 3) / 2.0f;
                pos[i * 2] = r * std::cos(a);
                pos[i * 2 + 1] = r * std::sin(a);
                Legacy::rainbow(i, 18, col + i * 4);
                col[i * 4 + 3] = 1.0f;
            }
            gl.glEnableClientState(glc::VERTEX_ARRAY);
            gl.glEnableClientState(glc::COLOR_ARRAY);
            gl.glVertexPointer(2, glc::FLOAT, 0, pos);
            gl.glColorPointer(4, glc::FLOAT, 0, col);
            gl.glDrawArrays(glc::TRIANGLES, 0, 18);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("arrays_client_elements"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const Shape grid = Shape::grid(6, 0.8f);
            std::vector<std::uint8_t> colors(grid.vertices.size() / 8 * 3);
            for (std::size_t i = 0; i < colors.size() / 3; ++i) {
                colors[i * 3] = static_cast<std::uint8_t>(i * 37);
                colors[i * 3 + 1] = static_cast<std::uint8_t>(255 - i * 11);
                colors[i * 3 + 2] = static_cast<std::uint8_t>(i * 90);
            }
            gl.glEnableClientState(glc::COLOR_ARRAY);
            gl.glColorPointer(3, glc::UNSIGNED_BYTE, 0, colors.data());
            Legacy::draw_shape(kit, grid, false, false);
            gl.glDisableClientState(glc::COLOR_ARRAY);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(compat("arrays_interleaved"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const float data[] = {1, 0, 0, -0.7f, -0.7f, 0, 0, 1, 0, 0.7f, -0.7f, 0, 0, 0, 1, 0.0f, 0.8f, 0,
                                  1, 1, 0, 0.0f, -0.9f, 0, 0, 1, 1, 0.9f, 0.5f, 0, 1, 0, 1, -0.9f, 0.5f, 0};
            gl.glInterleavedArrays(glc::C3F_V3F, 0, data);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glDrawArrays(glc::LINE_LOOP, 3, 3);
            return Gx::shot(kit, target, out, error);
        }, {});

        Cases::single(compat("arrays_array_element"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const float pos[] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.8f, 0.8f, -0.8f, 0.8f, 0.0f, 0.0f};
            const float col[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1};
            gl.glEnableClientState(glc::VERTEX_ARRAY);
            gl.glEnableClientState(glc::COLOR_ARRAY);
            gl.glVertexPointer(2, glc::FLOAT, 0, pos);
            gl.glColorPointer(3, glc::FLOAT, 0, col);
            gl.glBegin(glc::TRIANGLE_FAN);
            for (int index : {4, 0, 1, 2, 3, 0}) {
                gl.glArrayElement(index);
            }
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });
    }
} kBatch1;

}  // namespace

}  // namespace lsw::render::gl
