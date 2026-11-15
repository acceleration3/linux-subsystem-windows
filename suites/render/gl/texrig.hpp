#pragma once

#include "render/gl/flat2d.hpp"

namespace lsw::render::gl {

class TexRig {
public:
    Target target;
    GLuint vao = 0;

    bool open(Kit& kit, std::string& error, unsigned size = 128, GLenum color = glc::RGBA8) {
        TargetDesc desc;
        desc.width = size;
        desc.height = size;
        desc.colors = {color};
        if (!target.create(kit, desc, error)) {
            return false;
        }
        target.bind(kit);
        Gx::clear(kit, 0.0f, 0.0f, 0.0f);
        vao = kit.vertex_array();
        return true;
    }

    GLuint program(Kit& kit, const std::string& body, std::string& error, const char* uniforms = "", const char* out_type = "vec4") {
        std::string decls = uniforms;
        std::string preamble;
        while (decls.rfind("#extension", 0) == 0) {
            const std::size_t end = decls.find('\n');
            preamble += decls.substr(0, end + 1);
            decls.erase(0, end + 1);
        }
        const std::string source = std::string("#version 460 core\n") + preamble + "layout(location = 0) out " + out_type + " out_color;\n" + decls + "\n" + body + "\n";
        return kit.program(Sources::kScreenVertex, source.c_str(), error);
    }

    void draw(Kit& kit, GLuint prog) {
        kit.gl.glUseProgram(prog);
        kit.gl.glBindVertexArray(vao);
        kit.gl.glDrawArrays(glc::TRIANGLES, 0, 3);
    }

    static std::vector<std::uint8_t> cells(unsigned w, unsigned h, unsigned layers = 1, unsigned block = 4) {
        std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * layers * 4);
        for (unsigned z = 0; z < layers; ++z) {
            for (unsigned y = 0; y < h; ++y) {
                for (unsigned x = 0; x < w; ++x) {
                    const unsigned bx = x / block;
                    const unsigned by = y / block;
                    std::uint8_t* p = &out[((static_cast<std::size_t>(z) * h + y) * w + x) * 4];
                    p[0] = static_cast<std::uint8_t>(40 + ((bx * 61 + by * 29 + z * 97) % 200));
                    p[1] = static_cast<std::uint8_t>(30 + ((bx * 17 + by * 83 + z * 31) % 210));
                    p[2] = static_cast<std::uint8_t>(255 * x / (w > 1 ? w - 1 : 1));
                    p[3] = static_cast<std::uint8_t>(255 - (255 * y / (h > 1 ? h - 1 : 1)) / 2 - z * 8);
                }
            }
        }
        return out;
    }

    static GLuint texture(Kit& kit, GLenum target, GLenum filter = glc::NEAREST) {
        GLuint id = 0;
        kit.gl.glGenTextures(1, &id);
        kit.gl.glBindTexture(target, id);
        kit.gl.glTexParameteri(target, glc::TEXTURE_MIN_FILTER, static_cast<GLint>(filter));
        kit.gl.glTexParameteri(target, glc::TEXTURE_MAG_FILTER, static_cast<GLint>(filter == glc::NEAREST ? glc::NEAREST : glc::LINEAR));
        kit.defer([&kit, id] { kit.gl.glDeleteTextures(1, &id); });
        return id;
    }
};

}  // namespace lsw::render::gl
