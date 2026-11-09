#include "render/gl/kit.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lsw::render::gl {

namespace {
void LSW_GLAPI debug_message(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void*) {
    if (severity == 0x826B) {
        return;
    }
    std::fprintf(stderr, "GLDEBUG source=0x%x type=0x%x id=%u severity=0x%x: %.*s\n", source, type, id, severity, length, message);
}
}  // namespace

Kit::~Kit() { close(); }

void Kit::close() {
    for (auto it = cleanups_.rbegin(); it != cleanups_.rend(); ++it) {
        (*it)();
    }
    cleanups_.clear();
}

OpenResult Kit::open(platform::GlPlatform& host, const KitOptions& options, std::string& error) {
    platform = &host;
    width = options.width;
    height = options.height;
    platform::GlRequest request;
    request.core = options.core;
    request.major = options.major;
    request.minor = options.minor;
    request.width = options.width;
    request.height = options.height;
    request.window = options.window;
    if (!host.open(request, error)) {
        return OpenResult::Failed;
    }
    if (!gl.load([&host](const char* name) { return host.get_proc(name); }, error)) {
        return OpenResult::Failed;
    }
    const auto text = [&](GLenum name) {
        const GLubyte* s = gl.glGetString(name);
        return s != nullptr ? std::string(reinterpret_cast<const char*>(s)) : std::string();
    };
    vendor = text(glc::VENDOR);
    renderer = text(glc::RENDERER);
    version = text(glc::VERSION);
    gl.glGetIntegerv(glc::MAJOR_VERSION, &major);
    gl.glGetIntegerv(glc::MINOR_VERSION, &minor);
    uuid = host.device_uuid();
    if (!at_least(options.major, options.minor)) {
        error = "context version " + version + " is below the requested " + std::to_string(options.major) + "." +
                std::to_string(options.minor);
        return OpenResult::Unsupported;
    }
    for (const std::string& extension : options.extensions) {
        if (!has_extension(extension.c_str())) {
            error = "extension " + extension + " is not exposed";
            return OpenResult::Unsupported;
        }
    }
    if (std::getenv("LSW_GL_DEBUG") != nullptr && gl.glDebugMessageCallback != nullptr) {
        gl.glEnable(glc::DEBUG_OUTPUT);
        gl.glEnable(glc::DEBUG_OUTPUT_SYNCHRONOUS);
        gl.glDebugMessageCallback(&debug_message, nullptr);
    }
    gl.glPixelStorei(glc::PACK_ALIGNMENT, 1);
    gl.glPixelStorei(glc::UNPACK_ALIGNMENT, 1);
    while (gl.glGetError() != glc::NO_ERROR_) {
    }
    return OpenResult::Ok;
}

bool Kit::at_least(int wanted_major, int wanted_minor) const {
    return major > wanted_major || (major == wanted_major && minor >= wanted_minor);
}

bool Kit::check(const char* where, std::string& error) {
    if (!pending_.empty()) {
        error = std::string(where) + ": " + pending_;
        pending_.clear();
        while (gl.glGetError() != glc::NO_ERROR_) {
        }
        return false;
    }
    const GLenum code = gl.glGetError();
    if (code == glc::NO_ERROR_) {
        return true;
    }
    error = std::string(where) + " raised " + Api::error_name(code);
    while (gl.glGetError() != glc::NO_ERROR_) {
    }
    return false;
}

int Kit::integer(GLenum name) {
    GLint value = 0;
    gl.glGetIntegerv(name, &value);
    return value;
}

bool Kit::has_extension(const char* name) const {
    GLint count = 0;
    gl.glGetIntegerv(glc::NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const GLubyte* s = gl.glGetStringi(glc::EXTENSIONS, static_cast<GLuint>(i));
        if (s != nullptr && std::strcmp(reinterpret_cast<const char*>(s), name) == 0) {
            return true;
        }
    }
    return false;
}

