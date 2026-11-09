#include "render/gl/flat2d.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec stage(const char* name) { return CaseSpec("stages", name).min_version(4, 6); }

struct Stage {
    Target target;
    GLuint vao = 0;

    bool open(Kit& kit, std::string& error, unsigned size = 128) {
        TargetDesc desc;
        desc.width = size;
        desc.height = size;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.08f, 0.08f, 0.12f);
        vao = kit.vertex_array();
        return true;
    }
};

GLuint compute_program(Kit& kit, const char* source, std::string& error) { return kit.program({{glc::COMPUTE_SHADER, source}}, error); }

GLuint screen_program(Kit& kit, const char* fragment, std::string& error) { return kit.program(Sources::kScreenVertex, fragment, error); }

const struct Batch11 {
    Batch11() {
        Cases::single(stage("compute_ssbo_feeds_vertex_array"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* cs =
                "#version 460 core\n"
                "layout(local_size_x = 32) in;\n"
                "layout(std430, binding = 0) buffer Out { vec4 data[]; };\n"
                "void main() {\n"
                "    uint i = gl_GlobalInvocationID.x;\n"
                "    float a = 6.2831853 * float(i) / 30.0;\n"
                "    float r = (i % 2u == 0u) ? 0.85 : 0.4;\n"
                "    vec2 p = (i == 0u) ? vec2(0.0) : vec2(cos(a), sin(a)) * r;\n"
                "    data[i * 2u] = vec4(p, 0.0, 1.0);\n"
                "    data[i * 2u + 1u] = vec4(0.5 + 0.5 * cos(a), 0.5 + 0.5 * sin(a * 2.0), 1.0 - float(i) / 32.0, 1.0);\n"
                "}\n";
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec4 in_pos;\nlayout(location = 1) in vec4 in_col;\nout vec4 v_col;\n"
                "void main() { v_col = in_col; gl_Position = in_pos; }\n";
            static const char* fs = "#version 460 core\nin vec4 v_col;\nlayout(location = 0) out vec4 out_color;\nvoid main() { out_color = v_col; }\n";
            const GLuint compute = compute_program(kit, cs, error);
            const GLuint draw = compute != 0 ? kit.program(vs, fs, error) : 0;
            if (draw == 0) {
                return false;
            }
            const GLuint buffer = kit.buffer(glc::SHADER_STORAGE_BUFFER, nullptr, 32 * 32, glc::DYNAMIC_COPY);
            gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, buffer);
            gl.glUseProgram(compute);
            gl.glDispatchCompute(1, 1, 1);
            gl.glMemoryBarrier(glc::VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
            gl.glBindVertexArray(s.vao);
            gl.glBindBuffer(glc::ARRAY_BUFFER, buffer);
            gl.glVertexAttribPointer(0, 4, glc::FLOAT, glc::FALSE_, 32, nullptr);
            gl.glVertexAttribPointer(1, 4, glc::FLOAT, glc::FALSE_, 32, reinterpret_cast<const void*>(16));
            gl.glEnableVertexAttribArray(0);
            gl.glEnableVertexAttribArray(1);
            gl.glUseProgram(draw);
            gl.glDrawArrays(glc::TRIANGLE_FAN, 0, 31);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("compute_image_store_dispatch_indirect"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* cs =
                "#version 460 core\n"
                "layout(local_size_x = 8, local_size_y = 8) in;\n"
                "layout(rgba8, binding = 0) writeonly uniform image2D u_image;\n"
                "void main() {\n"
                "    ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
                "    vec2 q = vec2(p) / 64.0;\n"
                "    float v = sin(q.x * 18.0) * cos(q.y * 14.0 + q.x * 6.0);\n"
                "    imageStore(u_image, p, vec4(0.5 + 0.5 * v, q.y, 1.0 - q.x, 1.0));\n"
                "}\n";
            static const char* fs =
                "#version 460 core\nuniform sampler2D u_tex;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { out_color = texelFetch(u_tex, ivec2(gl_FragCoord.xy * 0.5), 0); }\n";
            const GLuint compute = compute_program(kit, cs, error);
            const GLuint draw = compute != 0 ? screen_program(kit, fs, error) : 0;
            if (draw == 0) {
                return false;
            }
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_2D, texture);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::RGBA8, 64, 64);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glBindImageTexture(0, texture, 0, glc::FALSE_, 0, glc::WRITE_ONLY, glc::RGBA8);
            const GLuint groups[3] = {8, 8, 1};
            const GLuint indirect = kit.buffer(glc::DISPATCH_INDIRECT_BUFFER, groups, sizeof groups, glc::STATIC_DRAW);
            static_cast<void>(indirect);
            gl.glUseProgram(compute);
            gl.glDispatchComputeIndirect(0);
            gl.glMemoryBarrier(glc::TEXTURE_FETCH_BARRIER_BIT);
            gl.glUseProgram(draw);
            kit.u1i(draw, "u_tex", 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("compute_shared_memory_reduce"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* cs =
                "#version 460 core\n"
                "layout(local_size_x = 64) in;\n"
                "layout(std430, binding = 0) buffer Out { uint sums[]; };\n"
                "shared uint partial[64];\n"
                "void main() {\n"
                "    uint row = gl_WorkGroupID.x;\n"
                "    uint x = gl_LocalInvocationID.x;\n"
                "    partial[x] = ((x * 2654435761u) ^ (row * 40503u)) % 97u;\n"
                "    barrier();\n"
                "    for (uint stride = 32u; stride > 0u; stride >>= 1) {\n"
                "        if (x < stride) partial[x] += partial[x + stride];\n"
                "        barrier();\n"
                "    }\n"
                "    if (x == 0u) sums[row] = partial[0];\n"
                "}\n";
            static const char* fs =
                "#version 460 core\n"
                "layout(std430, binding = 0) readonly buffer In { uint sums[]; };\n"
                "layout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                "    uint row = uint(c.y * 16.0);\n"
                "    float v = float(sums[row]) / 3500.0;\n"
                "    out_color = c.x < v ? vec4(0.9, 0.5 + 0.03 * float(row), 0.2, 1.0) : vec4(0.1, 0.1, 0.15, 1.0);\n"
                "}\n";
            const GLuint compute = compute_program(kit, cs, error);
            const GLuint draw = compute != 0 ? screen_program(kit, fs, error) : 0;
            if (draw == 0) {
                return false;
            }
            const GLuint buffer = kit.buffer(glc::SHADER_STORAGE_BUFFER, nullptr, 16 * 4, glc::DYNAMIC_COPY);
            gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, buffer);
            gl.glUseProgram(compute);
            gl.glDispatchCompute(16, 1, 1);
            gl.glMemoryBarrier(glc::SHADER_STORAGE_BARRIER_BIT);
            gl.glUseProgram(draw);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("ssbo_atomic_histogram"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* cs =
                "#version 460 core\n"
                "layout(local_size_x = 64) in;\n"
                "layout(std430, binding = 0) buffer Hist { uint bins[16]; uint total; };\n"
                "void main() {\n"
                "    uint i = gl_GlobalInvocationID.x;\n"
                "    uint h = (i * 747796405u + 2891336453u);\n"
                "    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;\n"
                "    uint bin = ((h >> 22u) ^ h) % 16u;\n"
                "    if (bin < 3u) bin = 3u + (i % 3u);\n"
                "    atomicAdd(bins[bin], 1u);\n"
                "    atomicMax(total, i);\n"
                "}\n";
            static const char* fs =
                "#version 460 core\n"
                "layout(std430, binding = 0) readonly buffer Hist { uint bins[16]; uint total; };\n"
                "layout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                "    uint b = uint(c.x * 16.0);\n"
                "    float v = float(bins[b]) / 800.0;\n"
                "    out_color = c.y < v ? vec4(0.3 + float(b) / 24.0, 0.8, 0.4, 1.0) : vec4(0.1, 0.1, 0.15, 1.0);\n"
                "    if (total != 4095u) out_color = vec4(1.0, 0.0, 0.0, 1.0);\n"
                "}\n";
            const GLuint compute = compute_program(kit, cs, error);
            const GLuint draw = compute != 0 ? screen_program(kit, fs, error) : 0;
            if (draw == 0) {
                return false;
            }
            const std::vector<std::uint32_t> zero(17, 0);
            const GLuint buffer = kit.buffer(glc::SHADER_STORAGE_BUFFER, zero.data(), zero.size() * 4, glc::DYNAMIC_COPY);
            gl.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, buffer);
            gl.glUseProgram(compute);
            gl.glDispatchCompute(64, 1, 1);
            gl.glMemoryBarrier(glc::SHADER_STORAGE_BARRIER_BIT);
            gl.glUseProgram(draw);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("atomic_counter_fragment_count"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\n"
                "void main() { gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
            static const char* fs =
                "#version 460 core\nlayout(binding = 0, offset = 0) uniform atomic_uint u_counter;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { atomicCounterIncrement(u_counter); out_color = vec4(0.9, 0.7, 0.2, 1.0); }\n";
            static const char* bar_fs =
                "#version 460 core\nuniform float u_fraction;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { vec2 c = gl_FragCoord.xy / 128.0; out_color = (c.y < 0.12 && c.x < u_fraction) ? vec4(0.2, 0.9, 0.4, 1.0) : vec4(0.0, 0.0, 0.0, 0.0); }\n";
            const GLuint program = kit.program(vs, fs, error);
            const GLuint bar = program != 0 ? screen_program(kit, bar_fs, error) : 0;
            if (bar == 0) {
                return false;
            }
            const GLuint zero = 0;
            const GLuint counter = kit.buffer(glc::ATOMIC_COUNTER_BUFFER, &zero, 4, glc::DYNAMIC_COPY);
            gl.glBindBufferBase(glc::ATOMIC_COUNTER_BUFFER, 0, counter);
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(9, 0.3f, 0.8f));
            gl.glUseProgram(program);
            gl.glBindVertexArray(vao);
            gl.glDrawArrays(glc::TRIANGLES, 0, 9);
            gl.glMemoryBarrier(glc::ATOMIC_COUNTER_BARRIER_BIT | glc::BUFFER_UPDATE_BARRIER_BIT);
            GLuint count = 0;
            gl.glGetBufferSubData(glc::ATOMIC_COUNTER_BUFFER, 0, 4, &count);
            gl.glBindVertexArray(s.vao);
            gl.glEnable(glc::BLEND);
            gl.glBlendFunc(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA);
            kit.u1f(bar, "u_fraction", static_cast<float>(count) / 8192.0f);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("image_atomic_overdraw_heatmap"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\nlayout(location = 0) in vec2 in_pos;\nlayout(location = 1) in vec3 in_col;\n"
                "void main() { gl_Position = vec4(in_pos, 0.0, 1.0); }\n";
            static const char* fs =
                "#version 460 core\nlayout(r32ui, binding = 0) coherent uniform uimage2D u_heat;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { imageAtomicAdd(u_heat, ivec2(gl_FragCoord.xy), 1u); out_color = vec4(0.0); }\n";
            static const char* show =
                "#version 460 core\nuniform usampler2D u_heat;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    uint n = texelFetch(u_heat, ivec2(gl_FragCoord.xy), 0).r;\n"
                "    out_color = vec4(float(n) / 4.0, float(n == 2u), float(n >= 3u) * 0.8, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(vs, fs, error);
            const GLuint display = program != 0 ? screen_program(kit, show, error) : 0;
            if (display == 0) {
                return false;
            }
            GLuint texture = 0;
            gl.glGenTextures(1, &texture);
            kit.defer([&kit, texture] { kit.gl.glDeleteTextures(1, &texture); });
            gl.glBindTexture(glc::TEXTURE_2D, texture);
            gl.glTexStorage2D(glc::TEXTURE_2D, 1, glc::R32UI, 128, 128);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            const GLuint zero = 0;
            gl.glClearTexImage(texture, 0, glc::RED_INTEGER, glc::UNSIGNED_INT, &zero);
            gl.glBindImageTexture(0, texture, 0, glc::FALSE_, 0, glc::READ_WRITE, glc::R32UI);
            const GLuint vao = Flat2d::upload(kit, Flat2d::ring(15, 0.25f, 0.85f, 0.3f));
            gl.glUseProgram(program);
            gl.glBindVertexArray(vao);
            gl.glColorMask(0, 0, 0, 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 15);
            gl.glColorMask(1, 1, 1, 1);
            gl.glMemoryBarrier(glc::SHADER_IMAGE_ACCESS_BARRIER_BIT | glc::TEXTURE_FETCH_BARRIER_BIT);
            gl.glBindVertexArray(s.vao);
            gl.glUseProgram(display);
            kit.u1i(display, "u_heat", 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("ubo_std140_queried_offsets"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\n"
                "layout(location = 0) in vec2 in_pos;\n"
                "layout(location = 1) in vec3 in_col;\n"
                "layout(std140) uniform Frame {\n"
                "    vec4 tints[4];\n"
                "    mat4 transform;\n"
                "    float scale;\n"
                "    vec3 shift;\n"
                "} frame;\n"
                "out vec3 v_col;\n"
                "void main() {\n"
                "    int k = gl_InstanceID % 4;\n"
                "    v_col = in_col * frame.tints[k].rgb;\n"
                "    vec4 p = frame.transform * vec4(in_pos * frame.scale, 0.0, 1.0);\n"
                "    gl_Position = vec4(p.xy + frame.shift.xy + vec2(float(k % 2) - 0.5, float(k / 2) - 0.5) * 0.9, 0.0, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(vs, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            const char* names[4] = {"Frame.tints", "Frame.transform", "Frame.scale", "Frame.shift"};
            GLuint indices[4];
            gl.glGetUniformIndices(program, 4, names, indices);
            GLint offsets[4] = {};
            gl.glGetActiveUniformsiv(program, 4, indices, glc::UNIFORM_OFFSET, offsets);
            const GLuint block = gl.glGetUniformBlockIndex(program, "Frame");
            GLint block_size = 0;
            gl.glGetActiveUniformBlockiv(program, block, glc::UNIFORM_BLOCK_DATA_SIZE, &block_size);
            gl.glUniformBlockBinding(program, block, 3);
            if (block_size <= 0 || indices[0] == glc::INVALID_INDEX) {
                error = "uniform block introspection failed";
                return false;
            }
            const GLint align = kit.integer(glc::UNIFORM_BUFFER_OFFSET_ALIGNMENT);
            const GLint stride = ((block_size + align - 1) / align) * align;
            std::vector<std::uint8_t> data(static_cast<std::size_t>(stride) * 2, 0);
            for (int variant = 0; variant < 2; ++variant) {
                std::uint8_t* base = data.data() + static_cast<std::size_t>(variant) * static_cast<std::size_t>(stride);
                float tints[16];
                for (int i = 0; i < 16; ++i) {
                    tints[i] = (i % 4 == 3) ? 1.0f : 0.4f + 0.15f * static_cast<float>((i + variant * 5) % 5);
                }
                std::memcpy(base + offsets[0], tints, sizeof tints);
                const Mat4 m = Mat4::rotate_z(0.3f + 0.5f * static_cast<float>(variant));
                std::memcpy(base + offsets[1], m.m, sizeof m.m);
                const float scale = variant == 0 ? 0.6f : 0.35f;
                std::memcpy(base + offsets[2], &scale, 4);
                const float shift[3] = {0.0f, variant == 0 ? 0.0f : -0.05f, 0.0f};
                std::memcpy(base + offsets[3], shift, 12);
            }
            const GLuint buffer = kit.buffer(glc::UNIFORM_BUFFER, data.data(), data.size(), glc::STATIC_DRAW);
            const GLuint vao = Flat2d::upload(kit, Flat2d::tile(1));
            const GLushort idx[6] = {0, 1, 2, 0, 2, 3};
            kit.buffer(glc::ELEMENT_ARRAY_BUFFER, idx, sizeof idx, glc::STATIC_DRAW);
            gl.glUseProgram(program);
            gl.glBindVertexArray(vao);
            for (int variant = 0; variant < 2; ++variant) {
                gl.glBindBufferRange(glc::UNIFORM_BUFFER, 3, buffer, static_cast<GLintptr>(variant) * stride, block_size);
                gl.glDrawElementsInstanced(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr, 4);
            }
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("ssbo_vertex_pulling"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\n"
                "struct Item { vec2 center; float size; float pad; vec4 color; };\n"
                "layout(std430, binding = 2) readonly buffer Items { Item items[]; };\n"
                "out vec4 v_col;\n"
                "void main() {\n"
                "    Item it = items[gl_InstanceID];\n"
                "    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;\n"
                "    v_col = it.color;\n"
                "    gl_Position = vec4(it.center + corner * it.size, 0.0, 1.0);\n"
                "}\n";
            static const char* fs = "#version 460 core\nin vec4 v_col;\nlayout(location = 0) out vec4 out_color;\nvoid main() { out_color = v_col; }\n";
            const GLuint program = kit.program(vs, fs, error);
            if (program == 0) {
                return false;
            }
            struct Item {
                float center[2];
                float size;
                float pad;
                float color[4];
            };
            Item items[12];
            for (int i = 0; i < 12; ++i) {
                items[i] = {{-0.7f + 0.28f * static_cast<float>(i % 6), -0.4f + 0.8f * static_cast<float>(i / 6)}, 0.05f + 0.01f * static_cast<float>(i), 0.0f,
                            {0.2f + 0.07f * static_cast<float>(i), 1.0f - 0.07f * static_cast<float>(i), 0.5f, 1.0f}};
            }
            const GLuint buffer = kit.buffer(glc::SHADER_STORAGE_BUFFER, items, sizeof items, glc::STATIC_DRAW);
            gl.glBindBufferRange(glc::SHADER_STORAGE_BUFFER, 2, buffer, 0, sizeof items);
            gl.glUseProgram(program);
            gl.glBindVertexArray(s.vao);
            gl.glDrawArraysInstanced(glc::TRIANGLE_STRIP, 0, 4, 12);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("program_resource_introspection"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* fs =
                "#version 460 core\n"
                "uniform vec4 u_a;\n"
                "uniform vec4 u_b;\n"
                "uniform float u_k[3];\n"
                "uniform vec2 u_grid;\n"
                "layout(location = 0) out vec4 out_color;\n"
                "void main() {\n"
                "    vec2 c = gl_FragCoord.xy / 128.0;\n"
                "    vec4 m = mix(u_a, u_b, step(0.5, fract(c.x * u_grid.x)));\n"
                "    out_color = m * (u_k[0] + u_k[1] * c.y + u_k[2] * c.x);\n"
                "}\n";
            const GLuint program = screen_program(kit, fs, error);
            if (program == 0) {
                return false;
            }
            GLint count = 0;
            gl.glGetProgramInterfaceiv(program, glc::UNIFORM, glc::ACTIVE_RESOURCES, &count);
            gl.glUseProgram(program);
            int visited = 0;
            for (GLint i = 0; i < count; ++i) {
                char name[64] = {};
                GLsizei length = 0;
                gl.glGetProgramResourceName(program, glc::UNIFORM, static_cast<GLuint>(i), sizeof name, &length, name);
                const GLint location = gl.glGetProgramResourceLocation(program, glc::UNIFORM, name);
                if (location < 0) {
                    continue;
                }
                ++visited;
                const std::string n = name;
                if (n == "u_a") gl.glUniform4f(location, 0.9f, 0.4f, 0.2f, 1.0f);
                else if (n == "u_b") gl.glUniform4f(location, 0.2f, 0.5f, 0.9f, 1.0f);
                else if (n == "u_grid") gl.glUniform2f(location, 6.0f, 0.0f);
                else if (n.rfind("u_k", 0) == 0) {
                    const float k[3] = {0.4f, 0.3f, 0.3f};
                    gl.glUniform1fv(location, 3, k);
                }
            }
            if (visited != 4) {
                error = "program resource enumeration visited " + std::to_string(visited) + " uniforms instead of 4";
                return false;
            }
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            return Gx::shot(kit, s.target, out, error);
        });

        Cases::single(stage("mesh_shader_triangle").extension("GL_NV_mesh_shader"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Stage s;
            if (!s.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            static const char* ms =
                "#version 460 core\n"
                "#extension GL_NV_mesh_shader : require\n"
                "layout(local_size_x = 1) in;\n"
                "layout(triangles, max_vertices = 3, max_primitives = 1) out;\n"
                "out PerVertex { vec3 color; } v_out[];\n"
                "void main() {\n"
                "    gl_PrimitiveCountNV = 1;\n"
                "    gl_MeshVerticesNV[0].gl_Position = vec4(-0.8, -0.8, 0.0, 1.0);\n"
                "    gl_MeshVerticesNV[1].gl_Position = vec4(0.8, -0.8, 0.0, 1.0);\n"
                "    gl_MeshVerticesNV[2].gl_Position = vec4(0.0, 0.8, 0.0, 1.0);\n"
                "    v_out[0].color = vec3(1, 0, 0); v_out[1].color = vec3(0, 1, 0); v_out[2].color = vec3(0, 0, 1);\n"
                "    gl_PrimitiveIndicesNV[0] = 0; gl_PrimitiveIndicesNV[1] = 1; gl_PrimitiveIndicesNV[2] = 2;\n"
                "}\n";
            static const char* fs =
                "#version 460 core\n#extension GL_NV_mesh_shader : require\nin PerVertex { vec3 color; } v_in;\nlayout(location = 0) out vec4 out_color;\n"
                "void main() { out_color = vec4(v_in.color, 1.0); }\n";
            const GLuint program = kit.program({{glc::MESH_SHADER_NV, ms}, {glc::FRAGMENT_SHADER, fs}}, error);
            if (program == 0) {
                return false;
            }
            gl.glUseProgram(program);
            gl.glDrawMeshTasksNV(0, 1);
            return Gx::shot(kit, s.target, out, error);
        });
    }
} kBatch11;

}  // namespace

}  // namespace lsw::render::gl
