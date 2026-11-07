#include "sanity/shader/glsl_checks.hpp"

#include "sanity/gl/kit.hpp"
#include "support/gl_program.hpp"
#include "support/gl_target.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lsw::suites::sanity::shader {

namespace {

constexpr unsigned kShadingLanguageVersion = 0x8B8C;
constexpr int kEdge = 16;

int language_version(const GlApi& api) {
    const std::string text = api.text(kShadingLanguageVersion);
    int major = 0;
    int minor = 0;
    if (std::sscanf(text.c_str(), "%d.%d", &major, &minor) != 2) {
        return 0;
    }
    return major * 100 + minor;
}

bool compatibility_context(const GlApi& api) {
    if (api.at_least(3, 2)) {
        return (static_cast<unsigned>(api.integer(glc::CONTEXT_PROFILE_MASK)) & glc::CONTEXT_COMPATIBILITY_PROFILE_BIT) != 0;
    }
    return true;
}

std::string vertex_source(int version) {
    const std::string header = "#version " + std::to_string(version) + "\n";
    if (version < 130) {
        return header + "attribute vec2 pos; varying vec2 uv;\nvoid main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    }
    if (version < 330) {
        return header + "in vec2 pos; out vec2 uv;\nvoid main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
    }
    return header + "layout(location = 0) in vec2 pos; out vec2 uv;\nvoid main() { uv = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }\n";
}

std::string fragment_source(int version) {
    const std::string header = "#version " + std::to_string(version) + "\n";
    if (version < 120) {
        return header + "varying vec2 uv;\nvoid main() { float cell = mod(floor(uv.x * 4.0) + floor(uv.y * 4.0), 2.0); gl_FragColor = vec4(uv.x, uv.y, cell, 1.0); }\n";
    }
    if (version < 130) {
        return header +
               "varying vec2 uv;\nvoid main() { float k[3] = float[3](0.25, 0.5, 0.75); mat2 m = mat2(0.5, 0.0, 0.0, 0.5);\n"
               "  vec2 t = m * (uv * 2.0); gl_FragColor = vec4(t.x, t.y, k[int(mod(floor(uv.x * 4.0), 3.0))], 1.0); }\n";
    }
    const std::string output = version < 330 ? "out vec4 c;\n" : "layout(location = 0) out vec4 c;\n";
    if (version < 400) {
        return header + "in vec2 uv;\n" + output +
               "void main() { uvec2 u = uvec2(uv * 16.0); uint v = (u.x ^ u.y) & 3u; c = vec4(uv.x, uv.y, float(v) / 3.0, 1.0); }\n";
    }
    return header + "in vec2 uv;\n" + output +
           "void main() { uvec2 u = uvec2(uv * 16.0); c = vec4(uv.x, fma(uv.y, 1.0, 0.0), float(bitCount(u.x)) / 4.0, 1.0); }\n";
}

double expected_blue(int version, int x, int y) {
    if (version < 120) {
        return ((x / 4) + (y / 4)) % 2;
    }
    if (version < 130) {
        const double k[3] = {0.25, 0.5, 0.75};
        return k[(x / 4) % 3];
    }
    if (version < 400) {
        return static_cast<double>((x ^ y) & 3) / 3.0;
    }
    int bits = 0;
    for (int v = x; v != 0; v >>= 1) {
        bits += v & 1;
    }
    return bits / 4.0;
}

std::uint8_t byte_of(double value) {
    return static_cast<std::uint8_t>(std::lround(std::min(std::max(value, 0.0), 1.0) * 255.0));
}

bool draw_and_check(const GlApi& api, int version, std::string& problem) {
    GlProgram program;
    std::string error;
    if (!program.build(api, vertex_source(version), fragment_source(version), error)) {
        problem = "#version " + std::to_string(version) + " does not build: " + error;
        return false;
    }
    GlTarget target;
    if (!target.create(api, kEdge, kEdge, glc::RGBA8, false, error)) {
        problem = error;
        return false;
    }
    api.drain_errors();
    const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
    unsigned vao = 0;
    unsigned vbo = 0;
    if (api.glGenVertexArrays != nullptr) {
        api.glGenVertexArrays(1, &vao);
        api.glBindVertexArray(vao);
    }
    api.glGenBuffers(1, &vbo);
    api.glBindBuffer(glc::ARRAY_BUFFER, vbo);
    api.glBufferData(glc::ARRAY_BUFFER, sizeof(quad), quad, glc::STATIC_DRAW);
    int location = api.glGetAttribLocation(program.id(), "pos");
    if (location < 0) {
        location = 0;
    }
    api.glEnableVertexAttribArray(static_cast<unsigned>(location));
    api.glVertexAttribPointer(static_cast<unsigned>(location), 2, glc::FLOAT, 0, 8, nullptr);
    target.bind();
    api.glViewport(0, 0, kEdge, kEdge);
    api.glDisable(glc::BLEND);
    api.glDisable(glc::DEPTH_TEST);
    api.glClearColor(0, 0, 0, 0);
    api.glClear(glc::COLOR_BUFFER_BIT);
    program.use();
    api.glDrawArrays(glc::TRIANGLE_STRIP, 0, 4);
    std::vector<std::uint8_t> pixels;
    const bool read = target.read_rgba(pixels, error);
    api.glFinish();
    api.glUseProgram(0);
    api.glDisableVertexAttribArray(static_cast<unsigned>(location));
    api.glBindBuffer(glc::ARRAY_BUFFER, 0);
    api.glDeleteBuffers(1, &vbo);
    if (vao != 0) {
        api.glBindVertexArray(0);
        api.glDeleteVertexArrays(1, &vao);
    }
    api.glBindFramebuffer(glc::FRAMEBUFFER, 0);
    if (!read) {
        problem = error;
        return false;
    }
    for (int y = 0; y < kEdge; ++y) {
        for (int x = 0; x < kEdge; ++x) {
            const std::uint8_t* pixel = pixels.data() + (static_cast<std::size_t>(y) * kEdge + static_cast<std::size_t>(x)) * 4;
            double red = (x + 0.5) / kEdge;
            double green = (y + 0.5) / kEdge;
            if (version >= 120 && version < 130) {
                red = (x + 0.5) / kEdge;
                green = (y + 0.5) / kEdge;
            }
            const std::uint8_t want[4] = {byte_of(red), byte_of(green), byte_of(expected_blue(version, x, y)), 255};
            for (int c = 0; c < 4; ++c) {
                if (std::abs(static_cast<int>(pixel[c]) - static_cast<int>(want[c])) > 2) {
                    problem = "#version " + std::to_string(version) + " pixel (" + std::to_string(x) + "," + std::to_string(y) + ") channel " + std::to_string(c) + " is " +
                              std::to_string(pixel[c]) + ", expected " + std::to_string(want[c]);
                    return false;
                }
            }
        }
    }
    return true;
}

bool compute_check(const GlApi& api, int version, std::string& problem) {
    const std::string source = "#version " + std::to_string(version) +
                               "\nlayout(local_size_x = 64) in;\nlayout(std430, binding = 0) buffer B { uint b[]; };\n"
                               "void main() { uint i = gl_GlobalInvocationID.x; b[i] = i * i + 7u; }\n";
    GlProgram program;
    std::string error;
    if (!program.build_compute(api, source, error)) {
        problem = "compute #version " + std::to_string(version) + " does not build: " + error;
        return false;
    }
    api.drain_errors();
    unsigned buffer = 0;
    api.glGenBuffers(1, &buffer);
    api.glBindBuffer(glc::SHADER_STORAGE_BUFFER, buffer);
    api.glBufferData(glc::SHADER_STORAGE_BUFFER, 256 * 4, nullptr, glc::DYNAMIC_DRAW);
    api.glBindBufferBase(glc::SHADER_STORAGE_BUFFER, 0, buffer);
    program.use();
    api.glDispatchCompute(4, 1, 1);
    api.glMemoryBarrier(glc::SHADER_STORAGE_BARRIER_BIT | glc::BUFFER_UPDATE_BARRIER_BIT);
    std::vector<std::uint32_t> words(256, 0);
    api.glGetBufferSubData(glc::SHADER_STORAGE_BUFFER, 0, 256 * 4, words.data());
    api.glFinish();
    api.glUseProgram(0);
    api.glBindBuffer(glc::SHADER_STORAGE_BUFFER, 0);
    api.glDeleteBuffers(1, &buffer);
    for (std::uint32_t i = 0; i < 256; ++i) {
        if (words[i] != i * i + 7u) {
            problem = "compute #version " + std::to_string(version) + " word " + std::to_string(i) + " is " + std::to_string(words[i]) + ", expected " + std::to_string(i * i + 7u);
            return false;
        }
    }
    return true;
}

struct ShaderResult {
    unsigned shader = 0;
    bool compiled = false;
    std::string log;
    int log_length = 0;
};

ShaderResult compile(const GlApi& api, unsigned type, const std::string& source) {
    ShaderResult result;
    result.shader = api.glCreateShader(type);
    const char* text = source.c_str();
    api.glShaderSource(result.shader, 1, &text, nullptr);
    api.glCompileShader(result.shader);
    int status = 0;
    api.glGetShaderiv(result.shader, glc::COMPILE_STATUS, &status);
    result.compiled = status != 0;
    api.glGetShaderiv(result.shader, glc::INFO_LOG_LENGTH, &result.log_length);
    if (result.log_length > 0) {
        std::vector<char> log(static_cast<std::size_t>(result.log_length) + 1, 0);
        int written = 0;
        api.glGetShaderInfoLog(result.shader, result.log_length, &written, log.data());
        result.log.assign(log.data());
    }
    return result;
}

}  // namespace

Outcome GlslChecks::versions(Context& context) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (api.glCreateShader == nullptr || api.glGetAttribLocation == nullptr) {
        return Context::skip("the context has no GLSL entry points");
    }
    const int limit = language_version(api);
    if (limit == 0) {
        return Outcome::fail("GL_SHADING_LANGUAGE_VERSION '" + api.text(kShadingLanguageVersion) + "' does not start with major.minor");
    }
    const bool compatibility = compatibility_context(api);
    Problems problems;
    int tested = 0;
    const int versions[] = {110, 120, 130, 140, 150, 330, 400, 410, 420, 430, 440, 450, 460};
    for (int version : versions) {
        if (version > limit) {
            const ShaderResult result = compile(api, glc::FRAGMENT_SHADER, "#version " + std::to_string(version) + "\nvoid main() {}\n");
            if (result.compiled && version > limit + 0) {
                problems.add("#version " + std::to_string(version) + " compiled although GL_SHADING_LANGUAGE_VERSION reports " + std::to_string(limit));
            }
            api.glDeleteShader(result.shader);
            api.drain_errors();
            continue;
        }
        if (version < 330 && !compatibility) {
            continue;
        }
        if (version < 150 && api.at_least(3, 2) && !compatibility) {
            continue;
        }
        ++tested;
        std::string problem;
        if (!draw_and_check(api, version, problem)) {
            problems.add(problem);
        }
        if (version >= 430 && api.glDispatchCompute != nullptr && api.glBindBufferBase != nullptr && !compute_check(api, version, problem)) {
            problems.add(problem);
        }
    }
    context.measure("glsl.language_version", limit);
    context.measure("glsl.versions_tested", tested);
    if (tested == 0) {
        return Context::skip("no GLSL version in the tested list is usable in this context");
    }
    return problems.outcome();
}