GLuint Kit::shader(GLenum stage, const char* source, std::string& error) {
    const GLuint id = gl.glCreateShader(stage);
    gl.glShaderSource(id, 1, &source, nullptr);
    gl.glCompileShader(id);
    GLint ok = 0;
    gl.glGetShaderiv(id, glc::COMPILE_STATUS, &ok);
    if (ok == 0) {
        char log[2048] = {};
        gl.glGetShaderInfoLog(id, sizeof log - 1, nullptr, log);
        char name[32];
        std::snprintf(name, sizeof name, "stage 0x%x", stage);
        error = std::string(name) + " shader failed to compile: " + log;
        gl.glDeleteShader(id);
        return 0;
    }
    return id;
}

GLuint Kit::link(const std::vector<GLuint>& shaders, std::string& error, bool separable) {
    const GLuint program = gl.glCreateProgram();
    if (separable) {
        gl.glProgramParameteri(program, glc::PROGRAM_SEPARABLE, 1);
    }
    for (GLuint id : shaders) {
        gl.glAttachShader(program, id);
    }
    gl.glLinkProgram(program);
    for (GLuint id : shaders) {
        gl.glDeleteShader(id);
    }
    GLint ok = 0;
    gl.glGetProgramiv(program, glc::LINK_STATUS, &ok);
    if (ok == 0) {
        char log[2048] = {};
        gl.glGetProgramInfoLog(program, sizeof log - 1, nullptr, log);
        error = std::string("program failed to link: ") + log;
        gl.glDeleteProgram(program);
        return 0;
    }
    defer([this, program] { gl.glDeleteProgram(program); });
    return program;
}

GLuint Kit::program(std::initializer_list<ShaderSource> stages, std::string& error, bool separable) {
    std::vector<GLuint> shaders;
    for (const ShaderSource& stage : stages) {
        const GLuint id = shader(stage.stage, stage.text, error);
        if (id == 0) {
            for (GLuint other : shaders) {
                gl.glDeleteShader(other);
            }
            return 0;
        }
        shaders.push_back(id);
    }
    return link(shaders, error, separable);
}

GLuint Kit::program(const char* vertex_source, const char* fragment_source, std::string& error) {
    return program({{glc::VERTEX_SHADER, vertex_source}, {glc::FRAGMENT_SHADER, fragment_source}}, error);
}

void Kit::u1i(GLuint program, const char* name, GLint v) {
    const GLint location = uniform(program, name, pending_);
    gl.glUseProgram(program);
    if (location >= 0) gl.glUniform1i(location, v);
}

void Kit::u1f(GLuint program, const char* name, GLfloat v) {
    const GLint location = uniform(program, name, pending_);
    gl.glUseProgram(program);
    if (location >= 0) gl.glUniform1f(location, v);
}

void Kit::u2f(GLuint program, const char* name, GLfloat a, GLfloat b) {
    const GLint location = uniform(program, name, pending_);
    gl.glUseProgram(program);
    if (location >= 0) gl.glUniform2f(location, a, b);
}

void Kit::u3f(GLuint program, const char* name, GLfloat a, GLfloat b, GLfloat c) {
    const GLint location = uniform(program, name, pending_);
    gl.glUseProgram(program);
    if (location >= 0) gl.glUniform3f(location, a, b, c);
}

void Kit::u4f(GLuint program, const char* name, GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
    const GLint location = uniform(program, name, pending_);
    gl.glUseProgram(program);
    if (location >= 0) gl.glUniform4f(location, a, b, c, d);
}

void Kit::umat4(GLuint program, const char* name, const GLfloat* m) {
    const GLint location = uniform(program, name, pending_);
    gl.glUseProgram(program);
    if (location >= 0) gl.glUniformMatrix4fv(location, 1, glc::FALSE_, m);
}

GLuint Kit::buffer(GLenum target, const void* data, std::size_t size, GLenum usage) {
    GLuint id = 0;
    gl.glGenBuffers(1, &id);
    gl.glBindBuffer(target, id);
    gl.glBufferData(target, static_cast<GLsizeiptr>(size), data, usage);
    defer([this, id] { gl.glDeleteBuffers(1, &id); });
    return id;
}

