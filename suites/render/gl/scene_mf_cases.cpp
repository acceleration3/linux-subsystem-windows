#include "render/gl/flat2d.hpp"
#include "render/gl/scene3.hpp"
#include "render/gl/texrig.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec scene(const std::string& name) { return CaseSpec("scene", name).min_version(4, 6).size(128, 128); }

struct Rt {
    GLuint texture = 0;
    GLuint fbo = 0;
};

Rt make_rt(Kit& kit, unsigned w, unsigned h, GLenum internal, GLenum filter = glc::NEAREST) {
    Api& gl = kit.gl;
    Rt rt;
    gl.glGenTextures(1, &rt.texture);
    gl.glBindTexture(glc::TEXTURE_2D, rt.texture);
    gl.glTexStorage2D(glc::TEXTURE_2D, 1, internal, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
    gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
    gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(filter));
    gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::REPEAT));
    gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::REPEAT));
    gl.glGenFramebuffers(1, &rt.fbo);
    gl.glBindFramebuffer(glc::FRAMEBUFFER, rt.fbo);
    gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, rt.texture, 0);
    kit.defer([&kit, rt] {
        kit.gl.glDeleteFramebuffers(1, &rt.fbo);
        kit.gl.glDeleteTextures(1, &rt.texture);
    });
    return rt;
}

void bind_unit(Kit& kit, unsigned unit, GLenum target, GLuint texture) {
    kit.gl.glActiveTexture(glc::TEXTURE0 + unit);
    kit.gl.glBindTexture(target, texture);
}

struct Pingpong : TexRig {
    Rt a;
    Rt b;
    int current = 0;
    GLuint step = 0;
    GLuint show = 0;

    [[nodiscard]] const Rt& read() const { return current == 0 ? a : b; }
    [[nodiscard]] const Rt& write() const { return current == 0 ? b : a; }
};

using SetupFn = std::function<bool(Kit&, Pingpong&, std::string&)>;

void pingpong_case(const std::string& name, unsigned size, GLenum internal, unsigned frames, std::vector<unsigned> captured, const std::string& init_body, const std::string& step_body,
                   const std::string& show_body, unsigned steps_per_frame = 1, Tolerance tolerance = Tolerance::tight(1)) {
    Cases::multi(scene(name).frames(frames, std::move(captured)).tolerance(std::move(tolerance)), [=] {
        auto s = std::make_shared<Pingpong>();
        Fns fns;
        fns.setup = [=](Kit& kit, std::string& error) {
            if (!s->open(kit, error)) {
                return false;
            }
            s->a = make_rt(kit, size, size, internal);
            s->b = make_rt(kit, size, size, internal);
            const GLuint init = s->program(kit, init_body, error);
            s->step = s->program(kit, step_body, error, "uniform sampler2D u_prev;");
            s->show = s->program(kit, show_body, error, "uniform sampler2D u_tex;");
            if (init == 0 || s->step == 0 || s->show == 0) {
                return false;
            }
            kit.gl.glBindFramebuffer(glc::FRAMEBUFFER, s->a.fbo);
            kit.gl.glViewport(0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size));
            s->draw(kit, init);
            return kit.check("pingpong init", error);
        };
        fns.frame = [=](Kit& kit, unsigned, std::string& error) {
            for (unsigned k = 0; k < steps_per_frame; ++k) {
                kit.gl.glBindFramebuffer(glc::FRAMEBUFFER, s->write().fbo);
                kit.gl.glViewport(0, 0, static_cast<GLsizei>(size), static_cast<GLsizei>(size));
                bind_unit(kit, 0, glc::TEXTURE_2D, s->read().texture);
                kit.u1i(s->step, "u_prev", 0);
                s->draw(kit, s->step);
                s->current = 1 - s->current;
            }
            s->target.bind(kit);
            bind_unit(kit, 0, glc::TEXTURE_2D, s->read().texture);
            kit.u1i(s->show, "u_tex", 0);
            s->draw(kit, s->show);
            return kit.check("pingpong frame", error);
        };
        fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
        return fns;
    });
}

