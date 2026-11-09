#include "render/gl/flat2d.hpp"
#include "render/gl/legacy.hpp"
#include "render/gl/scene3.hpp"
#include "render/gl/texrig.hpp"

#include <cmath>
#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec scene(const std::string& name) { return CaseSpec("scene", name).min_version(4, 6); }

const struct Batch22 {
    Batch22() {
        Cases::multi(scene("shared_context_texture_render_frames").frames(4, {0, 3}), [] {
            struct State {
                TexRig rig;
                GLuint texture = 0;
                GLuint flat = 0;
                GLuint sample = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                Api& gl = kit.gl;
                if (!s->rig.open(kit, error)) {
                    return false;
                }
                if (!kit.platform->create_shared_context(error)) {
                    return false;
                }
                gl.glGenTextures(1, &s->texture);
                gl.glBindTexture(glc::TEXTURE_2D, s->texture);
                gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 64, 64);
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR));
                gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
                kit.defer([&kit, t = s->texture] { kit.gl.glDeleteTextures(1, &t); });
                s->flat = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
                s->sample = s->rig.program(kit, "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = texture(u_tex, c) * (0.6 + 0.4 * c.x); }", error, "uniform sampler2D u_tex;");
                return s->flat != 0 && s->sample != 0 && kit.check("shared setup", error);
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                if (!kit.platform->make_current(1, error)) {
                    return false;
                }
                GLuint fbo = 0;
                GLuint vao = 0;
                GLuint vbo = 0;
                gl.glGenFramebuffers(1, &fbo);
                gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
                gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0, glc::TEXTURE_2D, s->texture, 0);
                const bool complete = gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) == glc::FRAMEBUFFER_COMPLETE;
                gl.glViewport(0, 0, 64, 64);
                if (frame == 0) {
                    gl.glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
                    gl.glClear(glc::COLOR_BUFFER_BIT);
                }
                const std::vector<V2> verts = Flat2d::ring(3 + (frame % 2) * 3, 0.15f + 0.1f * static_cast<float>(frame), 0.5f + 0.1f * static_cast<float>(frame), 0.6f * static_cast<float>(frame));
                gl.glGenVertexArrays(1, &vao);
                gl.glBindVertexArray(vao);
                gl.glGenBuffers(1, &vbo);
                gl.glBindBuffer(glc::ARRAY_BUFFER, vbo);
                gl.glBufferData(glc::ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(V2)), verts.data(), glc::STATIC_DRAW);
                gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
                gl.glEnableVertexAttribArray(0);
                gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
                gl.glEnableVertexAttribArray(1);
                gl.glUseProgram(s->flat);
                gl.glUniform1f(gl.glGetUniformLocation(s->flat, "u_size"), 1.0f);
                gl.glDrawArrays(glc::TRIANGLES, 0, static_cast<GLsizei>(verts.size()));
                GLsync fence = gl.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
                gl.glFlush();
                gl.glBindFramebuffer(glc::FRAMEBUFFER, 0);
                gl.glBindVertexArray(0);
                gl.glDeleteVertexArrays(1, &vao);
                gl.glDeleteBuffers(1, &vbo);
                gl.glDeleteFramebuffers(1, &fbo);
                std::string second_error;
                const bool second_ok = kit.check("second context", second_error);
                if (!kit.platform->make_current(0, error)) {
                    return false;
                }
                if (!complete || !second_ok) {
                    error = complete ? second_error : "the shared texture framebuffer is incomplete in the second context";
                    return false;
                }
                const GLenum waited = gl.glClientWaitSync(fence, glc::SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
                gl.glDeleteSync(fence);
                if (waited != glc::ALREADY_SIGNALED && waited != glc::CONDITION_SATISFIED) {
                    error = "the fence created in the second context did not signal";
                    return false;
                }
                s->rig.target.bind(kit);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, s->texture);
                kit.u1i(s->sample, "u_tex", 0);
                s->rig.draw(kit, s->sample);
                return kit.check("shared sample", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->rig.target, out, error); };
            return fns;
        });

        Cases::single(scene("instanced_forest_per_instance_attributes").size(128, 128), [](Kit& kit, CaptureSet& out, std::string& error) {
            Scene3 s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec3 in_pos;\nlayout(location = 1) in vec3 in_nrm;\nlayout(location = 2) in vec2 in_uv;\n"
                "layout(location = 3) in vec4 in_inst;\nlayout(location = 4) in vec3 in_tint;\nuniform mat4 u_mvp;\nout vec3 v_nrm;\nout vec3 v_tint;\n"
                "void main() { v_nrm = in_nrm; v_tint = in_tint; vec3 p = in_pos * in_inst.w + vec3(in_inst.x, 0.0, in_inst.z) + vec3(0.0, in_inst.y, 0.0); gl_Position = u_mvp * vec4(p, 1.0); }\n";
            static const char* fs =
                "#version 460 core\nin vec3 v_nrm;\nin vec3 v_tint;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { float d = max(dot(normalize(v_nrm), normalize(vec3(0.4, 0.9, 0.3))), 0.0); out_color = vec4(v_tint * (0.25 + 0.75 * d), 1.0); }\n";
            const GLuint program = kit.program(vs, fs, error);
            if (program == 0) {
                return false;
            }
            std::vector<float> trunks;
            std::vector<float> crowns;
            std::uint32_t seed = 5;
            for (int i = 0; i < 70; ++i) {
                const float x = (Pattern::unit_random(seed) - 0.5f) * 5.0f;
                const float z = (Pattern::unit_random(seed) - 0.5f) * 5.0f;
                const float sc = 0.5f + Pattern::unit_random(seed) * 0.6f;
                trunks.insert(trunks.end(), {x, 0.0f, z, sc, 0.45f, 0.3f, 0.15f});
                crowns.insert(crowns.end(), {x, 1.1f * sc, z, sc, 0.15f + 0.3f * Pattern::unit_random(seed), 0.5f + 0.4f * Pattern::unit_random(seed), 0.2f});
            }
            const GpuShape cone = Gx::upload(kit, Shape::cone(10, 0.3f, 1.4f));
            const GpuShape sphere = Gx::upload(kit, Shape::sphere(8, 10, 0.6f));
            const auto bind_instances = [&](const GpuShape& shape, const std::vector<float>& data) {
                gl.glBindVertexArray(shape.vao);
                kit.buffer(glc::ARRAY_BUFFER, data.data(), data.size() * 4, glc::STATIC_DRAW);
                gl.glVertexAttribPointer(3, 4, glc::FLOAT, glc::FALSE_, 28, nullptr);
                gl.glVertexAttribPointer(4, 3, glc::FLOAT, glc::FALSE_, 28, reinterpret_cast<const void*>(16));
                gl.glEnableVertexAttribArray(3);
                gl.glEnableVertexAttribArray(4);
                gl.glVertexAttribDivisor(3, 1);
                gl.glVertexAttribDivisor(4, 1);
            };
            bind_instances(cone, trunks);
            bind_instances(sphere, crowns);
            gl.glUseProgram(program);
            kit.umat4(program, "u_mvp", (Gx::camera(1.0f, 7.0f, 0.7f, 0.6f)).m);
            gl.glEnable(glc::DEPTH_TEST);
            cone.draw_instanced(kit, 70);
            sphere.draw_instanced(kit, 70);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(scene("ui_compositing_rounded_panels_scissor_alpha"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\nuniform vec4 u_rect;\nuniform vec4 u_color;\nuniform float u_radius;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 p = gl_FragCoord.xy - (u_rect.xy + u_rect.zw * 0.5);\n"
                "    vec2 q = abs(p) - u_rect.zw * 0.5 + u_radius;\n"
                "    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - u_radius;\n"
                "    float a = clamp(0.5 - d, 0.0, 1.0);\n"
                "    out_color = vec4(u_color.rgb * (0.85 + 0.15 * gl_FragCoord.y / 128.0), u_color.a * a);\n"
                "}\n";
            const GLuint program = kit.program(Sources::kScreenVertex, fs, error);
            if (program == 0) {
                return false;
            }
            gl.glClearColor(0.2f, 0.25f, 0.3f, 1);
            gl.glClear(glc::COLOR_BUFFER_BIT);
            gl.glEnable(glc::BLEND);
            gl.glBlendFuncSeparate(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA, glc::ONE, glc::ONE_MINUS_SRC_ALPHA);
            gl.glBindVertexArray(rig.vao);
            const auto panel = [&](float x, float y, float w, float h, float r, float cr, float cg, float cb, float ca) {
                kit.u4f(program, "u_rect", x, y, w, h);
                kit.u4f(program, "u_color", cr, cg, cb, ca);
                kit.u1f(program, "u_radius", r);
                gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            };
            panel(8, 8, 112, 112, 10, 0.1f, 0.1f, 0.14f, 0.85f);
            gl.glEnable(glc::SCISSOR_TEST);
            gl.glScissor(14, 30, 100, 60);
            for (int i = 0; i < 6; ++i) {
                panel(18, 20.0f + 18.0f * static_cast<float>(i), 92, 14, 4, 0.9f - 0.1f * static_cast<float>(i), 0.5f, 0.3f + 0.1f * static_cast<float>(i), 0.9f);
            }
            gl.glDisable(glc::SCISSOR_TEST);
            panel(30, 96, 68, 18, 9, 0.2f, 0.7f, 0.4f, 1.0f);
            panel(50, 40, 60, 60, 30, 0.9f, 0.9f, 0.2f, 0.35f);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::single(scene("terrain_tessellated_heightmap_displacement"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Scene3 s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            std::vector<float> height(32 * 32);
            for (int y = 0; y < 32; ++y) {
                for (int x = 0; x < 32; ++x) {
                    height[static_cast<std::size_t>(y * 32 + x)] = 0.5f + 0.25f * std::sin(static_cast<float>(x) * 0.5f) * std::cos(static_cast<float>(y) * 0.4f) + 0.15f * std::sin(static_cast<float>(x + y) * 0.9f);
                }
            }
            const GLuint tex = TexRig::texture(kit, glc::TEXTURE_2D, glc::LINEAR);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R32F, 32, 32);
            gl.glTexSubImage2D(glc::TEXTURE_2D, 0, 0, 0, 32, 32, glc::RED, glc::FLOAT, height.data());
            static const char* vs = "#version 460 core\nlayout(location = 0) in vec3 in_pos;\nlayout(location = 2) in vec2 in_uv;\nout vec2 vuv;\nvoid main() { vuv = in_uv; gl_Position = vec4(in_pos, 1.0); }\n";
            static const char* tcs =
                "#version 460 core\nlayout(vertices = 4) out;\nin vec2 vuv[];\nout vec2 tuv[];\n"
                "void main() { gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position; tuv[gl_InvocationID] = vuv[gl_InvocationID];\n"
                "    if (gl_InvocationID == 0) { for (int i = 0; i < 4; ++i) gl_TessLevelOuter[i] = 9.0; gl_TessLevelInner[0] = 9.0; gl_TessLevelInner[1] = 9.0; } }\n";
            static const char* tes =
                "#version 460 core\nlayout(quads, equal_spacing, ccw) in;\nin vec2 tuv[];\nuniform sampler2D u_height;\nuniform mat4 u_mvp;\nout vec3 world;\nout float h;\n"
                "void main() {\n"
                "    vec2 t = gl_TessCoord.xy;\n"
                "    vec4 p = mix(mix(gl_in[0].gl_Position, gl_in[1].gl_Position, t.x), mix(gl_in[3].gl_Position, gl_in[2].gl_Position, t.x), t.y);\n"
                "    vec2 uv = mix(mix(tuv[0], tuv[1], t.x), mix(tuv[3], tuv[2], t.x), t.y);\n"
                "    h = texture(u_height, uv).r;\n"
                "    p.z = h * 1.2;\n"
                "    world = p.xyz;\n"
                "    gl_Position = u_mvp * vec4(p.x, p.z, -p.y, 1.0);\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nin vec3 world;\nin float h;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { vec3 n = normalize(cross(dFdx(world), dFdy(world))); float d = max(dot(n, normalize(vec3(0.3, 0.5, 0.8))), 0.0); out_color = vec4(mix(vec3(0.2, 0.5, 0.2), vec3(0.9, 0.85, 0.7), smoothstep(0.35, 0.85, h)) * (0.3 + 0.7 * d), 1.0); }\n";
            const GLuint program = kit.program({{glc::VERTEX_SHADER, vs}, {glc::TESS_CONTROL_SHADER, tcs}, {glc::TESS_EVALUATION_SHADER, tes}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            const GpuShape grid = Gx::upload(kit, Shape::grid(1, 1.6f));
            std::vector<std::uint32_t> patch = {0, 1, 3, 2};
            (void)patch;
            const std::vector<GLuint> quad = {0, 1, 3, 2};
            gl.glBindVertexArray(grid.vao);
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, quad.data(), quad.size() * 4, glc::STATIC_DRAW);
            gl.glUseProgram(program);
            kit.u1i(program, "u_height", 0);
            gl.glActiveTexture(glc::TEXTURE0);
            gl.glBindTexture(glc::TEXTURE_2D, tex);
            kit.umat4(program, "u_mvp", (Gx::camera(1.0f, 4.0f, 0.8f, 0.5f)).m);
            gl.glEnable(glc::DEPTH_TEST);
            gl.glPatchParameteri(glc::PATCH_VERTICES, 4);
            gl.glDrawElements(glc::PATCHES, 4, glc::UNSIGNED_INT, nullptr);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(scene("oit_linked_list_atomic_counter_resolve"), [](Kit& kit, CaptureSet& out, std::string& error) {
            TexRig rig;
            if (!rig.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\nout vec3 v_col;\nout float v_depth;\n"
                "void main() { v_col = in_col; v_depth = float(gl_VertexID / 3) / 8.0; gl_Position = vec4(in_pos, v_depth * 0.5, 1.0); }\n";
            static const char* fs =
                "#version 460 core\nin vec3 v_col;\nin float v_depth;\nlayout(early_fragment_tests) in;\n"
                "layout(binding = 0, offset = 0) uniform atomic_uint u_counter;\nlayout(r32ui, binding = 0) coherent uniform uimage2D u_head;\n"
                "struct Node { vec4 color; float depth; uint next; uint pad0; uint pad1; };\nlayout(std430, binding = 3) buffer Nodes { Node nodes[]; };\n"
                "void main() {\n"
                "    uint index = atomicCounterIncrement(u_counter);\n"
                "    if (index >= 8192u) return;\n"
                "    uint prev = imageAtomicExchange(u_head, ivec2(gl_FragCoord.xy), index);\n"
                "    nodes[index] = Node(vec4(v_col, 0.55), v_depth, prev, 0u, 0u);\n"
                "}\n";
            static const char* resolve =
                "#version 460 core\nlayout(r32ui, binding = 0) readonly uniform uimage2D u_head;\nlayout(location = 0) out vec4 out_color;\n"
                "struct Node { vec4 color; float depth; uint next; uint pad0; uint pad1; };\nlayout(std430, binding = 3) readonly buffer Nodes { Node nodes[]; };\n"
                "void main() {\n"
                "    Node list[8]; int n = 0;\n"
                "    uint at = imageLoad(u_head, ivec2(gl_FragCoord.xy)).r;\n"
                "    while (at != 0xFFFFFFFFu && n < 8) { list[n++] = nodes[at]; at = nodes[at].next; }\n"
                "    for (int i = 1; i < n; ++i) { Node key = list[i]; int j = i - 1; while (j >= 0 && list[j].depth < key.depth) { list[j + 1] = list[j]; --j; } list[j + 1] = key; }\n"
                "    vec3 c = vec3(0.12, 0.12, 0.16);\n"
                "    for (int i = 0; i < n; ++i) c = mix(c, list[i].color.rgb, list[i].color.a);\n"
                "    out_color = vec4(c, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(vs, fs, error);
            const GLuint resolve_program = program != 0 ? kit.program(Sources::kScreenVertex, resolve, error) : 0;
            if (resolve_program == 0) {
                return false;
            }
            GLuint head = 0;
            gl.glGenTextures(1, &head);
            kit.defer([&kit, head] { kit.gl.glDeleteTextures(1, &head); });
            gl.glBindTexture(glc::TEXTURE_2D, head);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R32UI, 128, 128);
            const GLuint empty = 0xFFFFFFFFu;
            gl.glClearTexImage(head, 0, glc::RED_INTEGER, glc::UNSIGNED_INT, &empty);
            gl.glBindImageTexture(0, head, 0, glc::FALSE_, 0, glc::READ_WRITE, glc::R32UI);
            const GLuint zero = 0;
            kit.buffer(glc::ATOMIC_COUNTER_BUFFER, &zero, 4, glc::DYNAMIC_DRAW);
            const GLuint counter = kit.buffer(glc::ATOMIC_COUNTER_BUFFER, &zero, 4, glc::DYNAMIC_DRAW);
            gl.glBindBufferBase(glc::ATOMIC_COUNTER_BUFFER, 0, counter);
            const GLuint nodes = kit.buffer(glc::SHADER_STORAGE_BUFFER, nullptr, 8192 * 32, glc::DYNAMIC_COPY);
            gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 3, nodes);
            std::vector<V2> tris;
            for (int i = 0; i < 6; ++i) {
                const float a = 1.0f * static_cast<float>(i);
                const float cx = 0.35f * std::cos(a * 1.3f);
                const float cy = 0.35f * std::sin(a * 1.3f);
                tris.push_back({cx + 0.5f * std::cos(a), cy + 0.5f * std::sin(a), 0.9f - 0.12f * static_cast<float>(i), 0.3f + 0.1f * static_cast<float>(i), 0.3f});
                tris.push_back({cx + 0.5f * std::cos(a + 2.1f), cy + 0.5f * std::sin(a + 2.1f), 0.9f - 0.12f * static_cast<float>(i), 0.3f + 0.1f * static_cast<float>(i), 0.3f});
                tris.push_back({cx + 0.5f * std::cos(a + 4.2f), cy + 0.5f * std::sin(a + 4.2f), 0.9f - 0.12f * static_cast<float>(i), 0.3f + 0.1f * static_cast<float>(i), 0.3f});
            }
            gl.glBindVertexArray(Flat2d::upload(kit, tris));
            gl.glUseProgram(program);
            gl.glColorMask(0, 0, 0, 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 18);
            gl.glColorMask(1, 1, 1, 1);
            gl.glMemoryBarrier(glc::SHADER_IMAGE_ACCESS_BARRIER_BIT | glc::SHADER_STORAGE_BARRIER_BIT);
            gl.glBindVertexArray(rig.vao);
            rig.draw(kit, resolve_program);
            return Gx::shot(kit, rig.target, out, error);
        });

        Cases::multi(CaseSpec("scene", "legacy_game_frame_lists_fog_multitexture").compat_profile().frames(3, {0, 2}).tolerance(Tolerance::tight(2)), [] {
            struct State {
                Target target;
                GLuint tree = 0;
                GLuint lightmap = 0;
                GLuint detail = 0;
            };
            auto s = std::make_shared<State>();
            Fns fns;
            fns.setup = [s](Kit& kit, std::string& error) {
                Api& gl = kit.gl;
                if (!Legacy::open(kit, s->target, error)) {
                    return false;
                }
                s->tree = gl.glGenLists(1);
                gl.glNewList(s->tree, glc::COMPILE);
                gl.glBegin(glc::TRIANGLES);
                gl.glColor3f(0.4f, 0.25f, 0.1f);
                gl.glVertex3f(-0.1f, 0.0f, 0.0f);
                gl.glVertex3f(0.1f, 0.0f, 0.0f);
                gl.glVertex3f(0.0f, 0.6f, 0.0f);
                gl.glColor3f(0.1f, 0.6f, 0.2f);
                gl.glVertex3f(-0.4f, 0.4f, 0.0f);
                gl.glVertex3f(0.4f, 0.4f, 0.0f);
                gl.glVertex3f(0.0f, 1.3f, 0.0f);
                gl.glEnd();
                gl.glEndList();
                s->lightmap = Gx::make_texture(kit, 32, 1);
                s->detail = Gx::make_texture(kit, 32, 0);
                return kit.check("legacy setup", error);
            };
            fns.frame = [s](Kit& kit, unsigned frame, std::string& error) {
                Api& gl = kit.gl;
                s->target.bind(kit);
                const float fog[4] = {0.55f, 0.6f, 0.7f, 1.0f};
                gl.glClearColor(fog[0], fog[1], fog[2], 1);
                gl.glClear(glc::COLOR_BUFFER_BIT | glc::DEPTH_BUFFER_BIT);
                Legacy::perspective(kit, 12.0f, 8.0f * static_cast<float>(frame), 2.0f);
                gl.glTranslatef(0.0f, -0.6f, 1.0f - 1.2f * static_cast<float>(frame));
                gl.glEnable(glc::DEPTH_TEST);
                gl.glEnable(glc::FOG);
                gl.glFogfv(glc::FOG_COLOR, fog);
                gl.glFogi(glc::FOG_MODE, static_cast<GLint>(glc::EXP2));
                gl.glFogf(glc::FOG_DENSITY, 0.22f);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glBindTexture(glc::TEXTURE_2D, s->lightmap);
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::REPLACE));
                gl.glActiveTexture(glc::TEXTURE1);
                gl.glBindTexture(glc::TEXTURE_2D, s->detail);
                gl.glEnable(glc::TEXTURE_2D);
                gl.glTexEnvi(glc::TEXTURE_ENV, glc::TEXTURE_ENV_MODE, static_cast<GLint>(glc::MODULATE));
                gl.glBegin(glc::QUADS);
                for (int i = 0; i < 4; ++i) {
                    const float x = (i == 1 || i == 2) ? 3.0f : -3.0f;
                    const float z = (i >= 2) ? -14.0f : 1.0f;
                    gl.glMultiTexCoord2f(glc::TEXTURE0, x * 0.2f, z * 0.2f);
                    gl.glMultiTexCoord2f(glc::TEXTURE1, x, z);
                    gl.glVertex3f(x, 0.0f, z);
                }
                gl.glEnd();
                gl.glActiveTexture(glc::TEXTURE1);
                gl.glDisable(glc::TEXTURE_2D);
                gl.glActiveTexture(glc::TEXTURE0);
                gl.glDisable(glc::TEXTURE_2D);
                for (int i = 0; i < 12; ++i) {
                    gl.glPushMatrix();
                    gl.glTranslatef(-2.0f + 1.1f * static_cast<float>(i % 4), 0.0f, -1.0f - 1.6f * static_cast<float>(i / 4) - 0.3f * static_cast<float>(i % 3));
                    gl.glScalef(0.8f + 0.1f * static_cast<float>(i % 3), 0.8f + 0.1f * static_cast<float>(i % 3), 1.0f);
                    gl.glCallList(s->tree);
                    gl.glPopMatrix();
                }
                gl.glDisable(glc::FOG);
                gl.glDisable(glc::DEPTH_TEST);
                gl.glMatrixMode(glc::PROJECTION);
                gl.glLoadIdentity();
                gl.glMatrixMode(glc::MODELVIEW);
                gl.glLoadIdentity();
                gl.glColor3f(0.9f, 0.9f, 0.2f);
                gl.glRectf(-0.95f, 0.85f, -0.95f + 0.2f * static_cast<float>(frame + 1), 0.92f);
                return kit.check("legacy frame", error);
            };
            fns.capture = [s](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, s->target, out, error); };
            return fns;
        });

        Cases::single(CaseSpec("scene", "legacy_cad_wireframe_stencil_selection").compat_profile().tolerance(Tolerance::raster(2, 1)), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!Legacy::open(kit, target, error)) {
                return false;
            }
            Api& gl = kit.gl;
            Legacy::perspective(kit, 25.0f, 30.0f, 4.0f);
            gl.glEnable(glc::DEPTH_TEST);
            const Shape part = Shape::torus(20, 10, 0.8f, 0.35f);
            const Shape block = Shape::cube(0.5f);
            gl.glColor3f(0.55f, 0.6f, 0.7f);
            gl.glEnable(glc::POLYGON_OFFSET_FILL);
            gl.glPolygonOffset(1.0f, 1.0f);
            const float* tv = block.vertices.data();
            const auto draw_part = [&](const Shape& shape) {
                gl.glBegin(glc::TRIANGLES);
                for (std::uint32_t index : shape.indices) {
                    gl.glVertex3fv(&shape.vertices[index * 8]);
                }
                gl.glEnd();
            };
            static_cast<void>(tv);
            draw_part(part);
            gl.glPushMatrix();
            gl.glTranslatef(0.9f, -0.5f, 0.5f);
            draw_part(block);
            gl.glPopMatrix();
            gl.glDisable(glc::POLYGON_OFFSET_FILL);
            gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
            gl.glColor3f(0.05f, 0.05f, 0.1f);
            gl.glLineWidth(1.0f);
            draw_part(part);
            gl.glPushMatrix();
            gl.glTranslatef(0.9f, -0.5f, 0.5f);
            draw_part(block);
            gl.glPopMatrix();
            gl.glPolygonMode(glc::FRONT_AND_BACK, glc::FILL);
            gl.glEnable(glc::STENCIL_TEST);
            gl.glClearStencil(0);
            gl.glClear(glc::STENCIL_BUFFER_BIT);
            gl.glStencilFunc(glc::ALWAYS, 1, 0xFF);
            gl.glStencilOp(glc::KEEP, glc::KEEP, glc::REPLACE);
            gl.glColorMask(0, 0, 0, 0);
            gl.glPushMatrix();
            gl.glTranslatef(0.9f, -0.5f, 0.5f);
            draw_part(block);
            gl.glPopMatrix();
            gl.glColorMask(1, 1, 1, 1);
            gl.glStencilFunc(glc::NOTEQUAL, 1, 0xFF);
            gl.glDisable(glc::DEPTH_TEST);
            gl.glPolygonMode(glc::FRONT_AND_BACK, glc::LINE);
            gl.glLineWidth(4.0f);
            gl.glColor3f(1.0f, 0.7f, 0.1f);
            gl.glPushMatrix();
            gl.glTranslatef(0.9f, -0.5f, 0.5f);
            gl.glScalef(1.12f, 1.12f, 1.12f);
            draw_part(block);
            gl.glPopMatrix();
            gl.glPolygonMode(glc::FRONT_AND_BACK, glc::FILL);
            gl.glDisable(glc::STENCIL_TEST);
            gl.glEnable(glc::LINE_STIPPLE);
            gl.glLineStipple(2, 0x3333);
            gl.glLineWidth(1.0f);
            gl.glColor3f(0.9f, 0.2f, 0.2f);
            gl.glBegin(glc::LINES);
            gl.glVertex3f(-1.6f, -1.0f, 0.0f);
            gl.glVertex3f(1.6f, 1.0f, 0.0f);
            gl.glVertex3f(-1.6f, 1.0f, 0.0f);
            gl.glVertex3f(1.6f, -1.0f, 0.0f);
            gl.glEnd();
            return Gx::shot(kit, target, out, error);
        });
    }
} kBatch22;

}  // namespace

}  // namespace lsw::render::gl