GLuint Kit::vertex_array() {
    GLuint id = 0;
    gl.glGenVertexArrays(1, &id);
    gl.glBindVertexArray(id);
    defer([this, id] { gl.glDeleteVertexArrays(1, &id); });
    return id;
}

GLuint Kit::texture2d(unsigned w, unsigned h, GLenum internal, GLenum format, GLenum type,
                      const std::vector<std::vector<std::uint8_t>>& levels, bool generate_mips) {
    GLuint id = 0;
    gl.glGenTextures(1, &id);
    gl.glBindTexture(glc::TEXTURE_2D, id);
    unsigned lw = w;
    unsigned lh = h;
    for (std::size_t level = 0; level < levels.size(); ++level) {
        gl.glTexImage2D(glc::TEXTURE_2D, static_cast<GLint>(level), static_cast<GLint>(internal), static_cast<GLsizei>(lw),
                        static_cast<GLsizei>(lh), 0, format, type, levels[level].data());
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    if (levels.empty()) {
        gl.glTexImage2D(glc::TEXTURE_2D, 0, static_cast<GLint>(internal), static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, format, type,
                        nullptr);
    }
    if (generate_mips) {
        gl.glGenerateMipmap(glc::TEXTURE_2D);
    }
    gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_BASE_LEVEL, 0);
    gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAX_LEVEL, levels.empty() ? 0 : static_cast<GLint>(levels.size() - 1));
    defer([this, id] { gl.glDeleteTextures(1, &id); });
    return id;
}

bool Kit::format_info(GLenum internal, FormatInfo& out) {
    switch (internal) {
        case glc::RGBA8: out = {internal, glc::RGBA, glc::UNSIGNED_BYTE, PixelKind::Rgba8Unorm}; return true;
        case glc::RGBA16F: out = {internal, glc::RGBA, glc::HALF_FLOAT, PixelKind::Rgba16Float}; return true;
        case glc::RGBA32F: out = {internal, glc::RGBA, glc::FLOAT, PixelKind::Rgba32Float}; return true;
        case glc::R32F: out = {internal, glc::RED, glc::FLOAT, PixelKind::R32Float}; return true;
        case glc::RGBA32UI: out = {internal, glc::RGBA_INTEGER, glc::UNSIGNED_INT, PixelKind::Rgba32Uint}; return true;
        case glc::R8: out = {internal, glc::RED, glc::UNSIGNED_BYTE, PixelKind::R8Unorm}; return true;
        case glc::RG8: out = {internal, glc::RG, glc::UNSIGNED_BYTE, PixelKind::Rg8Unorm}; return true;
        case glc::R16F: out = {internal, glc::RED, glc::HALF_FLOAT, PixelKind::R16Float}; return true;
        case glc::RG16F: out = {internal, glc::RG, glc::HALF_FLOAT, PixelKind::Rg16Float}; return true;
        case glc::R16: out = {internal, glc::RED, glc::UNSIGNED_SHORT, PixelKind::R16Unorm}; return true;
        case glc::R16UI: out = {internal, glc::RED_INTEGER, glc::UNSIGNED_SHORT, PixelKind::R16Uint}; return true;
        case glc::RG32F: out = {internal, glc::RG, glc::FLOAT, PixelKind::Rg32Float}; return true;
        case glc::RGBA16: out = {internal, glc::RGBA, glc::UNSIGNED_SHORT, PixelKind::Rgba16Unorm}; return true;
        case glc::RGBA8UI: out = {internal, glc::RGBA_INTEGER, glc::UNSIGNED_BYTE, PixelKind::Rgba8Uint}; return true;
        case glc::RGBA16UI: out = {internal, glc::RGBA_INTEGER, glc::UNSIGNED_SHORT, PixelKind::Rgba16Uint}; return true;
        default: return false;
    }
}