const struct Batch21 {
    Batch21() {
        pingpong_case("life_pingpong_fragment", 64, glc::RGBA8, 12, {0, 5, 11},
                      "void main() { ivec2 p = ivec2(gl_FragCoord.xy); uint h = uint(p.x * 73856093) ^ uint(p.y * 19349663); h = (h ^ (h >> 13)) * 1274126177u; "
                      "float alive = float((h >> 16) % 5u == 0u); bool glider = (p.x == 10 && p.y == 11) || (p.x == 11 && p.y == 12) || (p.x == 9 && p.y == 13) || (p.x == 10 && p.y == 13) || (p.x == 11 && p.y == 13); "
                      "out_color = vec4(max(alive, float(glider)), 0.0, 0.0, 1.0); }",
                      "void main() { ivec2 p = ivec2(gl_FragCoord.xy); int n = 0; for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) if (x != 0 || y != 0) n += int(texelFetch(u_prev, (p + ivec2(x, y) + 64) % 64, 0).r > 0.5); "
                      "bool alive = texelFetch(u_prev, p, 0).r > 0.5; bool next = alive ? (n == 2 || n == 3) : (n == 3); out_color = vec4(float(next), float(alive) * 0.6, 0.0, 1.0); }",
                      "void main() { vec4 v = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 2.0), 0); out_color = vec4(v.r, v.g * 0.8 + v.r * 0.2, v.r * 0.5, 1.0); }");

        pingpong_case("reaction_diffusion_gray_scott", 64, glc::RG32F, 10, {0, 9},
                      "void main() { vec2 p = gl_FragCoord.xy; float seed = float(length(p - vec2(32.0)) < 6.0 || length(p - vec2(18.0, 40.0)) < 4.0); out_color = vec4(1.0 - seed, seed * 0.5, 0.0, 1.0); }",
                      "void main() { ivec2 p = ivec2(gl_FragCoord.xy); vec2 c = texelFetch(u_prev, p, 0).rg; vec2 l = vec2(0.0); "
                      "l += 0.2 * (texelFetch(u_prev, (p + ivec2(1, 0) + 64) % 64, 0).rg + texelFetch(u_prev, (p + ivec2(-1, 0) + 64) % 64, 0).rg + texelFetch(u_prev, (p + ivec2(0, 1) + 64) % 64, 0).rg + texelFetch(u_prev, (p + ivec2(0, -1) + 64) % 64, 0).rg); "
                      "l += 0.05 * (texelFetch(u_prev, (p + ivec2(1, 1) + 64) % 64, 0).rg + texelFetch(u_prev, (p + ivec2(-1, 1) + 64) % 64, 0).rg + texelFetch(u_prev, (p + ivec2(1, -1) + 64) % 64, 0).rg + texelFetch(u_prev, (p + ivec2(-1, -1) + 64) % 64, 0).rg); l -= c; "
                      "float uvv = c.x * c.y * c.y; vec2 n = c + vec2(1.0 * l.x - uvv + 0.055 * (1.0 - c.x), 0.5 * l.y + uvv - (0.062 + 0.055) * c.y); out_color = vec4(clamp(n, 0.0, 1.0), 0.0, 1.0); }",
                      "void main() { vec2 v = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 2.0), 0).rg; out_color = vec4(v.y * 3.0, v.x * 0.7, 1.0 - v.x, 1.0); }", 6, Tolerance::floats(4e-3f, 4e-3f).with_outliers(0.002));

        Cases::multi(scene("taa_jitter_history_accumulate").frames(8, {0, 7}).tolerance(Tolerance::tight(2)), [] {
            struct State {
                Scene3 sc;
                Rt history[2];
                Rt current;
                GLuint resolve = 0;
                GLuint copy_vao = 0;
                int index = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->sc.open(kit, error)) {
                    return false;
                }
                s->history[0] = make_rt(kit, 128, 128, glc::RGBA8, glc::NEAREST);
                s->history[1] = make_rt(kit, 128, 128, glc::RGBA8, glc::NEAREST);
                s->current = make_rt(kit, 128, 128, glc::RGBA8, glc::NEAREST);
                for (const Rt& r : {s->history[0], s->history[1]}) {
                    kit.gl.glBindFramebuffer(glc::FRAMEBUFFER, r.fbo);
                    kit.gl.glClearColor(0.08f, 0.08f, 0.12f, 1);
                    kit.gl.glClear(glc::COLOR_BUFFER_BIT);
                }
                static const char* resolve_fs =
                    "#version 460 core\nuniform sampler2D u_current;\nuniform sampler2D u_history;\nuniform float u_alpha;\nlayout(location = 0) out vec4 out_color;\n"
                    "void main() { ivec2 p = ivec2(gl_FragCoord.xy); out_color = mix(texelFetch(u_history, p, 0), texelFetch(u_current, p, 0), u_alpha); }\n";
                s->resolve = kit.program(Sources::kScreenVertex, resolve_fs, error);
                s->copy_vao = kit.vertex_array();
                return s->resolve != 0;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                static const float halton_x[8] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f, 0.0625f};
                static const float halton_y[8] = {0.333f, 0.667f, 0.111f, 0.444f, 0.778f, 0.222f, 0.556f, 0.889f};
                gl.glBindFramebuffer(glc::FRAMEBUFFER, s->current.fbo);
                gl.glViewport(0, 0, 128, 128);
                const GLenum draw = glc::COLOR_ATTACHMENT0;
                gl.glDrawBuffers(1, &draw);
                if (frame == 0) {
                    GLuint depth = 0;
                    gl.glGenRenderbuffers(1, &depth);
                    gl.glBindRenderbuffer(glc::RENDERBUFFER, depth);
                    gl.glRenderbufferStorage(glc::RENDERBUFFER, glc::DEPTH_COMPONENT24, 128, 128);
                    gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::RENDERBUFFER, depth);
                    kit.defer([&kit, depth] { kit.gl.glDeleteRenderbuffers(1, &depth); });
                }
                Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1, true);
                gl.glEnable(glc::DEPTH_TEST);
                gl.glUseProgram(s->sc.program);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, s->sc.texture);
                const float jx = (halton_x[frame % 8] - 0.5f) * 2.0f / 128.0f;
                const float jy = (halton_y[frame % 8] - 0.5f) * 2.0f / 128.0f;
                s->sc.view = Mat4::translate(jx, jy, 0.0f) * Gx::camera(1.0f, 3.2f, 0.5f, 0.4f);
                s->sc.draw(kit, s->sc.cube, Mat4::rotate_y(0.3f) * Mat4::rotate_x(0.2f));
                const Rt& old_history = s->history[s->index];
                const Rt& new_history = s->history[1 - s->index];
                gl.glBindFramebuffer(glc::FRAMEBUFFER, new_history.fbo);
                gl.glDisable(glc::DEPTH_TEST);
                bind_unit(kit, 0, glc::TEXTURE_2D, s->current.texture);
                bind_unit(kit, 1, glc::TEXTURE_2D, old_history.texture);
                kit.u1i(s->resolve, "u_current", 0);
                kit.u1i(s->resolve, "u_history", 1);
                kit.u1f(s->resolve, "u_alpha", frame == 0 ? 1.0f : 0.25f);
                gl.glBindVertexArray(s->copy_vao);
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
                s->index = 1 - s->index;
                return kit.check("taa", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) {
                Image image;
                if (!kit.read_color(s->history[s->index].fbo, glc::COLOR_ATTACHMENT0, glc::RGBA8, 128, 128, image, error)) {
                    return false;
                }
                out.add("", std::move(image));
                return true;
            };
            return fns;
        });

        Cases::multi(scene("particles_transform_feedback_pingpong").frames(10, {0, 4, 9}).tolerance(Tolerance::raster(2, 1)), [] {
            struct State {
                Target target;
                GLuint update = 0;
                GLuint draw = 0;
                GLuint buffers[2] = {};
                GLuint tfo = 0;
                GLuint vaos[2] = {};
                int index = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                Api& gl = kit.gl;
                if (!s->target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                static const char* vs =
                    "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec2 in_vel;\nout vec2 o_pos;\nout vec2 o_vel;\nuniform float u_dt;\n"
                    "void main() {\n"
                    "    vec2 v = in_vel + vec2(0.0, -0.9) * u_dt - in_pos * 0.35 * u_dt;\n"
                    "    vec2 p = in_pos + v * u_dt;\n"
                    "    if (p.y < -0.9) { p.y = -0.9; v.y = -v.y * 0.8; }\n"
                    "    if (abs(p.x) > 0.95) { p.x = sign(p.x) * 0.95; v.x = -v.x; }\n"
                    "    o_pos = p; o_vel = v; gl_Position = vec4(p, 0.0, 1.0);\n"
                    "}\n";
                const GLuint vertex = kit.shader(glc::VERTEX_SHADER, vs, error);
                if (vertex == 0) {
                    return false;
                }
                s->update = gl.glCreateProgram();
                gl.glAttachShader(s->update, vertex);
                const char* varyings[2] = {"o_pos", "o_vel"};
                gl.glTransformFeedbackVaryings(s->update, 2, varyings, glc::INTERLEAVED_ATTRIBS);
                gl.glLinkProgram(s->update);
                GLint ok = 0;
                gl.glGetProgramiv(s->update, glc::LINK_STATUS, &ok);
                if (ok == 0) {
                    error = "particle update program failed to link";
                    return false;
                }
                kit.defer([&kit, p = s->update] { kit.gl.glDeleteProgram(p); });
                static const char* draw_vs =
                    "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec2 in_vel;\nout vec3 v_col;\n"
                    "void main() { v_col = vec3(0.5 + 0.5 * in_vel.x, 0.6, 1.0 - 0.5 * abs(in_vel.y)); gl_PointSize = 5.0; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
                s->draw = kit.program(draw_vs, Flat2d::kFragment, error);
                if (s->draw == 0) {
                    return false;
                }
                std::vector<float> data;
                std::uint32_t seed = 99;
                for (int i = 0; i < 200; ++i) {
                    data.push_back(Pattern::unit_random(seed) * 1.6f - 0.8f);
                    data.push_back(Pattern::unit_random(seed) * 1.2f - 0.2f);
                    data.push_back(Pattern::unit_random(seed) * 1.0f - 0.5f);
                    data.push_back(Pattern::unit_random(seed) * 0.8f);
                }
                for (int i = 0; i < 2; ++i) {
                    s->buffers[i] = kit.buffer(glc::ARRAY_BUFFER, data.data(), data.size() * 4, glc::DYNAMIC_COPY);
                    s->vaos[i] = kit.vertex_array();
                    gl.glBindBuffer(glc::ARRAY_BUFFER, s->buffers[i]);
                    gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, 16, nullptr);
                    gl.glVertexAttribPointer(1, 2, glc::FLOAT, glc::FALSE_, 16, reinterpret_cast<const void*>(8));
                    gl.glEnableVertexAttribArray(0);
                    gl.glEnableVertexAttribArray(1);
                }
                gl.glGenTransformFeedbacks(1, &s->tfo);
                kit.defer([&kit, t = s->tfo] { kit.gl.glDeleteTransformFeedbacks(1, &t); });
                return kit.check("particle setup", error);
            };
            fns.frame = [s](Kit& kit, unsigned, std::string& error) {
                Api& gl = kit.gl;
                const int src = s->index;
                const int dst = 1 - s->index;
                gl.glUseProgram(s->update);
                kit.u1f(s->update, "u_dt", 0.06f);
                gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, s->tfo);
                gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, s->buffers[dst]);
                gl.glBindVertexArray(s->vaos[src]);
                gl.glEnable(glc::RASTERIZER_DISCARD);
                gl.glBeginTransformFeedback(glc::POINTS);
                gl.glDrawArrays(glc::POINTS, 0, 200);
                gl.glEndTransformFeedback();
                gl.glDisable(glc::RASTERIZER_DISCARD);
                gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
                s->target.bind(kit);
                Gx::clear(kit, 0.05f, 0.05f, 0.1f);
                gl.glEnable(glc::PROGRAM_POINT_SIZE);
                gl.glUseProgram(s->draw);
                gl.glBindVertexArray(s->vaos[dst]);
                gl.glDrawArrays(glc::POINTS, 0, 200);
                s->index = dst;
                return kit.check("particles", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });

        Cases::multi(scene("particles_compute_indirect_count").frames(8, {1, 7}).tolerance(Tolerance::raster(2, 1)), [] {
            struct State {
                Target target;
                GLuint compute = 0;
                GLuint draw = 0;
                GLuint particles = 0;
                GLuint command = 0;
                GLuint vao = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                Api& gl = kit.gl;
                if (!s->target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                static const char* cs =
                    "#version 460 core\nlayout(local_size_x = 64) in;\n"
                    "struct P { vec2 pos; vec2 vel; float life; float pad0; float pad1; float pad2; };\n"
                    "layout(std430, binding = 0) buffer Particles { P p[]; };\n"
                    "layout(std430, binding = 1) buffer Cmd { uint count; uint instances; uint first; uint base; uint alive; uint order[]; };\n"
                    "uniform float u_dt;\nuniform uint u_frame;\n"
                    "void main() {\n"
                    "    uint i = gl_GlobalInvocationID.x;\n"
                    "    if (i >= 256u) return;\n"
                    "    P q = p[i];\n"
                    "    q.vel.y -= 0.8 * u_dt;\n"
                    "    q.pos += q.vel * u_dt;\n"
                    "    q.life -= u_dt * (0.4 + 0.1 * float(i % 5u));\n"
                    "    if (q.life > 0.0) { uint slot = atomicAdd(count, 1u); order[slot] = i; }\n"
                    "    p[i] = q;\n"
                    "}\n";
                static const char* vs =
                    "#version 460 core\nstruct P { vec2 pos; vec2 vel; float life; float pad0; float pad1; float pad2; };\n"
                    "layout(std430, binding = 0) readonly buffer Particles { P p[]; };\n"
                    "layout(std430, binding = 1) readonly buffer Cmd { uint count; uint instances; uint first; uint base; uint alive; uint order[]; };\n"
                    "out vec3 v_col;\n"
                    "void main() { P q = p[order[gl_VertexID]]; v_col = vec3(1.0, 0.4 + 0.5 * q.life, 0.2); gl_PointSize = 3.0 + 4.0 * q.life; gl_Position = vec4(q.pos, 0.0, 1.0); }\n";
                s->compute = kit.program({{glc::COMPUTE_SHADER, cs}}, error);
                s->draw = s->compute != 0 ? kit.program(vs, Flat2d::kFragment, error) : 0;
                if (s->draw == 0) {
                    return false;
                }
                struct P {
                    float pos[2];
                    float vel[2];
                    float life;
                    float pad[3];
                };
                std::vector<P> particles(256);
                std::uint32_t seed = 7;
                for (P& q : particles) {
                    q = {{Pattern::unit_random(seed) * 0.2f - 0.1f, -0.7f}, {Pattern::unit_random(seed) * 1.2f - 0.6f, 0.9f + Pattern::unit_random(seed) * 0.8f}, 0.3f + Pattern::unit_random(seed) * 0.9f, {0, 0, 0}};
                }
                s->particles = kit.buffer(glc::SHADER_STORAGE_BUFFER, particles.data(), particles.size() * sizeof(P), glc::DYNAMIC_COPY);
                gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, s->particles);
                std::vector<std::uint32_t> command(5 + 256, 0);
                command[1] = 1;
                s->command = kit.buffer(glc::SHADER_STORAGE_BUFFER, command.data(), command.size() * 4, glc::DYNAMIC_COPY);
                gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 1, s->command);
                s->vao = kit.vertex_array();
                return kit.check("compute particle setup", error);
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                const GLuint zero = 0;
                gl.glBindBuffer(glc::SHADER_STORAGE_BUFFER, s->command);
                gl.glBufferSubData(glc::SHADER_STORAGE_BUFFER, 0, 4, &zero);
                gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, s->particles);
                gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 1, s->command);
                gl.glUseProgram(s->compute);
                kit.u1f(s->compute, "u_dt", 0.12f);
                gl.glUniform1ui(gl.glGetUniformLocation(s->compute, "u_frame"), frame);
                gl.glDispatchCompute(4, 1, 1);
                gl.glMemoryBarrier(glc::COMMAND_BARRIER_BIT | glc::SHADER_STORAGE_BARRIER_BIT);
                s->target.bind(kit);
                Gx::clear(kit, 0.05f, 0.05f, 0.1f);
                gl.glEnable(glc::PROGRAM_POINT_SIZE);
                gl.glUseProgram(s->draw);
                gl.glBindVertexArray(s->vao);
                gl.glBindBuffer(glc::DRAW_INDIRECT_BUFFER, s->command);
                gl.glDrawArraysIndirect(glc::POINTS, nullptr);
                return kit.check("compute particles", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });

        Cases::multi(scene("occlusion_query_drives_next_frame_lod").frames(6, {0, 2, 5}), [] {
            struct State {
                Scene3 sc;
                GLuint query = 0;
                bool detailed = true;
                GLuint program = 0;
                GpuShape high;
                GpuShape low;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->sc.open(kit, error)) {
                    return false;
                }
                s->high = Gx::upload(kit, Shape::sphere(24, 32, 0.7f));
                s->low = Gx::upload(kit, Shape::sphere(4, 6, 0.7f));
                kit.gl.glGenQueries(1, &s->query);
                kit.defer([&kit, q = s->query] { kit.gl.glDeleteQueries(1, &q); });
                return true;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                s->sc.target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1, true, true);
                gl.glEnable(glc::DEPTH_TEST);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, s->sc.texture);
                s->sc.view = Gx::camera(1.0f, 3.5f, 0.1f, 0.2f);
                const float wall_x = -1.6f + 0.55f * static_cast<float>(frame);
                s->sc.draw(kit, s->sc.cube, Mat4::translate(wall_x, 0.0f, 1.2f) * Mat4::scale(0.6f, 1.6f, 0.2f), 0.5f, 0.5f, 0.9f);
                gl.glColorMask(0, 0, 0, 0);
                gl.glDepthMask(0);
                gl.glBeginQuery(glc::SAMPLES_PASSED, s->query);
                s->sc.draw(kit, s->sc.cube, Mat4::translate(0.2f, 0.0f, 0.0f) * Mat4::scale(0.8f, 0.8f, 0.8f));
                gl.glEndQuery(glc::SAMPLES_PASSED);
                gl.glColorMask(1, 1, 1, 1);
                gl.glDepthMask(1);
                const GpuShape& shape = s->detailed ? s->high : s->low;
                s->sc.draw(kit, shape, Mat4::translate(0.2f, 0.0f, 0.0f), s->detailed ? 0.9f : 0.9f, s->detailed ? 0.5f : 0.9f, 0.3f);
                GLuint samples = 0;
                gl.glGetQueryObjectuiv(s->query, glc::QUERY_RESULT, &samples);
                s->detailed = samples > 900;
                return kit.check("occlusion", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->sc.target, out, error); };
            return fns;
        });

        Cases::multi(scene("conditional_render_any_samples_conservative").frames(6, {0, 3, 5}), [] {
            struct State {
                Scene3 sc;
                GLuint query = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->sc.open(kit, error)) {
                    return false;
                }
                kit.gl.glGenQueries(1, &s->query);
                kit.defer([&kit, q = s->query] { kit.gl.glDeleteQueries(1, &q); });
                return true;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                s->sc.target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1, true, true);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, s->sc.texture);
                s->sc.view = Gx::camera(1.0f, 3.5f, 0.2f, 0.3f);
                const float x = -1.5f + 0.6f * static_cast<float>(frame);
                s->sc.draw(kit, s->sc.cube, Mat4::translate(0.0f, 0.0f, 1.0f) * Mat4::scale(0.9f, 1.8f, 0.15f), 0.4f, 0.4f, 0.8f);
                gl.glColorMask(0, 0, 0, 0);
                gl.glDepthMask(0);
                gl.glBeginQuery(glc::ANY_SAMPLES_PASSED_CONSERVATIVE, s->query);
                s->sc.draw(kit, s->sc.cube, Mat4::translate(x, 0.0f, 0.0f) * Mat4::scale(0.3f, 0.3f, 0.3f));
                gl.glEndQuery(glc::ANY_SAMPLES_PASSED_CONSERVATIVE);
                gl.glColorMask(1, 1, 1, 1);
                gl.glDepthMask(1);
                gl.glBeginConditionalRender(s->query, glc::QUERY_WAIT);
                s->sc.draw(kit, s->sc.sphere, Mat4::translate(x, 0.0f, 0.0f) * Mat4::scale(0.3f, 0.3f, 0.3f), 0.2f, 0.9f, 0.3f);
                gl.glEndConditionalRender();
                gl.glBeginConditionalRender(s->query, glc::QUERY_WAIT_INVERTED);
                s->sc.draw(kit, s->sc.torus, Mat4::translate(0.0f, -1.2f, 0.0f) * Mat4::scale(0.3f, 0.3f, 0.3f), 0.9f, 0.3f, 0.2f);
                gl.glEndConditionalRender();
                return kit.check("conditional render", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->sc.target, out, error); };
            return fns;
        });

        Cases::multi(scene("dynamic_cube_map_reflection_frames").frames(4, {0, 3}).tolerance(Tolerance::filter(4, 0.002)), [] {
            struct State {
                Scene3 sc;
                GLuint cube = 0;
                GLuint fbo = 0;
                GLuint env_program = 0;
                GLuint reflect_program = 0;
                GLuint depth = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                Api& gl = kit.gl;
                if (!s->sc.open(kit, error)) {
                    return false;
                }
                gl.glGenTextures(1, &s->cube);
                gl.glBindTexture(glc::TEXTURE_CUBE_MAP, s->cube);
                gl.glTexStorage2D(glc::TEXTURE_CUBE_MAP, 1, glc::RGBA8, 64, 64);
                gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
                gl.glTexParameteri(glc::TEXTURE_CUBE_MAP, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
                kit.defer([&kit, c = s->cube] { kit.gl.glDeleteTextures(1, &c); });
                gl.glGenRenderbuffers(1, &s->depth);
                gl.glBindRenderbuffer(glc::RENDERBUFFER, s->depth);
                gl.glRenderbufferStorage(glc::RENDERBUFFER, glc::DEPTH_COMPONENT24, 64, 64);
                kit.defer([&kit, d = s->depth] { kit.gl.glDeleteRenderbuffers(1, &d); });
                gl.glGenFramebuffers(1, &s->fbo);
                kit.defer([&kit, f = s->fbo] { kit.gl.glDeleteFramebuffers(1, &f); });
                static const char* reflect_fs =
                    "#version 460 core\nin vec3 v_nrm;\nin vec3 v_world;\nuniform samplerCube u_env;\nuniform vec3 u_eye;\nlayout(location = 0) out vec4 out_color;\n"
                    "void main() { vec3 n = normalize(v_nrm); vec3 r = reflect(normalize(v_world - u_eye), n); out_color = vec4(texture(u_env, r).rgb * 0.85 + 0.1, 1.0); }\n";
                s->reflect_program = kit.program(Sources::kLitVertex, reflect_fs, error);
                return s->reflect_program != 0;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                Scene3& sc = s->sc;
                gl.glBindFramebuffer(glc::FRAMEBUFFER, s->fbo);
                gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::RENDERBUFFER, s->depth);
                gl.glViewport(0, 0, 64, 64);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, sc.texture);
                const Mat4 face_views[6] = {Mat4::rotate_y(-1.5708f), Mat4::rotate_y(1.5708f), Mat4::rotate_x(1.5708f), Mat4::rotate_x(-1.5708f), Mat4::identity(), Mat4::rotate_y(3.14159f)};
                const Mat4 proj = Mat4::perspective(1.5708f, 1.0f, 0.2f, 20.0f, false);
                const float a = 0.8f * static_cast<float>(frame);
                for (unsigned face = 0; face < 6; ++face) {
                    gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_CUBE_MAP_POSITIVE_X + face, s->cube, 0);
                    Gx::clear(kit, 0.15f + 0.1f * static_cast<float>(face), 0.2f, 0.3f, 1, true);
                    gl.glEnable(glc::DEPTH_TEST);
                    sc.view = proj * face_views[face];
                    for (int i = 0; i < 4; ++i) {
                        const float t = a + 1.5708f * static_cast<float>(i);
                        sc.draw(kit, sc.cube, Mat4::translate(2.2f * std::cos(t), 0.4f * std::sin(t * 2.0f), 2.2f * std::sin(t)) * Mat4::scale(0.5f, 0.5f, 0.5f), 0.9f - 0.2f * static_cast<float>(i), 0.3f + 0.2f * static_cast<float>(i), 0.3f);
                    }
                }
                gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::DEPTH_ATTACHMENT, glc::RENDERBUFFER, 0);
                sc.target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f, 1, true, true);
                gl.glEnable(glc::DEPTH_TEST);
                sc.view = Gx::camera(1.0f, 3.2f, 0.3f, 0.4f);
                const GLuint lit = sc.program;
                sc.program = s->reflect_program;
                bind_unit(kit, 0, glc::TEXTURE_CUBE_MAP, s->cube);
                kit.u1i(s->reflect_program, "u_env", 0);
                kit.u3f(s->reflect_program, "u_eye", 0.0f, 0.0f, 3.2f);
                sc.draw(kit, sc.sphere, Mat4::identity());
                sc.program = lit;
                return kit.check("dynamic cube", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->sc.target, out, error); };
            return fns;
        });

        Cases::multi(scene("ubo_ring_slot_per_frame_trail").frames(8, {0, 4, 7}), [] {
            struct State {
                Target target;
                GLuint program = 0;
                GLuint ubo = 0;
                GLuint vao = 0;
                GLint stride = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                s->target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                static const char* vs =
                    "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\nlayout(std140, binding = 2) uniform Frame { mat4 m; vec4 tint; };\nout vec3 v_col;\n"
                    "void main() { v_col = in_col * tint.rgb; gl_Position = m * vec4(in_pos, 0.0, 1.0); }\n";
                s->program = kit.program(vs, Flat2d::kFragment, error);
                if (s->program == 0) {
                    return false;
                }
                const GLint align = kit.integer(glc::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
                s->stride = ((80 + align - 1) / align) * align;
                s->ubo = kit.buffer(glc::UNIFORM_BUFFER, nullptr, static_cast<std::size_t>(s->stride) * 4, glc::DYNAMIC_DRAW);
                s->vao = Flat2d::upload(kit, Flat2d::tile(1));
                const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
                kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
                return kit.check("ubo ring setup", error);
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                const unsigned slot = frame % 4;
                const float a = 0.55f * static_cast<float>(frame);
                const Mat4 m = Mat4::translate(0.6f * std::cos(a), 0.6f * std::sin(a), 0.0f) * Mat4::rotate_z(a) * Mat4::scale(0.25f, 0.25f, 1.0f);
                float block[20];
                std::memcpy(block, m.m, sizeof m.m);
                block[16] = 1.0f - 0.1f * static_cast<float>(frame);
                block[17] = 0.4f + 0.07f * static_cast<float>(frame);
                block[18] = 0.5f + 0.05f * static_cast<float>(frame);
                block[19] = 1.0f;
                gl.glBindBuffer(glc::UNIFORM_BUFFER, s->ubo);
                gl.glBufferSubData(glc::UNIFORM_BUFFER, static_cast<GLintptr>(slot) * s->stride, sizeof block, block);
                gl.glBindBufferRange(glc::UNIFORM_BUFFER, 2, s->ubo, static_cast<GLintptr>(slot) * s->stride, sizeof block);
                s->target.bind(kit);
                gl.glUseProgram(s->program);
                gl.glBindVertexArray(s->vao);
                gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
                return kit.check("ubo ring", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });

        Cases::multi(scene("texture_respecify_formats_each_frame").frames(6, {0, 2, 5}), [] {
            struct State {
                TexRig rig;
                GLuint texture = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                if (!s->rig.open(kit, error)) {
                    return false;
                }
                kit.gl.glGenTextures(1, &s->texture);
                kit.defer([&kit, t = s->texture] { kit.gl.glDeleteTextures(1, &t); });
                return true;
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                struct Spec {
                    GLenum internal;
                    GLenum format;
                    GLenum type;
                    unsigned size;
                    unsigned bytes;
                };
                const Spec specs[6] = {{glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE, 16, 4}, {glc::R16F, glc::RED, glc::HALF_FLOAT, 8, 2}, {glc::RGB565, glc::RGB, glc::UNSIGNED_SHORT_5_6_5, 32, 2},
                                       {glc::RG8, glc::RG, glc::UNSIGNED_BYTE, 4, 2}, {glc::RGBA32F, glc::RGBA, glc::FLOAT, 16, 16}, {glc::R8, glc::RED, glc::UNSIGNED_BYTE, 64, 1}};
                const Spec& sp = specs[frame % 6];
                std::vector<std::uint8_t> data(static_cast<std::size_t>(sp.size) * sp.size * sp.bytes);
                for (std::size_t i = 0; i < data.size(); ++i) {
                    data[i] = static_cast<std::uint8_t>((i * 37 + frame * 61) % 251);
                }
                if (sp.type == glc::FLOAT) {
                    auto* f = reinterpret_cast<float*>(data.data());
                    for (std::size_t i = 0; i < data.size() / 4; ++i) {
                        f[i] = static_cast<float>((i * 7 + frame) % 13) / 12.0f;
                    }
                } else if (sp.type == glc::HALF_FLOAT) {
                    auto* h = reinterpret_cast<std::uint16_t*>(data.data());
                    for (std::size_t i = 0; i < data.size() / 2; ++i) {
                        h[i] = PixelInfo::float_to_half(static_cast<float>((i * 5) % 11) / 10.0f);
                    }
                }
                gl.glBindTexture(glc::TEXTURE_2D, s->texture);
                gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<GLint>(sp.internal), static_cast<GLsizei>(sp.size), static_cast<GLsizei>(sp.size), 0, sp.format, sp.type, data.data());
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
                const GLuint program = s->rig.program(kit, "void main() { ivec2 s = textureSize(u_tex, 0); vec4 t = texelFetch(u_tex, ivec2(gl_FragCoord.xy / 128.0 * vec2(s)), 0); out_color = vec4(t.rgb + vec3(t.a * 0.0), 1.0) * (0.3 + 0.7 * float(s.x) / 64.0 + 0.4); }", error, "uniform sampler2D u_tex;");
                if (program == 0) {
                    return false;
                }
                s->rig.target.bind(kit);
                kit.u1i(program, "u_tex", 0);
                s->rig.draw(kit, program);
                return kit.check("respecify", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->rig.target, out, error); };
            return fns;
        });
    }
} kBatch21;

}  // namespace

}  // namespace lsw::render::gl
