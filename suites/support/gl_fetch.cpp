#include "support/gl_fetch.hpp"

#include "support/gl_program.hpp"
#include "support/gl_target.hpp"

namespace lsw::suites {

bool GlFetch::run(const GlApi& api, const GlFetchRequest& request, std::vector<float>& out, std::string& error) {
    const std::string vertex = "#version 330 core\nvoid main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
    const std::string fragment = "#version 330 core\nuniform " + request.sampler + " tex;\nuniform ivec2 params;\nout vec4 color;\n"
                                 "void main() { ivec2 p = ivec2(gl_FragCoord.xy); color = vec4(" + request.expression + "); }\n";
    std::string blame;
    const auto step = [&](const char* name) {
        for (int i = 0; i < 16; ++i) {
            const unsigned failure = api.glGetError();
            if (failure == 0) {
                break;
            }
            blame += std::string(blame.empty() ? "" : ", ") + GlApi::error_name(failure) + " after " + name;
        }
    };
    step("earlier calls (before the sampling pass)");
    GlProgram program;
    if (!program.build(api, vertex, fragment, error)) {
        return false;
    }
    step("building the program");
    GlTarget target;
    if (!target.create(api, request.width, request.height, glc::RGBA32F, false, error)) {
        return false;
    }
    step("creating the target");
    unsigned vao = 0;
    api.glGenVertexArrays(1, &vao);
    api.glBindVertexArray(vao);
    step("creating the vertex array");
    api.glViewport(0, 0, request.width, request.height);
    api.glDisable(glc::BLEND);
    api.glDisable(glc::DEPTH_TEST);
    api.glDisable(glc::SCISSOR_TEST);
    api.glColorMask(1, 1, 1, 1);
    step("setting the viewport and fixed state");
    program.use();
    step("using the program");
    api.glActiveTexture(glc::TEXTURE0);
    api.glBindTexture(request.target, request.texture);
    step("binding the texture");
    api.glUniform1i(program.uniform("tex"), 0);
    api.glUniform2i(program.uniform("params"), request.first, request.second);
    step("setting the uniforms");
    api.glDrawArrays(glc::TRIANGLES, 0, 3);
    step("the draw");
    out.assign(static_cast<std::size_t>(request.width) * static_cast<std::size_t>(request.height) * 4, -1.0f);
    api.glBindFramebuffer(glc::READ_FRAMEBUFFER, target.fbo());
    api.glReadPixels(0, 0, request.width, request.height, glc::RGBA, glc::FLOAT, out.data());
    step("the read-back");
    api.glBindTexture(request.target, 0);
    api.glUseProgram(0);
    api.glBindVertexArray(0);
    api.glDeleteVertexArrays(1, &vao);
    step("cleanup");
    if (!blame.empty()) {
        error = "sampling the texture gave " + blame;
        return false;
    }
    return true;
}

}  // namespace lsw::suites