GLint Kit::uniform(GLuint program, const char* name, std::string& error) {
    const GLenum before = gl.glGetError();
    if (before != glc::NO_ERROR_) {
        error = std::string("before glGetUniformLocation(") + name + ") the error flag already held " + Api::error_name(before);
        return -1;
    }
    const GLint location = gl.glGetUniformLocation(program, name);
    const GLenum code = gl.glGetError();
    if (code != glc::NO_ERROR_) {
        error = std::string("glGetUniformLocation(") + name + ") raised " + Api::error_name(code);
        return -1;
    }
    if (location < 0) {
        error = std::string("uniform ") + name + " is not active in the program";
    }
    return location;
}

bool Kit::read_color(GLuint fbo, GLenum read_buffer, GLenum internal, unsigned w, unsigned h, Image& out, std::string& error) {
    FormatInfo info;
    if (!format_info(internal, info)) {
        error = "readback of GL internal format 0x" + [&] { char t[16]; std::snprintf(t, sizeof t, "%x", internal); return std::string(t); }() + " is not supported by the judge";
        return false;
    }
    Image image(w, h, info.kind);
    gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo);
    gl.glReadBuffer(read_buffer);
    gl.glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), info.format, info.type, image.bytes().data());
    if (!check("glReadPixels", error)) {
        return false;
    }
    image.flip_vertical();
    out = std::move(image);
    return true;
}

bool Kit::read_depth(GLuint fbo, unsigned w, unsigned h, Image& out, std::string& error) {
    Image image(w, h, PixelKind::Depth32Float);
    gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo);
    gl.glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), glc::DEPTH_COMPONENT, glc::FLOAT, image.bytes().data());
    if (!check("glReadPixels(depth)", error)) {
        return false;
    }
    image.flip_vertical();
    out = std::move(image);
    return true;
}

bool Kit::read_stencil(GLuint fbo, unsigned w, unsigned h, Image& out, std::string& error) {
    Image image(w, h, PixelKind::R8Uint);
    gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo);
    gl.glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), glc::STENCIL_INDEX, glc::UNSIGNED_BYTE, image.bytes().data());
    if (!check("glReadPixels(stencil)", error)) {
        return false;
    }
    image.flip_vertical();
    out = std::move(image);
    return true;
}

