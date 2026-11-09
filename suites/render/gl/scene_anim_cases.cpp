#include "render/gl/scene_extra.hpp"

namespace lsw::render::gl {

namespace {

CaseSpec scene(const std::string& name) { return CaseSpec("scene", name).min_version(4, 6); }

struct Tube {
    GLuint vao = 0;
    GLsizei count = 0;

    void create(Kit& kit) {
        constexpr unsigned rings = 25;
        constexpr unsigned segments = 18;
        struct Vertex {
            float p[3];
            float n[3];
            float uv[2];
            std::uint8_t bone[4];
            float weight[4];
        };
        std::vector<Vertex> vertices;
        for (unsigned r = 0; r < rings; ++r) {
            const float t = static_cast<float>(r) / static_cast<float>(rings - 1);
            const float y = -1.2f + 2.4f * t;
            const float radius = 0.32f + 0.08f * std::sin(t * 9.0f);
            const float zone = t * 3.0f;
            const unsigned lower = std::min(static_cast<unsigned>(zone), 2u);
            const float blend = zone - static_cast<float>(lower);
            for (unsigned s = 0; s <= segments; ++s) {
                const float a = 6.2831853f * static_cast<float>(s) / static_cast<float>(segments);
                Vertex v{};
                v.p[0] = radius * std::cos(a);
                v.p[1] = y;
                v.p[2] = radius * std::sin(a);
                v.n[0] = std::cos(a);
                v.n[2] = std::sin(a);
                v.uv[0] = static_cast<float>(s) / static_cast<float>(segments);
                v.uv[1] = t;
                v.bone[0] = static_cast<std::uint8_t>(lower);
                v.bone[1] = static_cast<std::uint8_t>(lower + 1);
                v.weight[0] = 1.0f - blend;
                v.weight[1] = blend;
                vertices.push_back(v);
            }
        }
        std::vector<std::uint32_t> indices;
        for (unsigned r = 0; r + 1 < rings; ++r) {
            for (unsigned s = 0; s < segments; ++s) {
                const std::uint32_t a = r * (segments + 1) + s;
                const std::uint32_t b = a + segments + 1;
                indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
            }
        }
        Api& gl = kit.gl;
        vao = kit.vertex_array();
        kit.buffer(glc::ARRAY_BUFFER, vertices.data(), vertices.size() * sizeof(Vertex), glc::STATIC_DRAW);
        gl.glVertexAttribPointer(0, 3, glc::FLOAT, glc::FALSE_, sizeof(Vertex), nullptr);
        gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(Vertex), reinterpret_cast<const void*>(12));
        gl.glVertexAttribPointer(2, 2, glc::FLOAT, glc::FALSE_, sizeof(Vertex), reinterpret_cast<const void*>(24));
        gl.glVertexAttribIPointer(3, 4, glc::UNSIGNED_BYTE, sizeof(Vertex), reinterpret_cast<const void*>(32));
        gl.glVertexAttribPointer(4, 4, glc::FLOAT, glc::FALSE_, sizeof(Vertex), reinterpret_cast<const void*>(36));
        for (GLuint i = 0; i < 5; ++i) {
            gl.glEnableVertexAttribArray(i);
        }
        kit.buffer(glc::ELEMENT_ARRAY_BUFFER, indices.data(), indices.size() * 4, glc::STATIC_DRAW);
        count = static_cast<GLsizei>(indices.size());
    }
};

Mat4 bone_matrix(unsigned index, float angle) {
    const float pivot = -1.2f + 0.8f * static_cast<float>(index);
    return Mat4::translate(0.0f, pivot, 0.0f) * Mat4::rotate_z(angle) * Mat4::translate(0.0f, -pivot, 0.0f);
}

std::string skin_vertex(bool storage) {
    const std::string block = storage ? "layout(std430, binding = 1) readonly buffer Bones { mat4 bones[]; };\n" : "layout(std140, binding = 1) uniform Bones { mat4 bones[4]; };\n";
    return "#version 460 core\n"
           "layout(location = 0) in vec3 in_pos;\nlayout(location = 1) in vec3 in_nrm;\nlayout(location = 2) in vec2 in_uv;\n"
           "layout(location = 3) in uvec4 in_bone;\nlayout(location = 4) in vec4 in_weight;\n" + block +
           "uniform mat4 u_vp;\nout vec3 v_nrm;\nout vec2 v_uv;\n"
           "void main() {\n"
           "    mat4 skin = bones[in_bone.x] * in_weight.x + bones[in_bone.y] * in_weight.y;\n"
           "    v_nrm = mat3(skin) * in_nrm;\n"
           "    v_uv = in_uv;\n"
           "    gl_Position = u_vp * (skin * vec4(in_pos, 1.0));\n"
           "}\n";
}

const char* kSkinFragment =
    "#version 460 core\nin vec3 v_nrm;\nin vec2 v_uv;\nlayout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    float stripe = mod(floor(v_uv.y * 12.0) + floor(v_uv.x * 6.0), 2.0);\n"
    "    float d = 0.25 + 0.75 * max(dot(normalize(v_nrm), normalize(vec3(0.4, 0.5, 0.8))), 0.0);\n"
    "    out_color = vec4(mix(vec3(0.9, 0.55, 0.25), vec3(0.3, 0.6, 0.9), stripe) * d, 1.0);\n"
    "}\n";

struct SkinState {
    Target target;
    Tube tube;
    GLuint program = 0;
    GLuint buffer = 0;
    float angles[4] = {0, 0, 0, 0};
};

void write_bones(Kit& kit, SkinState& s, GLenum target, unsigned first, unsigned count) {
    Mat4 palette[4];
    for (unsigned i = 0; i < 4; ++i) {
        palette[i] = bone_matrix(i, s.angles[i]);
    }
    kit.gl.glBindBuffer(target, s.buffer);
    kit.gl.glBufferSubData(target, static_cast<GLintptr>(first * sizeof(Mat4)), static_cast<GLsizeiptr>(count * sizeof(Mat4)), &palette[first]);
}

bool draw_skin(Kit& kit, SkinState& s, std::string& error) {
    Api& gl = kit.gl;
    s.target.bind(kit);
    Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1.0f, true, true);
    gl.glEnable(glc::DEPTH_TEST);
    gl.glUseProgram(s.program);
    const Mat4 vp = Gx::camera(1.0f, 4.2f, 0.4f, 0.2f);
    gl.glUniformMatrix4fv(gl.glGetUniformLocation(s.program, "u_vp"), 1, glc::FALSE_, vp.m);
    gl.glBindVertexArray(s.tube.vao);
    gl.glDrawElements(glc::TRIANGLES, s.tube.count, glc::UNSIGNED_INT, nullptr);
    return kit.check("skinning", error);
}

