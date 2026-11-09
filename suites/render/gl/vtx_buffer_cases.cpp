#include "render/gl/flat2d.hpp"

#include <cstring>

namespace lsw::render::gl {

namespace {

CaseSpec vtx(const char* name) { return CaseSpec("vtx", name).min_version(4, 6); }

struct Canvas2d {
    Target target;
    GLuint program = 0;

    bool open(Kit& kit, std::string& error, bool depth = false) {
        TargetDesc desc;
        desc.depth = depth ? glc::DEPTH24_STENCIL8 : 0;
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.08f, 0.08f, 0.12f);
        program = kit.program(Flat2d::kVertex, Flat2d::kFragment, error);
        if (program == 0) {
            return false;
        }
        kit.u1f(program, "u_size", 1.0f);
        return true;
    }
};

std::vector<V2> quad_at(float cx, float cy, float h, float k) {
    return {{cx - h, cy - h, 1.0f * k, 0.3f * k, 0.2f * k}, {cx + h, cy - h, 0.3f * k, 1.0f * k, 0.2f * k}, {cx + h, cy + h, 0.2f * k, 0.4f * k, 1.0f * k},
            {cx - h, cy - h, 1.0f * k, 0.3f * k, 0.2f * k}, {cx + h, cy + h, 0.2f * k, 0.4f * k, 1.0f * k}, {cx - h, cy + h, 1.0f * k, 1.0f * k, 0.3f * k}};
}

void attribute_pair(Kit& kit, GLuint vao_dummy, GLuint buffer, GLintptr offset, GLsizei stride) {
    static_cast<void>(vao_dummy);
    Api& gl = kit.gl;
    gl.glVertexAttribFormat(0, 2, glc::FLOAT, glc::FALSE_, 0);
    gl.glVertexAttribBinding(0, 0);
    gl.glVertexAttribFormat(1, 3, glc::FLOAT, glc::FALSE_, 8);
    gl.glVertexAttribBinding(1, 0);
    gl.glBindVertexBuffer(0, buffer, offset, stride);
    gl.glEnableVertexAttribArray(0);
    gl.glEnableVertexAttribArray(1);
}

const char* kInstanceVertex =
    "#version 460 core\n"
    "layout(location = 0) in vec2 in_pos;\n"
    "layout(location = 1) in vec3 in_col;\n"
    "layout(location = 2) in vec2 in_off;\n"
    "layout(location = 3) in vec3 in_tint;\n"
    "out vec3 v_col;\n"
    "void main() {\n"
    "    v_col = in_col * in_tint;\n"
    "    gl_Position = vec4(in_pos * 0.35 + in_off + vec2(0.0, 0.12 * float(gl_InstanceID % 2)), 0.0, 1.0);\n"
    "}\n";

const struct Batch8 {
    Batch8() {
        Cases::single(vtx("binding_interleaved_offset"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Canvas2d c;
            if (!c.open(kit, error)) {
                return false;
            }
            std::vector<V2> all = quad_at(-0.5f, 0.0f, 0.4f, 1.0f);
            const std::vector<V2> second = quad_at(0.5f, 0.0f, 0.4f, 0.6f);
            all.insert(all.end(), second.begin(), second.end());
            std::vector<V2> padded(3);
            padded.insert(padded.end(), all.begin(), all.end());
            kit.vertex_array();
            const GLuint buffer = kit.buffer(glc::ARRAY_BUFFER, padded.data(), padded.size() * sizeof(V2), glc::STATIC_DRAW);
            attribute_pair(kit, 0, buffer, 3 * sizeof(V2), sizeof(V2));
            kit.gl.glDrawArrays(glc::TRIANGLES, 0, 12);
            return Gx::shot(kit, c.target, out, error);
        });

        Cases::single(vtx("binding_rebind_between_draws"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Canvas2d c;
            if (!c.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            kit.vertex_array();
            GLuint buffers[3];
            for (int i = 0; i < 3; ++i) {
                const std::vector<V2> v = quad_at(-0.6f + 0.6f * static_cast<float>(i), -0.5f + 0.5f * static_cast<float>(i), 0.28f, 1.0f - 0.25f * static_cast<float>(i));
                buffers[i] = kit.buffer(glc::ARRAY_BUFFER, v.data(), v.size() * sizeof(V2), glc::STATIC_DRAW);
            }
            attribute_pair(kit, 0, buffers[0], 0, sizeof(V2));
            for (int i = 0; i < 3; ++i) {
                gl.glBindVertexBuffer(0, buffers[i], 0, sizeof(V2));
                gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            }
            return Gx::shot(kit, c.target, out, error);
        });

        Cases::single(vtx("binding_two_buffers_bind_vertex_buffers"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Canvas2d c;
            if (!c.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const std::vector<V2> quad = quad_at(0.0f, 0.0f, 0.7f, 1.0f);
            std::vector<float> positions;
            std::vector<float> colors;
            for (const V2& v : quad) {
                positions.insert(positions.end(), {v.x, v.y});
                colors.insert(colors.end(), {1.0f - v.r, 1.0f - v.g, 1.0f - v.b, 0.0f});
            }
            kit.vertex_array();
            const GLuint pb = kit.buffer(glc::ARRAY_BUFFER, positions.data(), positions.size() * 4, glc::STATIC_DRAW);
            const GLuint cb = kit.buffer(glc::ARRAY_BUFFER, colors.data(), colors.size() * 4, glc::STATIC_DRAW);
            gl.glVertexAttribFormat(0, 2, glc::FLOAT, glc::FALSE_, 0);
            gl.glVertexAttribBinding(0, 2);
            gl.glVertexAttribFormat(1, 3, glc::FLOAT, glc::FALSE_, 0);
            gl.glVertexAttribBinding(1, 3);
            const GLuint buffers[2] = {pb, cb};
            const GLintptr offsets[2] = {0, 0};
            const GLsizei strides[2] = {8, 16};
            gl.glBindVertexBuffers(2, 2, buffers, offsets, strides);
            gl.glEnableVertexAttribArray(0);
            gl.glEnableVertexAttribArray(1);
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return Gx::shot(kit, c.target, out, error);
        });

        const auto divisor_case = [](const char* name, bool binding_call, GLuint divisor) {
            Cases::single(vtx(name), [=](Kit& kit, CaptureSet& out, std::string& error) {
                Target target;
                if (!target.create(kit, TargetDesc{}, error)) {
                    return false;
                }
                target.bind(kit);
                Gx::clear(kit, 0.08f, 0.08f, 0.12f);
                const GLuint program = kit.program(kInstanceVertex, Flat2d::kFragment, error);
                if (program == 0) {
                    return false;
                }
                kit.gl.glUseProgram(program);
                Api& gl = kit.gl;
                const std::vector<V2> quad = quad_at(0, 0, 1.0f, 1.0f);
                kit.vertex_array();
                kit.buffer(glc::ARRAY_BUFFER, quad.data(), quad.size() * sizeof(V2), glc::STATIC_DRAW);
                gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(V2), nullptr);
                gl.glEnableVertexAttribArray(0);
                gl.glVertexAttribPointer(1, 3, glc::FLOAT, glc::FALSE_, sizeof(V2), reinterpret_cast<const void*>(8));
                gl.glEnableVertexAttribArray(1);
                float instance[8][5];
                for (int i = 0; i < 8; ++i) {
                    instance[i][0] = -0.6f + 0.4f * static_cast<float>(i % 4);
                    instance[i][1] = -0.5f + 1.0f * static_cast<float>(i / 4);
                    instance[i][2] = 0.4f + 0.08f * static_cast<float>(i);
                    instance[i][3] = 1.0f - 0.1f * static_cast<float>(i);
                    instance[i][4] = 0.7f;
                }
                kit.buffer(glc::ARRAY_BUFFER, instance, sizeof instance, glc::STATIC_DRAW);
                gl.glVertexAttribPointer(2, 2, glc::FLOAT, glc::FALSE_, 20, nullptr);
                gl.glVertexAttribPointer(3, 3, glc::FLOAT, glc::FALSE_, 20, reinterpret_cast<const void*>(8));
                gl.glEnableVertexAttribArray(2);
                gl.glEnableVertexAttribArray(3);
                if (binding_call) {
                    gl.glVertexAttribFormat(2, 2, glc::FLOAT, glc::FALSE_, 0);
                    gl.glVertexAttribBinding(2, 1);
                    gl.glVertexAttribFormat(3, 3, glc::FLOAT, glc::FALSE_, 8);
                    gl.glVertexAttribBinding(3, 1);
                    GLint buffer = 0;
                    gl.glGetIntegerv(glc::ARRAY_BUFFER_BINDING, &buffer);
                    gl.glBindVertexBuffer(1, static_cast<GLuint>(buffer), 0, 20);
                    gl.glVertexBindingDivisor(1, divisor);
                } else {
                    gl.glVertexAttribDivisor(2, divisor);
                    gl.glVertexAttribDivisor(3, divisor);
                }
                gl.glDrawArraysInstanced(glc::TRIANGLES, 0, 6, divisor == 1 ? 8 : 16);
                return Gx::shot(kit, target, out, error);
            });
        };
        divisor_case("divisor_attrib_1", false, 1);
        divisor_case("divisor_attrib_2", false, 2);
        divisor_case("divisor_binding_1", true, 1);
        divisor_case("divisor_binding_2", true, 2);

        Cases::single(vtx("dsa_vertex_array"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Canvas2d c;
            if (!c.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const std::vector<V2> tiles = Flat2d::tile(1);
            const GLushort indices[6] = {0, 1, 2, 0, 2, 3};
            GLuint buffers[2] = {};
            gl.glCreateBuffers(2, buffers);
            gl.glNamedBufferStorage(buffers[0], static_cast<GLsizeiptr>(tiles.size() * sizeof(V2)), tiles.data(), 0);
            gl.glNamedBufferStorage(buffers[1], sizeof indices, indices, 0);
            GLuint vao = 0;
            gl.glCreateVertexArrays(1, &vao);
            kit.defer([&kit, vao, b0 = buffers[0], b1 = buffers[1]] {
                kit.gl.glDeleteVertexArrays(1, &vao);
                kit.gl.glDeleteBuffers(1, &b0);
                kit.gl.glDeleteBuffers(1, &b1);
            });
            gl.glVertexArrayVertexBuffer(vao, 0, buffers[0], 0, sizeof(V2));
            gl.glVertexArrayAttribFormat(vao, 0, 2, glc::FLOAT, glc::FALSE_, 0);
            gl.glVertexArrayAttribFormat(vao, 1, 3, glc::FLOAT, glc::FALSE_, 8);
            gl.glVertexArrayAttribBinding(vao, 0, 0);
            gl.glVertexArrayAttribBinding(vao, 1, 0);
            gl.glEnableVertexArrayAttrib(vao, 0);
            gl.glEnableVertexArrayAttrib(vao, 1);
            gl.glVertexArrayElementBuffer(vao, buffers[1]);
            gl.glBindVertexArray(vao);
            gl.glDrawElements(glc::TRIANGLES, 6, glc::UNSIGNED_SHORT, nullptr);
            return Gx::shot(kit, c.target, out, error);
        });

        Cases::single(vtx("generic_attrib_constant_color"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Canvas2d c;
            if (!c.open(kit, error)) {
                return false;
            }
            Api& gl = kit.gl;
            const std::vector<V2> quad = quad_at(0, 0, 0.8f, 1.0f);
            std::vector<float> positions;
            for (const V2& v : quad) {
                positions.insert(positions.end(), {v.x, v.y});
            }
            kit.vertex_array();
            kit.buffer(glc::ARRAY_BUFFER, positions.data(), positions.size() * 4, glc::STATIC_DRAW);
            gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, 0, nullptr);
            gl.glEnableVertexAttribArray(0);
            gl.glDisableVertexAttribArray(1);
            gl.glVertexAttrib4f(1, 0.9f, 0.5f, 0.1f, 1.0f);
            gl.glDrawArrays(glc::TRIANGLES, 0, 3);
            gl.glVertexAttrib3f(1, 0.1f, 0.6f, 0.9f);
            gl.glDrawArrays(glc::TRIANGLES, 3, 3);
            return Gx::shot(kit, c.target, out, error);
        });

        Cases::single(vtx("generic_attrib_integer_constant"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            Api& gl = kit.gl;
            static const char* vs =
                "#version 460 core\n"
                "layout(location = 0) in vec2 in_pos;\n"
                "layout(location = 1) in ivec4 in_value;\n"
                "layout(location = 2) in uvec4 in_mask;\n"
                "out vec3 v_col;\n"
                "void main() {\n"
                "    v_col = vec3(in_value.xyz) / 100.0 + vec3(in_mask.xyz) * 0.25;\n"
                "    gl_Position = vec4(in_pos, 0.0, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(vs, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            gl.glUseProgram(program);
            const float positions[12] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.8f, 0.8f, -0.8f, -0.8f, 0.8f, 0.8f, -0.8f, 0.8f};
            kit.vertex_array();
            kit.buffer(glc::ARRAY_BUFFER, positions, sizeof positions, glc::STATIC_DRAW);
            gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, 0, nullptr);
            gl.glEnableVertexAttribArray(0);
            gl.glVertexAttribI4i(1, 80, 40, 10, 0);
            gl.glVertexAttribI4ui(2, 0, 1, 2, 0);
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return Gx::shot(kit, target, out, error);
        });

        Cases::single(vtx("default_attrib_value"), [](Kit& kit, CaptureSet& out, std::string& error) {
            Target target;
            if (!target.create(kit, TargetDesc{}, error)) {
                return false;
            }
            target.bind(kit);
            Gx::clear(kit, 0.08f, 0.08f, 0.12f);
            static const char* vs =
                "#version 460 core\n"
                "layout(location = 0) in vec2 in_pos;\n"
                "layout(location = 4) in vec4 in_unset;\n"
                "out vec3 v_col;\n"
                "void main() {\n"
                "    v_col = vec3(0.2, 0.6, 0.9) * in_unset.w + in_unset.xyz + vec3(0.3);\n"
                "    gl_Position = vec4(in_pos, 0.0, 1.0);\n"
                "}\n";
            const GLuint program = kit.program(vs, Flat2d::kFragment, error);
            if (program == 0) {
                return false;
            }
            kit.gl.glUseProgram(program);
            const float positions[12] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.8f, 0.8f, -0.8f, -0.8f, 0.8f, 0.8f, -0.8f, 0.8f};
            kit.vertex_array();
            kit.buffer(glc::ARRAY_BUFFER, positions, sizeof positions, glc::STATIC_DRAW);
            kit.gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, 0, nullptr);
            kit.gl.glEnableVertexAttribArray(0);
            kit.gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return Gx::shot(kit, target, out, error);
        });

        const auto buffer_case = [](const char* name, std::function<bool(Kit&, Canvas2d&, std::string&)> body, NeedFn need = {}) {
            Cases::single(vtx(name), [body](Kit& kit, CaptureSet& out, std::string& error) {
                Canvas2d c;
                if (!c.open(kit, error)) {
                    return false;
                }
                kit.vertex_array();
                if (!body(kit, c, error)) {
                    return false;
                }
                return Gx::shot(kit, c.target, out, error);
            }, std::move(need));
        };

        buffer_case("buffer_copy_sub_data", [](Kit& kit, Canvas2d&, std::string&) {
            Api& gl = kit.gl;
            const std::vector<V2> a = quad_at(-0.5f, -0.4f, 0.3f, 1.0f);
            const std::vector<V2> b = quad_at(0.4f, 0.3f, 0.35f, 0.7f);
            std::vector<V2> both = a;
            both.insert(both.end(), b.begin(), b.end());
            const GLuint source = kit.buffer(glc::COPY_READ_BUFFER, both.data(), both.size() * sizeof(V2), glc::STATIC_DRAW);
            const GLuint dest = kit.buffer(glc::COPY_WRITE_BUFFER, nullptr, 18 * sizeof(V2), glc::DYNAMIC_COPY);
            static_cast<void>(source);
            gl.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 6 * sizeof(V2), 0, 6 * sizeof(V2));
            gl.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 6 * sizeof(V2), 6 * sizeof(V2));
            gl.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 0, 12 * sizeof(V2), 3 * sizeof(V2));
            gl.glCopyBufferSubData(glc::COPY_READ_BUFFER, glc::COPY_WRITE_BUFFER, 9 * sizeof(V2), 15 * sizeof(V2), 3 * sizeof(V2));
            gl.glBindBuffer(glc::ARRAY_BUFFER, dest);
            attribute_pair(kit, 0, dest, 0, sizeof(V2));
            gl.glDrawArrays(glc::TRIANGLES, 0, 18);
            return true;
        });

        buffer_case("buffer_get_sub_data_roundtrip", [](Kit& kit, Canvas2d&, std::string&) {
            Api& gl = kit.gl;
            std::vector<V2> v = quad_at(0.0f, 0.0f, 0.6f, 1.0f);
            const GLuint buffer = kit.buffer(glc::ARRAY_BUFFER, v.data(), v.size() * sizeof(V2), glc::DYNAMIC_DRAW);
            std::vector<V2> back(v.size());
            gl.glGetBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(back.size() * sizeof(V2)), back.data());
            for (std::size_t i = 0; i < back.size(); ++i) {
                back[i].x *= 0.5f;
                back[i].y = back[i].y * 0.5f + 0.4f;
                back[i].r = back[i].b;
            }
            gl.glBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(back.size() * sizeof(V2)), back.data());
            attribute_pair(kit, 0, buffer, 0, sizeof(V2));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return true;
        });

        buffer_case("buffer_clear_data_sub_range", [](Kit& kit, Canvas2d&, std::string&) {
            Api& gl = kit.gl;
            std::vector<V2> v = quad_at(0.0f, 0.0f, 0.7f, 1.0f);
            const GLuint buffer = kit.buffer(glc::ARRAY_BUFFER, v.data(), v.size() * sizeof(V2), glc::DYNAMIC_DRAW);
            const float value = 0.35f;
            gl.glClearBufferSubData(glc::ARRAY_BUFFER, glc::R32F, 2 * sizeof(float) * 0 + 8, 12, glc::RED, glc::FLOAT, &value);
            gl.glClearBufferSubData(glc::ARRAY_BUFFER, glc::R32F, 3 * sizeof(V2) + 8, 12, glc::RED, glc::FLOAT, &value);
            attribute_pair(kit, 0, buffer, 0, sizeof(V2));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return true;
        });

        buffer_case("buffer_invalidate_then_refill", [](Kit& kit, Canvas2d&, std::string&) {
            Api& gl = kit.gl;
            const std::vector<V2> first = quad_at(-0.4f, 0.0f, 0.4f, 1.0f);
            const GLuint buffer = kit.buffer(glc::ARRAY_BUFFER, first.data(), first.size() * sizeof(V2), glc::DYNAMIC_DRAW);
            attribute_pair(kit, 0, buffer, 0, sizeof(V2));
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            gl.glInvalidateBufferData(buffer);
            const std::vector<V2> second = quad_at(0.45f, 0.0f, 0.4f, 0.6f);
            gl.glBufferSubData(glc::ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(second.size() * sizeof(V2)), second.data());
            gl.glDrawArrays(glc::TRIANGLES, 0, 6);
            return true;
        });

        buffer_case("buffer_named_dsa_storage_map", [](Kit& kit, Canvas2d&, std::string&) {
            Api& gl = kit.gl;
            GLuint buffer = 0;
            gl.glCreateBuffers(1, &buffer);
            kit.defer([&kit, buffer] { kit.gl.glDeleteBuffers(1, &buffer); });
            gl.glNamedBufferStorage(buffer, 12 * sizeof(V2), nullptr, glc::DYNAMIC_STORAGE_BIT | glc::MAP_WRITE_BIT | glc::MAP_READ_BIT);
            const std::vector<V2> first = quad_at(-0.45f, 0.0f, 0.4f, 1.0f);
            gl.glNamedBufferSubData(buffer, 0, 6 * sizeof(V2), first.data());
            void* mapped = gl.glMapNamedBufferRange(buffer, 6 * sizeof(V2), 6 * sizeof(V2), glc::MAP_WRITE_BIT | glc::MAP_INVALIDATE_RANGE_BIT);
            if (mapped == nullptr) {
                return false;
            }
            const std::vector<V2> second = quad_at(0.45f, 0.0f, 0.4f, 0.5f);
            std::memcpy(mapped, second.data(), second.size() * sizeof(V2));
            gl.glUnmapNamedBuffer(buffer);
            attribute_pair(kit, 0, buffer, 0, sizeof(V2));
            gl.glDrawArrays(glc::TRIANGLES, 0, 12);
            return true;
        });
    }
} kBatch8;

}  // namespace

}  // namespace lsw::render::gl
