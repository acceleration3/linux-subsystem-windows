#include "render/common/pattern.hpp"
#include "render/gl/gl.hpp"

namespace lsw::render::gl {

namespace {

struct QuadVertex {
    float x;
    float y;
    float u;
    float v;
};

class MippedQuadBlend : public GlSingleScene {
public:
    bool draw(Kit& kit, CaptureSet& out, std::string& error) override {
        constexpr unsigned kSize = 64;
        const unsigned levels = Pattern::level_count(kSize, kSize);
        const Pattern::Bytes base = Pattern::checker(kSize, kSize, 4, Pattern::pack(240, 240, 240), Pattern::pack(220, 40, 40));
        Api& gl = kit.gl;
        const GLuint texture = kit.texture2d(kSize, kSize, glc::RGBA8, glc::RGBA, glc::UNSIGNED_BYTE,
                                             Pattern::mip_chain(base, kSize, kSize, levels), false);
        gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(glc::LINEAR_MIPMAP_LINEAR));
        gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(glc::LINEAR));
        gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_S, static_cast<GLint>(glc::REPEAT));
        gl.glTexParameteri(glc::TEXTURE_2D, glc::TEXTURE_WRAP_T, static_cast<GLint>(glc::REPEAT));
        Target target;
        if (!target.create(kit, TargetDesc{}, error)) {
            return false;
        }
        const GLuint program = kit.program(GlShaders::kTextureVertex, GlShaders::kTextureFragment, error);
        if (program == 0) {
            return false;
        }
        const QuadVertex quads[8] = {
            {-0.9f, -0.9f, 0.0f, 0.0f}, {0.5f, -0.9f, 1.0f, 0.0f}, {-0.9f, 0.5f, 0.0f, 1.0f}, {0.5f, 0.5f, 1.0f, 1.0f},
            {-0.4f, -0.4f, 0.0f, 0.0f}, {0.9f, -0.4f, 6.0f, 0.0f}, {-0.4f, 0.9f, 0.0f, 6.0f}, {0.9f, 0.9f, 6.0f, 6.0f},
        };
        kit.vertex_array();
        kit.buffer(glc::ARRAY_BUFFER, quads, sizeof quads, glc::STATIC_DRAW);
        gl.glVertexAttribPointer(0, 2, glc::FLOAT, glc::FALSE_, sizeof(QuadVertex), nullptr);
        gl.glEnableVertexAttribArray(0);
        gl.glVertexAttribPointer(1, 2, glc::FLOAT, glc::FALSE_, sizeof(QuadVertex), reinterpret_cast<const void*>(8));
        gl.glEnableVertexAttribArray(1);
        target.bind(kit);
        gl.glClearColor(0.15f, 0.3f, 0.55f, 1.0f);
        gl.glClear(glc::COLOR_BUFFER_BIT);
        gl.glEnable(glc::BLEND);
        gl.glBlendFuncSeparate(glc::SRC_ALPHA, glc::ONE_MINUS_SRC_ALPHA, glc::ONE, glc::ONE_MINUS_SRC_ALPHA);
        gl.glUseProgram(program);
        gl.glActiveTexture(glc::TEXTURE0);
        gl.glBindTexture(glc::TEXTURE_2D, texture);
        gl.glUniform1i(0, 0);
        const GLint tint = 1;
        gl.glUniform4f(tint, 1, 1, 1, 1);
        gl.glDrawArrays(glc::TRIANGLE_STRIP, 0, 4);
        gl.glUniform4f(tint, 1, 1, 1, 0.5f);
        gl.glDrawArrays(glc::TRIANGLE_STRIP, 4, 4);
        gl.glFinish();
        Image color;
        if (!kit.check("scene", error) || !target.read_color(kit, 0, color, error)) {
            return false;
        }
        out.add("", std::move(color));
        return true;
    }
};

const GlRegister<MippedQuadBlend> kMippedQuadBlend{CaseSpec("texture", "mipped_quad_blend").tolerance(Tolerance::blend(2))};

}  // namespace

}  // namespace lsw::render::gl