Outcome GlslChecks::invalid_diagnostics(Context& context) {
    GlRig rig;
    std::string error;
    if (!gl::Kit::open(rig, context, error, 64, 64)) {
        return Outcome::fail(error);
    }
    const GlApi& api = rig.gl;
    if (api.glCreateShader == nullptr || api.glValidateProgram == nullptr) {
        return Context::skip("the context has no GLSL entry points");
    }
    Problems problems;
    const std::string header = "#version 120\n";
    const struct {
        const char* name;
        std::string source;
    } invalid[] = {{"a syntax error", header + "void main() { gl_FragColor = vec4(1.0 ; }\n"},
                   {"an undeclared identifier", header + "void main() { gl_FragColor = vec4(undeclared_name); }\n"},
                   {"a type mismatch", header + "void main() { vec4 c = 1.0; gl_FragColor = c; }\n"},
                   {"an unsupported version", "#version 999\nvoid main() {}\n"},
                   {"a required extension that does not exist", header + "#extension GL_LSW_does_not_exist : require\nvoid main() { gl_FragColor = vec4(1.0); }\n"},
                   {"a call with the wrong argument count", header + "void main() { gl_FragColor = vec4(sin(1.0, 2.0)); }\n"}};
    for (const auto& item : invalid) {
        api.drain_errors();
        const ShaderResult result = compile(api, glc::FRAGMENT_SHADER, item.source);
        const unsigned raised = api.glGetError();
        if (result.compiled) {
            problems.add(std::string(item.name) + " compiled");
        }
        if (result.log_length <= 1 || result.log.empty()) {
            problems.add(std::string(item.name) + " gave an empty info log (INFO_LOG_LENGTH " + std::to_string(result.log_length) + ")");
        }
        if (raised != 0) {
            problems.add(std::string(item.name) + " raised " + GlApi::error_name(raised) + " besides the compile status");
        }
        api.glDeleteShader(result.shader);
    }
    const std::string good_vertex = "#version 120\nattribute vec2 pos; varying vec4 a;\nvoid main() { a = vec4(pos, 0.0, 1.0); gl_Position = vec4(pos, 0.0, 1.0); }\n";
    const std::string good_fragment = "#version 120\nvarying vec4 a;\nvoid main() { gl_FragColor = a; }\n";
    const struct {
        const char* name;
        std::string vertex;
        std::string fragment;
    } links[] = {{"a fragment shader without main", good_vertex, "#version 120\nvoid helper() {}\n"},
                 {"a vertex shader without main", "#version 120\nvoid helper() {}\n", good_fragment},
                 {"a varying read by the fragment shader but not written by the vertex shader", good_vertex,
                  "#version 120\nvarying vec4 missing;\nvoid main() { gl_FragColor = missing; }\n"},
                 {"a varying declared with different types in the two stages", good_vertex, "#version 120\nvarying vec3 a;\nvoid main() { gl_FragColor = vec4(a, 1.0); }\n"},
                 {"a uniform declared with different types in the two stages", "#version 120\nuniform float u; attribute vec2 pos;\nvoid main() { gl_Position = vec4(pos * u, 0.0, 1.0); }\n",
                  "#version 120\nuniform int u;\nvoid main() { gl_FragColor = vec4(float(u)); }\n"}};
    for (const auto& item : links) {
        api.drain_errors();
        const ShaderResult vertex = compile(api, glc::VERTEX_SHADER, item.vertex);
        const ShaderResult fragment = compile(api, glc::FRAGMENT_SHADER, item.fragment);
        const unsigned program = api.glCreateProgram();
        api.glAttachShader(program, vertex.shader);
        api.glAttachShader(program, fragment.shader);
        api.glLinkProgram(program);
        int status = 1;
        api.glGetProgramiv(program, glc::LINK_STATUS, &status);
        int log_length = 0;
        api.glGetProgramiv(program, glc::INFO_LOG_LENGTH, &log_length);
        if (status != 0) {
            problems.add(std::string(item.name) + " linked");
        }
        if (log_length <= 1) {
            problems.add(std::string(item.name) + " gave an empty program info log (INFO_LOG_LENGTH " + std::to_string(log_length) + ")");
        }
        api.drain_errors();
        api.glUseProgram(program);
        const unsigned use_error = api.glGetError();
        if (status == 0 && use_error != glc::INVALID_OPERATION) {
            problems.add(std::string("glUseProgram on a program that failed to link (") + item.name + ") raised " + GlApi::error_name(use_error) + ", expected GL_INVALID_OPERATION");
        }
        api.drain_errors();
        api.glUseProgram(0);
        api.glDeleteProgram(program);
        api.glDeleteShader(vertex.shader);
        api.glDeleteShader(fragment.shader);
        if (api.glGetError() != 0) {
            problems.add(std::string("deleting the objects of ") + item.name + " raised an error");
        }
    }
    const int version = std::min(120, std::max(110, language_version(api)));
    std::string problem;
    if (compatibility_context(api) && !draw_and_check(api, version, problem)) {
        problems.add("the context does not render correctly after the invalid shaders: " + problem);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::shader