bool skin_setup(Kit& kit, SkinState& s, bool storage, std::string& error) {
    TargetDesc desc;
    desc.depth = glc::DEPTH_COMPONENT24;
    if (!s.target.create(kit, desc, error)) {
        return false;
    }
    s.tube.create(kit);
    const std::string vs = skin_vertex(storage);
    s.program = kit.program(vs.c_str(), kSkinFragment, error);
    if (s.program == 0) {
        return false;
    }
    const GLenum target = storage ? glc::SHADER_STORAGE_BUFFER : glc::UNIFORM_BUFFER;
    s.buffer = kit.buffer(target, nullptr, 4 * sizeof(Mat4), glc::DYNAMIC_DRAW);
    kit.gl.glBindBufferBase(target, 1, s.buffer);
    return true;
}

const char* kGlyphs[10] = {"01110100011001110101110011000101110", "00100011000010000100001000010001110", "01110100010000100010001000100011111",
                           "11110000010000101110000010000111110", "00010001100101010010111110001000010", "11111100001111000001000011000101110",
                           "00110010001000011110100011000101110", "11111000010001000100010000100001000", "01110100011000101110100011000101110",
                           "01110100011000101111000010001001100"};

std::vector<std::uint8_t> glyph_bitmap(unsigned digit) {
    std::vector<std::uint8_t> out(6 * 8, 0);
    for (unsigned y = 0; y < 7; ++y) {
        for (unsigned x = 0; x < 5; ++x) {
            out[y * 6 + x] = kGlyphs[digit][y * 5 + x] == '1' ? 255 : 0;
        }
    }
    return out;
}

