#pragma once

#include "support/gl_api.hpp"

#include <string>

namespace lsw::suites {
class GlProgram {
public:
    GlProgram() = default;
    GlProgram(const GlProgram&) = delete;
    GlProgram& operator=(const GlProgram&) = delete;
    ~GlProgram() { destroy(); }

    [[nodiscard]] bool build(const GlApi& api, const std::string& vertex, const std::string& fragment, std::string& error);

    [[nodiscard]] bool build_compute(const GlApi& api, const std::string& compute, std::string& error);

    [[nodiscard]] bool build_stages(const GlApi& api, const std::string& vertex, const std::string& fragment,
                                    const char* const* feedback_varyings, int feedback_count, std::string& error);

    void destroy();

    [[nodiscard]] unsigned id() const { return program_; }

    void use() const { api_->glUseProgram(program_); }

    [[nodiscard]] int uniform(const char* name) const { return api_->glGetUniformLocation(program_, name); }

private:
    [[nodiscard]] static unsigned compile(const GlApi& api, unsigned type, const std::string& source, std::string& error);

    [[nodiscard]] bool link(const char* const* feedback_varyings, int feedback_count, std::string& error);

    const GlApi* api_ = nullptr;
    unsigned program_ = 0;
    unsigned shaders_[2] = {0, 0};
};
}  // namespace lsw::suites