bool Target::create(Kit& kit, const TargetDesc& wanted, std::string& error) {
    desc = wanted;
    Api& gl = kit.gl;
    const bool multisampled = desc.samples > 1;
    gl.glGenFramebuffers(1, &fbo);
    gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    kit.defer([&kit, id = fbo] { kit.gl.glDeleteFramebuffers(1, &id); });
    std::vector<GLenum> draw;
    for (std::size_t i = 0; i < desc.colors.size(); ++i) {
        GLuint id = 0;
        if (multisampled) {
            gl.glGenRenderbuffers(1, &id);
            gl.glBindRenderbuffer(glc::RENDERBUFFER, id);
            gl.glRenderbufferStorageMultisample(glc::RENDERBUFFER, desc.samples, desc.colors[i], static_cast<GLsizei>(desc.width),
                                                static_cast<GLsizei>(desc.height));
            gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(i), glc::RENDERBUFFER, id);
            kit.defer([&kit, id] { kit.gl.glDeleteRenderbuffers(1, &id); });
        } else {
            FormatInfo info;
            if (!Kit::format_info(desc.colors[i], info)) {
                error = "unsupported colour format for a render target";
                return false;
            }
            id = kit.texture2d(desc.width, desc.height, desc.colors[i], info.format, info.type, {}, false);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(i), glc::TEXTURE_2D, id, 0);
        }
        color_buffers.push_back(id);
        draw.push_back(glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
    }
    if (desc.depth != 0) {
        gl.glGenRenderbuffers(1, &depth_buffer);
        gl.glBindRenderbuffer(glc::RENDERBUFFER, depth_buffer);
        if (multisampled) {
            gl.glRenderbufferStorageMultisample(glc::RENDERBUFFER, desc.samples, desc.depth, static_cast<GLsizei>(desc.width),
                                                static_cast<GLsizei>(desc.height));
        } else {
            gl.glRenderbufferStorage(glc::RENDERBUFFER, desc.depth, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
        }
        const bool stencil = desc.depth == glc::DEPTH24_STENCIL8 || desc.depth == glc::DEPTH32F_STENCIL8;
        gl.glFramebufferRenderbuffer(glc::FRAMEBUFFER, stencil ? glc::DEPTH_STENCIL_ATTACHMENT : glc::DEPTH_ATTACHMENT, glc::RENDERBUFFER,
                                     depth_buffer);
        kit.defer([&kit, id = depth_buffer] { kit.gl.glDeleteRenderbuffers(1, &id); });
    }
    gl.glDrawBuffers(static_cast<GLsizei>(draw.size()), draw.data());
    if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
        error = "framebuffer incomplete";
        return false;
    }
    if (multisampled) {
        gl.glGenFramebuffers(1, &resolve_fbo);
        gl.glBindFramebuffer(glc::FRAMEBUFFER, resolve_fbo);
        kit.defer([&kit, id = resolve_fbo] { kit.gl.glDeleteFramebuffers(1, &id); });
        for (std::size_t i = 0; i < desc.colors.size(); ++i) {
            FormatInfo info;
            if (!Kit::format_info(desc.colors[i], info)) {
                error = "unsupported colour format for a resolve target";
                return false;
            }
            const GLuint id = kit.texture2d(desc.width, desc.height, desc.colors[i], info.format, info.type, {}, false);
            gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::NEAREST));
            gl.glFramebufferTexture2D(glc::FRAMEBUFFER, glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(i), glc::TEXTURE_2D, id, 0);
            resolve_textures.push_back(id);
        }
        gl.glDrawBuffers(static_cast<GLsizei>(draw.size()), draw.data());
        if (gl.glCheckFramebufferStatus(glc::FRAMEBUFFER) != glc::FRAMEBUFFER_COMPLETE) {
            error = "resolve framebuffer incomplete";
            return false;
        }
    }
    gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    return kit.check("framebuffer setup", error);
}

void Target::bind(Kit& kit) const {
    kit.gl.glBindFramebuffer(glc::FRAMEBUFFER, fbo);
    kit.gl.glViewport(0, 0, static_cast<GLsizei>(desc.width), static_cast<GLsizei>(desc.height));
}

bool Target::resolve(Kit& kit, std::string& error) {
    if (desc.samples <= 1) {
        return true;
    }
    Api& gl = kit.gl;
    gl.glBindFramebuffer(glc::READ_FRAMEBUFFER, fbo);
    gl.glBindFramebuffer(glc::DRAW_FRAMEBUFFER, resolve_fbo);
    for (std::size_t i = 0; i < desc.colors.size(); ++i) {
        gl.glReadBuffer(glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
        const GLenum draw = glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
        gl.glDrawBuffers(1, &draw);
        gl.glBlitFramebuffer(0, 0, static_cast<GLint>(desc.width), static_cast<GLint>(desc.height), 0, 0, static_cast<GLint>(desc.width),
                             static_cast<GLint>(desc.height), glc::COLOR_BUFFER_BIT, glc::NEAREST);
    }
    return kit.check("glBlitFramebuffer", error);
}

bool Target::read_color(Kit& kit, std::size_t index, Image& out, std::string& error) {
    if (!resolve(kit, error)) {
        return false;
    }
    return kit.read_color(desc.samples > 1 ? resolve_fbo : fbo, glc::COLOR_ATTACHMENT0 + static_cast<GLenum>(index), desc.colors[index],
                          desc.width, desc.height, out, error);
}

bool Target::read_depth(Kit& kit, Image& out, std::string& error) {
    return kit.read_depth(fbo, desc.width, desc.height, out, error);
}

bool Target::read_stencil(Kit& kit, Image& out, std::string& error) {
    return kit.read_stencil(fbo, desc.width, desc.height, out, error);
}

}  // namespace lsw::render::gl