const struct AnimBatch {
    AnimBatch() {
        for (const bool storage : {false, true}) {
            Cases::single(scene(storage ? "skinning_ssbo_bone_palette" : "skinning_ubo_bone_palette"), [storage](Kit& kit, CaptureSet& out, std::string& error) {
                SkinState s;
                if (!skin_setup(kit, s, storage, error)) {
                    return false;
                }
                s.angles[1] = 0.5f;
                s.angles[2] = -0.9f;
                s.angles[3] = 0.6f;
                write_bones(kit, s, storage ? glc::SHADER_STORAGE_BUFFER : glc::UNIFORM_BUFFER, 0, 4);
                return draw_skin(kit, s, error) && Gx::shot(kit, s.target, out, error);
            });
        }

        Cases::multi(scene("skinning_ubo_incremental_pose").frames(6, {0, 2, 5}), [] {
            auto s = std::make_shared<SkinState>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!skin_setup(kit, *s, false, error)) {
                    return false;
                }
                write_bones(kit, *s, glc::UNIFORM_BUFFER, 0, 4);
                return true;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                const unsigned bone = frame % 3 + 1;
                s->angles[bone] += 0.35f * (frame % 2 == 0 ? 1.0f : -0.8f);
                write_bones(kit, *s, glc::UNIFORM_BUFFER, bone, 1);
                return draw_skin(kit, *s, error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });

        Cases::multi(scene("text_atlas_glyphs_added_per_frame").frames(6, {0, 2, 5}), [] {
            struct State {
                Target target;
                GLuint atlas = 0;
                GLuint program = 0;
                GLuint vao = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                Api& gl = kit.gl;
                if (!s->target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                gl.glGenTextures(1, &s->atlas);
                gl.glBindTexture(glc::TEXTURE_2D, s->atlas);
                kit.defer([&kit, t = s->atlas] { kit.gl.glDeleteTextures(1, &t); });
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R8, 64, 8);
                const std::vector<std::uint8_t> blank(64 * 8, 0);
                gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
                gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 64, 8, glc::RED, glc::UNSIGNED_BYTE, blank.data());
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
                static const char* vs =
                    "#version 460 core\nlayout(location = 0) in vec3 in_cell;\nlayout(location = 1) in vec3 in_tint;\nout vec2 v_uv;\nout vec3 v_tint;\n"
                    "void main() {\n"
                    "    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
                    "    vec2 size = vec2(6.0, 8.0) * 2.0 / 128.0 * 2.0;\n"
                    "    vec2 origin = in_cell.xy;\n"
                    "    gl_Position = vec4(origin + corner * size, 0.0, 1.0);\n"
                    "    v_uv = vec2((in_cell.z + corner.x * 5.0 / 6.0) * 6.0 / 64.0, (1.0 - corner.y) * 7.0 / 8.0);\n"
                    "    v_tint = in_tint;\n"
                    "}\n";
                static const char* fs =
                    "#version 460 core\nin vec2 v_uv;\nin vec3 v_tint;\nuniform sampler2D u_atlas;\nlayout(location = 0) out vec4 out_color;\n"
                    "void main() { float a = texture(u_atlas, v_uv).r; out_color = vec4(v_tint, a); }\n";
                s->program = kit.program(vs, fs, error);
                if (s->program == 0) {
                    return false;
                }
                struct Instance {
                    float x, y, glyph, r, g, b;
                };
                std::vector<Instance> instances;
                for (unsigned line = 0; line < 5; ++line) {
                    for (unsigned i = 0; i < 10; ++i) {
                        instances.push_back({-0.95f + 0.2f * static_cast<float>(i) * 0.9f, 0.7f - 0.35f * static_cast<float>(line), static_cast<float>((i + line * 3) % 10), 0.4f + 0.12f * static_cast<float>(line),
                                             0.95f - 0.15f * static_cast<float>(line), 0.5f + 0.1f * static_cast<float>(i % 3)});
                    }
                }
                s->vao = kit.vertex_array();
                kit.buffer(glc::ARRAY_BUFFER, instances.data(), instances.size() * sizeof(Instance), glc::STATIC_DRAW);
                gl.glVertexAttribPointer(0, 3, glc::FLOAT, glc::FALSE_, sizeof(Instance), nullptr);
                gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(Instance), reinterpret_cast<const void*>(12));
                gl.glVertexAttribDivisor(0, 1);
                gl.glVertexAttribDivisor(1, 1);
                gl.glEnableVertexAttribArray(0);
                gl.glEnableVertexAttribArray(1);
                return kit.check("text setup", error);
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                for (unsigned k = 0; k < 2 && frame * 2 + k < 10; ++k) {
                    const unsigned digit = frame * 2 + k;
                    const auto bitmap = glyph_bitmap(digit);
                    gl.glBindTexture(glc::TEXTURE_2D, s->atlas);
                    gl.glTexSubImage2D(glc::TEXTURE_2D, 0, static_cast<GLint>(digit * 6), 0, 6, 8, glc::RED, glc::UNSIGNED_BYTE, bitmap.data());
                }
                s->target.bind(kit);
                Gx::clear(kit, 0.1f, 0.1f, 0.16f);
                gl.glEnable(glc::BLEND);
                gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
                gl.glUseProgram(s->program);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, s->atlas);
                kit.u1i(s->program, "u_atlas", 0);
                gl.glBindVertexArray(s->vao);
                gl.glDrawArraysInstanced(glc::TRIANGLE_STRIP, 0, 4, 50);
                gl.glDisable(glc::BLEND);
                return kit.check("text frame", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });

        Cases::multi(scene("motion_blur_velocity_from_previous_frame").frames(6, {1, 3, 5}), [] {
            struct State {
                Target target;
                RenderTarget g;
                SceneMeshes meshes;
                GLuint geometry = 0;
                GLuint blur = 0;
                Mat4 previous[3];
                bool have_previous = false;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->target.create(kit, TargetDesc{}, error) || !s->g.create(kit, 128, 128, {glc::RGBA8, glc::RG16F}, glc::DEPTH_COMPONENT24, error, glc::LINEAR)) {
                    return false;
                }
                s->meshes.create(kit);
                static const char* vs =
                    "#version 460 core\nlayout(location = 0) in vec3 in_pos;\nlayout(location = 1) in vec3 in_nrm;\nlayout(location = 2) in vec2 in_uv;\n"
                    "uniform mat4 u_mvp;\nuniform mat4 u_prev_mvp;\nout vec3 v_nrm;\nout vec2 v_uv;\nout vec4 v_now;\nout vec4 v_before;\n"
                    "void main() { v_nrm = in_nrm; v_uv = in_uv; v_now = u_mvp * vec4(in_pos, 1.0); v_before = u_prev_mvp * vec4(in_pos, 1.0); gl_Position = v_now; }\n";
                static const char* fs =
                    "#version 460 core\nin vec3 v_nrm;\nin vec2 v_uv;\nin vec4 v_now;\nin vec4 v_before;\nuniform vec4 u_color;\n"
                    "layout(location = 0) out vec4 out_color;\nlayout(location = 1) out vec2 out_velocity;\n"
                    "void main() {\n"
                    "    float stripe = mod(floor(v_uv.x * 10.0) + floor(v_uv.y * 6.0), 2.0);\n"
                    "    out_color = vec4(u_color.rgb * (0.5 + 0.5 * stripe) * (0.4 + 0.6 * max(dot(normalize(v_nrm), vec3(0.3, 0.8, 0.5)), 0.0)), 1.0);\n"
                    "    out_velocity = (v_now.xy / v_now.w - v_before.xy / v_before.w) * 0.5;\n"
                    "}\n";
                static const char* blur_fs =
                    "#version 460 core\nuniform sampler2D u_color;\nuniform sampler2D u_velocity;\nlayout(location = 0) out vec4 out_color;\n"
                    "void main() {\n"
                    "    vec2 uv = gl_FragCoord.xy / 128.0;\n"
                    "    vec2 v = texture(u_velocity, uv).rg;\n"
                    "    vec3 c = vec3(0.0);\n"
                    "    for (int i = 0; i < 12; ++i) c += texture(u_color, uv - v * (float(i) / 11.0 - 0.5)).rgb;\n"
                    "    out_color = vec4(c / 12.0, 1.0);\n"
                    "}\n";
                s->geometry = kit.program(vs, fs, error);
                s->blur = kit.program(kFullscreenVertexText(), blur_fs, error);
                return s->geometry != 0 && s->blur != 0;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                const float t = static_cast<float>(frame);
                const Mat4 vp = Mat4::perspective(1.0f, 1.0f, 0.5f, 30.0f, false) * Mat4::translate(0, 0, -5.0f) * Mat4::rotate_x(0.3f);
                const Mat4 models[3] = {Mat4::translate(-1.8f + 0.7f * t, 0.0f, 0.0f) * Mat4::scale(0.5f, 0.5f, 0.5f), Mat4::translate(0.0f, -0.4f + 0.2f * t, 0.5f) * Mat4::rotate_y(0.5f * t) * Mat4::rotate_x(1.0f),
                                        Mat4::translate(1.4f - 0.15f * t, 0.5f, -0.5f) * Mat4::rotate_y(0.3f * t) * Mat4::scale(0.6f, 0.6f, 0.6f)};
                const GpuShape* shapes[3] = {&s->meshes.sphere, &s->meshes.torus, &s->meshes.cube};
                s->g.bind(kit);
                gl.glEnable(glc::DEPTH_TEST);
                gl.glDepthFunc(glc::LESS);
                gl.glDepthMask(1);
                const GLfloat clear[4] = {0.1f, 0.1f, 0.15f, 1.0f};
                const GLfloat still[4] = {0, 0, 0, 0};
                gl.glClearBufferfv(glc::COLOR, 0, clear);
                gl.glClearBufferfv(glc::COLOR, 1, still);
                gl.glClearDepth(1.0);
                gl.glClear(glc::DEPTH_BUFFER_BIT);
                Painter p{kit};
                gl.glUseProgram(s->geometry);
                for (int i = 0; i < 3; ++i) {
                    const Mat4 previous = s->have_previous ? s->previous[i] : vp * models[i];
                    p.matrix(s->geometry, "u_mvp", vp * models[i]);
                    p.matrix(s->geometry, "u_prev_mvp", previous);
                    p.vec(s->geometry, "u_color", 0.9f - 0.3f * static_cast<float>(i), 0.4f + 0.25f * static_cast<float>(i), 0.3f + 0.3f * static_cast<float>(i));
                    shapes[i]->draw(kit);
                    s->previous[i] = vp * models[i];
                }
                s->have_previous = true;
                s->target.bind(kit);
                gl.glDisable(glc::DEPTH_TEST);
                p.bind_texture(0, glc::TEXTURE_2D, s->g.color[0]);
                p.bind_texture(1, glc::TEXTURE_2D, s->g.color[1]);
                gl.glUseProgram(s->blur);
                p.integer(s->blur, "u_color", 0);
                p.integer(s->blur, "u_velocity", 1);
                gl.glBindVertexArray(kit.vertex_array());
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
                return kit.check("motion blur", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });
    }

    static const char* kFullscreenVertexText() {
        return "#version 460 core\nout vec2 v_uv;\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); v_uv = p; gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    }
} kAnimBatch;

}  // namespace

}  // namespace lsw::render::gl
