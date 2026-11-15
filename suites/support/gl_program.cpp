#include "support/gl_program.hpp"

#include "core/text.hpp"

namespace lsw::suites {

unsigned GlProgram::compile(const GlApi& api, unsigned type, const std::string& source, std::string& error) {
    if (api.glCreateShader == nullptr || api.glShaderSource == nullptr || api.glCompileShader == nullptr) {
        error = "the context has no shader entry points";
        return 0;
    }
    const unsigned shader = api.glCreateShader(type);
    if (shader == 0) {
        error = "glCreateShader(0x" + Text::hex(type, 4) + ") returned 0, " + GlApi::error_name(api.glGetError());
        return 0;
    }
    const char* text = source.c_str();
    api.glShaderSource(shader, 1, &text, nullptr);
    api.glCompileShader(shader);
    int status = 0;
    api.glGetShaderiv(shader, glc::COMPILE_STATUS, &status);
    if (status == 0) {
        char log[2048] = {};
        api.glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        error = std::string("shader compile failed: ") + log;
        api.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool GlProgram::link(const char* const* feedback_varyings, int feedback_count, std::string& error) {
    if (feedback_count > 0) {
        api_->glTransformFeedbackVaryings(program_, feedback_count, feedback_varyings, glc::INTERLEAVED_ATTRIBS);
    }
    api_->glLinkProgram(program_);
    int status = 0;
    api_->glGetProgramiv(program_, glc::LINK_STATUS, &status);
    if (status == 0) {
        char log[2048] = {};
        api_->glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
        error = std::string("program link failed: ") + log;
        destroy();
        return false;
    }
    return true;
}

bool GlProgram::build_stages(const GlApi& api, const std::string& vertex, const std::string& fragment,
                             const char* const* feedback_varyings, int feedback_count, std::string& error) {
    destroy();
    api_ = &api;
    shaders_[0] = compile(api, glc::VERTEX_SHADER, vertex, error);
    if (shaders_[0] == 0) {
        destroy();
        return false;
    }
    if (!fragment.empty()) {
        shaders_[1] = compile(api, glc::FRAGMENT_SHADER, fragment, error);
        if (shaders_[1] == 0) {
            destroy();
            return false;
        }
    }
    program_ = api.glCreateProgram();
    api.glAttachShader(program_, shaders_[0]);
    if (shaders_[1] != 0) {
        api.glAttachShader(program_, shaders_[1]);
    }
    return link(feedback_varyings, feedback_count, error);
}

bool GlProgram::build(const GlApi& api, const std::string& vertex, const std::string& fragment, std::string& error) {
    return build_stages(api, vertex, fragment, nullptr, 0, error);
}

bool GlProgram::build_compute(const GlApi& api, const std::string& compute, std::string& error) {
    destroy();
    api_ = &api;
    shaders_[0] = compile(api, glc::COMPUTE_SHADER, compute, error);
    if (shaders_[0] == 0) {
        destroy();
        return false;
    }
    program_ = api.glCreateProgram();
    api.glAttachShader(program_, shaders_[0]);
    return link(nullptr, 0, error);
}

void GlProgram::destroy() {
    if (api_ == nullptr) {
        return;
    }
    if (program_ != 0) {
        api_->glDeleteProgram(program_);
    }
    for (unsigned& shader : shaders_) {
        if (shader != 0) {
            api_->glDeleteShader(shader);
            shader = 0;
        }
    }
    program_ = 0;
    api_ = nullptr;
}

}  // namespace lsw::suites
