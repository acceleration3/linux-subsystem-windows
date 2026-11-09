#include "render/gl/flat2d.hpp"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec vtx(const char* name) { return CaseSpec("vtx", name).min_version(4, 6); }

struct MfState {
    Target target;
    GLuint program = 0;
    GLuint vao = 0;
    GLuint buffer = 0;
    void* mapped = nullptr;
    GLsync fences[4] = {};
    std::size_t slot_bytes = 6 * sizeof(V2);
};

std::vector<V2> frame_quad(unsigned frame) {
    const float a = 0.7f * static_cast<float>(frame);
    const float k = 1.0f - 0.06f * static_cast<float>(frame);
    const float cx = 0.6f * std::cos(a);
    const float cy = 0.6f * std::sin(a);
    const float h = 0.2f;
    const float r = 0.2f + 0.07f * static_cast<float>(frame % 5);
    return {{cx - h, cy - h, r * k, 0.3f * k, 0.2f * k}, {cx + h, cy - h, 0.3f * k, 1.0f * k, r * k}, {cx + h, cy + h, 0.2f * k, 0.4f * k, 1.0f * k},
            {cx - h, cy - h, r * k, 0.3f * k, 0.2f * k}, {cx + h, cy + h, 0.2f * k, 0.4f * k, 1.0f * k}, {cx - h, cy + h, 1.0f * k, 1.0f * k, r * k}};
}

using SetupFn = std::function<bool(Kit&, MfState&, std::string&)>;
using UploadFn = std::function<bool(Kit&, MfState&, unsigned, const std::vector<V2>&, GLint&, std::string&)>;

void attach_layout(Kit& kit, GLuint buffer) {
    Api& gl = kit.gl;
    gl.glVertexAttribFormat(0, 2, glc::FLOAT, glc::FALSE_, 0);
    gl.glVertexAttribBinding(0, 0);
    gl.glVertexAttribFormat(1, 3, glc::FLOAT, glc::FALSE_, 8);
    gl.glVertexAttribBinding(1, 0);
    gl.glBindVertexBuffer(0, buffer, 0, sizeof(V2));
    gl.glEnableVertexAttribArray(0);
    gl.glEnableVertexAttribArray(1);
}

void mf_case(const char* name, unsigned frames, std::vector<unsigned> captured, SetupFn setup, UploadFn upload) {
    Cases::multi(vtx(name).frames(frames, std::move(captured)), [setup, upload] {
        auto state = std::make_shared<MfState>();
        Fns fns;
        fns.setup = [state, setup](Kit& kit, std::string& error) {
            if (!state->target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            state->target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            state->program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (state->program == 0) {
                return false;
            }
            kit.u1f(state->program, "u_size", 1.0f);
            state->vao = kit.vertex_array();
            return setup(kit, *state, error);
        };
        fns.frame = [state, upload](Kit& kit, unsigned frame, std::string& error) {
            state->target.bind(kit);
            kit.gl.glUseProgram(state->program);
            kit.gl.glBindVertexArray(state->vao);
            GLint first = 0;
            if (!upload(kit, *state, frame, frame_quad(frame), first, error)) {
                return false;
            }
            kit.gl.glDrawArrays(glc::TRIANGLES, first, 6);
            return kit.check("frame", error);
        };
        fns.capture = [state](Kit& kit, unsigned, CaptureSet& out, std::string& error) { return Gx::shot(kit, state->target, out, error); };
        return fns;
    });
}

struct Capture {
    GLuint program = 0;
    GLuint tfo = 0;
};

bool make_capture(Kit& kit, const char* source, const char* const* varyings, GLsizei count, GLenum mode, Capture& out, std::string& error,
                  const char* geometry = nullptr) {
    Api& gl = kit.gl;
    const GLuint vs = kit.shader(glc::VERTEX_SHADER, source, error);
    if (vs == 0) {
        return false;
    }
    GLuint gs = 0;
    if (geometry != nullptr) {
        gs = kit.shader(glc::GEOMETRY_SHADER, geometry, error);
        if (gs == 0) {
            return false;
        }
    }
    out.program = gl.glCreateProgram();
    gl.glAttachShader(out.program, vs);
    if (gs != 0) {
        gl.glAttachShader(out.program, gs);
    }
    gl.glTransformFeedbackVaryings(out.program, count, varyings, mode);
    gl.glLinkProgram(out.program);
    GLint ok = 0;
    gl.glGetProgramiv(out.program, glc::LINK_STATUS, &ok);
    if (ok == 0) {
        char log[512] = {};
        gl.glGetProgramInfoLog(out.program, sizeof log - 1, nullptr, log);
        error = std::string("feedback program failed to link: ") + log;
        return false;
    }
    gl.glGenTransformFeedbacks(1, &out.tfo);
    kit.defer([&kit, program = out.program, tfo = out.tfo] {
        kit.gl.glDeleteTransformFeedbacks(1, &tfo);
        kit.gl.glDeleteProgram(program);
    });
    return true;
}

const char* kCaptureVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "out vec2 tf_pos;\n"
    "out vec3 tf_col;\n"
    "void main() {\n"
    "    tf_pos = in_pos * vec2(0.8, 0.8);\n"
    "    tf_col = in_col.bgr;\n"
    "    gl_Position = vec4(tf_pos, 0.0, 1.0);\n"
    "}\n";

const char* kStreamVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "out vec3 vc;\n"
    "void main() { vc = in_col; gl_Position = vec4(in_pos, 0.0, 1.0); }\n";

const char* kStreamGeometry =
    "#version 460 core\n"
    "layout(points) in;\n"
    "layout(points, max_vertices = 12) out;\n"
    "in vec3 vc[];\n"
    "layout(stream = 0) out vec2 s0_pos;\n"
    "layout(stream = 0) out vec3 s0_col;\n"
    "layout(stream = 1) out vec2 s1_pos;\n"
    "layout(stream = 1) out vec3 s1_col;\n"
    "void main() {\n"
    "    vec2 c = gl_in[0].gl_Position.xy;\n"
    "    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));\n"
    "    for (int i = 0; i < 6; ++i) {\n"
    "        s0_pos = c + corners[i] * 0.3; s0_col = vc[0] * (0.6 + 0.2 * float(i % 3));\n"
    "        EmitStreamVertex(0);\n"
    "    }\n"
    "    for (int i = 0; i < 6; ++i) {\n"
    "        s1_pos = c * 0.5 + vec2(0.0, -0.45) + corners[i] * vec2(0.12, 0.06); s1_col = vc[0].bgr;\n"
    "        EmitStreamVertex(1);\n"
    "    }\n"
    "}\n";

const struct Batch9 {
    Batch9() {
        mf_case("buffer_orphan_each_frame", 8, {0, 3, 7},
                [](Kit& kit, MfState& s, std::string&) {
                    s.buffer = kit.buffer(glc::ARRAY_BUFFER, nullptr, s.slot_bytes, glc::STREAM_DRAW);
                    attach_layout(kit, s.buffer);
                    return true;
                },
                [](Kit& kit, MfState& s, unsigned, const std::vector<V2>& quad, GLint& first, std::string&) {
                    kit.gl.glBindBuffer(glc::ARRAY_BUFFER, s.buffer);
                    kit.gl.glBufferData(glc::ARRAY_BUFFER, static_cast<GLsizeiptr>(s.slot_bytes), nullptr, glc::STREAM_DRAW);
                    kit.gl.glBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(s.slot_bytes), quad.data());
                    first = 0;
                    return true;
                });

        mf_case("buffer_subdata_ring", 8, {0, 4, 7},
                [](Kit& kit, MfState& s, std::string&) {
                    s.buffer = kit.buffer(glc::ARRAY_BUFFER, nullptr, s.slot_bytes * 3, glc::DYNAMIC_DRAW);
                    attach_layout(kit, s.buffer);
                    return true;
                },
                [](Kit& kit, MfState& s, unsigned frame, const std::vector<V2>& quad, GLint& first, std::string&) {
                    const unsigned slot = frame % 3;
                    kit.gl.glBindBuffer(glc::ARRAY_BUFFER, s.buffer);
                    kit.gl.glBufferSubData(glc::ARRAY_BUFFER, static_cast<GLintptr>(slot * s.slot_bytes), static_cast<GLsizeiptr>(s.slot_bytes), quad.data());
                    first = static_cast<GLint>(slot * 6);
                    return true;
                });

        mf_case("buffer_map_unsynchronized_append", 8, {1, 7},
                [](Kit& kit, MfState& s, std::string&) {
                    s.buffer = kit.buffer(glc::ARRAY_BUFFER, nullptr, s.slot_bytes * 8, glc::DYNAMIC_DRAW);
                    attach_layout(kit, s.buffer);
                    return true;
                },
                [](Kit& kit, MfState& s, unsigned frame, const std::vector<V2>& quad, GLint& first, std::string& error) {
                    kit.gl.glBindBuffer(glc::ARRAY_BUFFER, s.buffer);
                    void* p = kit.gl.glMapBufferRange(glc::ARRAY_BUFFER, static_cast<GLintptr>(frame * s.slot_bytes), static_cast<GLsizeiptr>(s.slot_bytes),
                                                      glc::MAP_WRITE_BIT | glc::MAP_UNSYNCHRONIZED_BIT | glc::MAP_INVALIDATE_RANGE_BIT);
                    if (p == nullptr) {
                        error = "glMapBufferRange returned null";
                        return false;
                    }
                    std::memcpy(p, quad.data(), s.slot_bytes);
                    kit.gl.glUnmapBuffer(glc::ARRAY_BUFFER);
                    first = static_cast<GLint>(frame * 6);
                    return true;
                });

        mf_case("buffer_map_flush_explicit", 8, {2, 7},
                [](Kit& kit, MfState& s, std::string&) {
                    s.buffer = kit.buffer(glc::ARRAY_BUFFER, nullptr, s.slot_bytes * 2, glc::DYNAMIC_DRAW);
                    attach_layout(kit, s.buffer);
                    return true;
                },
                [](Kit& kit, MfState& s, unsigned frame, const std::vector<V2>& quad, GLint& first, std::string& error) {
                    kit.gl.glBindBuffer(glc::ARRAY_BUFFER, s.buffer);
                    const unsigned slot = frame % 2;
                    void* p = kit.gl.glMapBufferRange(glc::ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(s.slot_bytes * 2), glc::MAP_WRITE_BIT | glc::MAP_FLUSH_EXPLICIT_BIT);
                    if (p == nullptr) {
                        error = "glMapBufferRange returned null";
                        return false;
                    }
                    std::memcpy(static_cast<std::uint8_t*>(p) + slot * s.slot_bytes, quad.data(), s.slot_bytes);
                    kit.gl.glFlushMappedBufferRange(glc::ARRAY_BUFFER, static_cast<GLintptr>(slot * s.slot_bytes), static_cast<GLsizeiptr>(s.slot_bytes));
                    kit.gl.glUnmapBuffer(glc::ARRAY_BUFFER);
                    first = static_cast<GLint>(slot * 6);
                    return true;
                });

        const auto persistent = [](const char* name, bool coherent, unsigned frames) {
            mf_case(name, frames, {0, frames / 2, frames - 1},
                    [coherent](Kit& kit, MfState& s, std::string& error) {
                        Api& gl = kit.gl;
                        const GLbitfield flags = glc::MAP_WRITE_BIT | glc::MAP_PERSISTENT_BIT | (coherent ? glc::MAP_COHERENT_BIT : 0);
                        gl.glGenBuffers(1, &s.buffer);
                        gl.glBindBuffer(glc::ARRAY_BUFFER, s.buffer);
                        kit.defer([&kit, id = s.buffer] { kit.gl.glDeleteBuffers(1, &id); });
                        gl.glBufferStorage(glc::ARRAY_BUFFER, static_cast<GLsizeiptr>(s.slot_bytes * 4), nullptr, flags);
                        s.mapped = gl.glMapBufferRange(glc::ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(s.slot_bytes * 4), flags | (coherent ? 0 : glc::MAP_FLUSH_EXPLICIT_BIT));
                        if (s.mapped == nullptr) {
                            error = "persistent mapping returned null";
                            return false;
                        }
                        kit.defer([&kit, id = s.buffer] {
                            kit.gl.glBindBuffer(glc::ARRAY_BUFFER, id);
                            kit.gl.glUnmapBuffer(glc::ARRAY_BUFFER);
                        });
                        attach_layout(kit, s.buffer);
                        return true;
                    },
                    [coherent](Kit& kit, MfState& s, unsigned frame, const std::vector<V2>& quad, GLint& first, std::string& error) {
                        Api& gl = kit.gl;
                        const unsigned slot = frame % 4;
                        if (s.fences[slot] != nullptr) {
                            const GLenum wait = gl.glClientWaitSync(s.fences[slot], glc::SYNC_FLUSH_COMMANDS_BIT, 2000000000ull);
                            if (wait != glc::ALREADY_SIGNALED && wait != glc::CONDITION_SATISFIED) {
                                error = "fence wait did not complete";
                                return false;
                            }
                            gl.glDeleteSync(s.fences[slot]);
                            s.fences[slot] = nullptr;
                        }
                        std::memcpy(static_cast<std::uint8_t*>(s.mapped) + slot * s.slot_bytes, quad.data(), s.slot_bytes);
                        if (!coherent) {
                            gl.glBindBuffer(glc::ARRAY_BUFFER, s.buffer);
                            gl.glFlushMappedBufferRange(glc::ARRAY_BUFFER, static_cast<GLintptr>(slot * s.slot_bytes), static_cast<GLsizeiptr>(s.slot_bytes));
                            gl.glMemoryBarrier(glc::CLIENT_MAPPED_BUFFER_BARRIER_BIT);
                        }
                        first = static_cast<GLint>(slot * 6);
                        return true;
                    });
        };
        persistent("buffer_persistent_coherent_ring_fence", true, 12);
        persistent("buffer_persistent_flush_barrier", false, 10);

        Cases::single(vtx("feedback_separate_attribs"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            Api& gl = kit.gl;
            Capture capture;
            const char* varyings[2] = {"tf_pos", "tf_col"};
            if (!make_capture(kit, kCaptureVertex, varyings, 2, glc::SEPARATE_ATTRIBS, capture, error)) {
                return false;
            }
            const std::vector<V2> tile = Flat2d::tile(1);
            std::vector<V2> verts;
            for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u}) {
                verts.push_back(tile[i]);
            }
            const GLuint vao = Flat2d::upload(kit, verts);
            const GLuint pos_buffer = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 6 * 8, glc::DYNAMIC_COPY);
            const GLuint col_buffer = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 6 * 12, glc::DYNAMIC_COPY);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, capture.tfo);
            gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, pos_buffer);
            gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 1, col_buffer);
            gl.glUseProgram(capture.program);
            gl.glBindVertexArray(vao);
            gl.glEnable(glc::RASTERIZER_DISCARD);
            gl.glBeginTransformFeedback(glc::TRIANGLES);
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            gl.glEndTransformFeedback();
            gl.glDisable(glc::RASTERIZER_DISCARD);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            kit.vertex_array();
            gl.glVertexAttribFormat(0, 2, glc::FLOAT, glc::FALSE_, 0);
            gl.glVertexAttribBinding(0, 0);
            gl.glVertexAttribFormat(1, 3, glc::FLOAT, glc::FALSE_, 0);
            gl.glVertexAttribBinding(1, 1);
            gl.glBindVertexBuffer(0, pos_buffer, 0, 8);
            gl.glBindVertexBuffer(1, col_buffer, 0, 12);
            gl.glEnableVertexAttribArray(0);
            gl.glEnableVertexAttribArray(1);
            gl.glDrawTransformFeedback(glc::TRIANGLES, capture.tfo);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(vtx("feedback_pause_resume"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            Api& gl = kit.gl;
            Capture capture;
            const char* varyings[2] = {"tf_pos", "tf_col"};
            if (!make_capture(kit, kCaptureVertex, varyings, 2, glc::INTERLEAVED_ATTRIBS, capture, error)) {
                return false;
            }
            std::vector<V2> verts;
            for (int i = 0; i < 3; ++i) {
                const float x = -0.9f + 0.8f * static_cast<float>(i);
                const float k = 1.0f - 0.3f * static_cast<float>(i);
                const std::vector<V2> q = {{x, -0.6f, k, 0.3f, 0.2f}, {x + 0.7f, -0.6f, 0.3f, k, 0.2f}, {x + 0.35f, 0.7f, 0.2f, 0.4f, k}};
                verts.insert(verts.end(), q.begin(), q.end());
            }
            const GLuint vao = Flat2d::upload(kit, verts);
            const GLuint buffer = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 9 * sizeof(V2), glc::DYNAMIC_COPY);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, capture.tfo);
            gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
            gl.glUseProgram(capture.program);
            gl.glBindVertexArray(vao);
            gl.glEnable(glc::RASTERIZER_DISCARD);
            gl.glBeginTransformFeedback(glc::TRIANGLES);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glPauseTransformFeedback();
            gl.glDrawArrays(glc::TRIANGLES, 3, 3);
            gl.glResumeTransformFeedback();
            gl.glDrawArrays(glc::TRIANGLES, 6, 3);
            gl.glEndTransformFeedback();
            gl.glDisable(glc::RASTERIZER_DISCARD);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            kit.vertex_array();
            gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
            gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
            gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
            gl.glEnableVertexAttribArray(0);
            gl.glEnableVertexAttribArray(1);
            gl.glDrawTransformFeedback(glc::TRIANGLES, capture.tfo);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(vtx("feedback_primitives_written_query"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            Api& gl = kit.gl;
            Capture capture;
            const char* varyings[2] = {"tf_pos", "tf_col"};
            if (!make_capture(kit, kCaptureVertex, varyings, 2, glc::INTERLEAVED_ATTRIBS, capture, error)) {
                return false;
            }
            std::vector<V2> verts;
            for (int i = 0; i < 4; ++i) {
                const std::vector<V2> q = Flat2d::ring(3, 0.3f, 0.3f);
                for (V2 v : q) {
                    v.x = v.x * 0.5f - 0.6f + 0.4f * static_cast<float>(i);
                    v.y = v.y * 0.5f + 0.4f * static_cast<float>(i % 2) - 0.2f;
                    verts.push_back(v);
                }
            }
            const GLuint vao = Flat2d::upload(kit, verts);
            const GLuint buffer = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 12 * sizeof(V2), glc::DYNAMIC_COPY);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, capture.tfo);
            gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, buffer);
            GLuint query = 0;
            gl.glGenQueries(1, &query);
            kit.defer([&kit, query] { kit.gl.glDeleteQueries(1, &query); });
            gl.glUseProgram(capture.program);
            gl.glBindVertexArray(vao);
            gl.glEnable(glc::RASTERIZER_DISCARD);
            gl.glBeginQuery(glc::TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN, query);
            gl.glBeginTransformFeedback(glc::TRIANGLES);
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            gl.glEndTransformFeedback();
            gl.glEndQuery(glc::TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
            gl.glDisable(glc::RASTERIZER_DISCARD);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
            GLuint written = 0;
            gl.glGetQueryObjectuiv(query, glc::QUERY_RESULT, &written);
            if (written != 3) {
                error = "the primitives-written query returned " + std::to_string(written) + " instead of 3";
                return false;
            }
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            kit.vertex_array();
            gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
            gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
            gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
            gl.glEnableVertexAttribArray(0);
            gl.glEnableVertexAttribArray(1);
            gl.glDrawArrays(glc::TRIANGLES, 0, static_cast<GLsizei>(written * 3));
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(vtx("feedback_buffer_range_offset"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            Api& gl = kit.gl;
            Capture capture;
            const char* varyings[2] = {"tf_pos", "tf_col"};
            if (!make_capture(kit, kCaptureVertex, varyings, 2, glc::INTERLEAVED_ATTRIBS, capture, error)) {
                return false;
            }
            const std::vector<V2> verts = Flat2d::ring(6, 0.35f, 0.9f, 0.4f);
            const GLuint vao = Flat2d::upload(kit, verts);
            const GLsizeiptr offset = 256;
            const GLuint buffer = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, offset + 6 * sizeof(V2), glc::DYNAMIC_COPY);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, capture.tfo);
            gl.glBindBufferRange(glc::TRANSFORM_FEEDBACK_BUFFER, 0, buffer, offset, 6 * sizeof(V2));
            gl.glUseProgram(capture.program);
            gl.glBindVertexArray(vao);
            gl.glEnable(glc::RASTERIZER_DISCARD);
            gl.glBeginTransformFeedback(glc::TRIANGLES);
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            gl.glEndTransformFeedback();
            gl.glDisable(glc::RASTERIZER_DISCARD);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            kit.vertex_array();
            gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
            gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(offset));
            gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(offset + 8));
            gl.glEnableVertexAttribArray(0);
            gl.glEnableVertexAttribArray(1);
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(vtx("feedback_geometry_streams"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            Api& gl = kit.gl;
            Capture capture;
            const char* varyings[5] = {"s0_pos", "s0_col", "gl_NextBuffer", "s1_pos", "s1_col"};
            if (!make_capture(kit, kStreamVertex, varyings, 5, glc::INTERLEAVED_ATTRIBS, capture, error, kStreamGeometry)) {
                return false;
            }
            const std::vector<V2> points = {{-0.5f, 0.3f, 1.0f, 0.5f, 0.2f}, {0.5f, 0.3f, 0.2f, 0.8f, 0.9f}};
            const GLuint vao = Flat2d::upload(kit, points);
            const GLuint buffer0 = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 12 * sizeof(V2), glc::DYNAMIC_COPY);
            const GLuint buffer1 = kit.buffer(glc::TRANSFORM_FEEDBACK_BUFFER, nullptr, 12 * sizeof(V2), glc::DYNAMIC_COPY);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, capture.tfo);
            gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 0, buffer0);
            gl.glBindBufferBase(glc::TRANSFORM_FEEDBACK_BUFFER, 1, buffer1);
            gl.glUseProgram(capture.program);
            gl.glBindVertexArray(vao);
            gl.glEnable(glc::RASTERIZER_DISCARD);
            gl.glBeginTransformFeedback(glc::POINTS);
            gl.glDrawArrays(glc::POINTS, 0, 2);
            gl.glEndTransformFeedback();
            gl.glDisable(glc::RASTERIZER_DISCARD);
            gl.glBindTransformFeedback(glc::TRANSFORM_FEEDBACK, 0);
            const GLuint program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.u1f(program, "u_size", 1.0f);
            for (GLuint stream = 0; stream < 2; ++stream) {
                kit.vertex_array();
                gl.glBindBuffer(glc::ARRAY_BUFFER, stream == 0 ? buffer0 : buffer1);
                gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
                gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
                gl.glEnableVertexAttribArray(0);
                gl.glEnableVertexAttribArray(1);
                gl.glDrawTransformFeedbackStream(glc::TRIANGLES, capture.tfo, stream);
            }
            return Gx::shot(kit, target, out, error);
        });
    }
} kBatch9;

}  // namespace

}  // namespace lsw::render::gl
