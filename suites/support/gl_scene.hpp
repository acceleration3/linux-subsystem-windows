#pragma once

#include "support/gl_api.hpp"
#include "support/gl_program.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites {
struct GlRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    std::uint32_t rgba = 0;
};

class GlScene {
public:
    GlScene(int width, int height) : width_(width), height_(height) {}
    GlScene(const GlScene&) = delete;
    GlScene& operator=(const GlScene&) = delete;
    ~GlScene() { release(); }

    void clear_color(std::uint32_t rgba) { clear_ = rgba; }

    void add(const GlRect& rect) { rects_.push_back(rect); }

    void clear_rects() { rects_.clear(); }

    [[nodiscard]] bool draw(const GlApi& api, std::string& error);

    void release();

    [[nodiscard]] std::vector<std::uint8_t> expected() const;

    [[nodiscard]] const std::vector<GlRect>& rects() const { return rects_; }

    [[nodiscard]] int width() const { return width_; }

    [[nodiscard]] int height() const { return height_; }

    struct Vertex {
        float x;
        float y;
        std::uint32_t rgba;
    };

    [[nodiscard]] std::vector<Vertex> vertices() const;

    [[nodiscard]] static const char* vertex_source();

    [[nodiscard]] static const char* fragment_source();

private:
    int width_;
    int height_;
    std::uint32_t clear_ = 0xFF000000u;
    std::vector<GlRect> rects_;
    const GlApi* api_ = nullptr;
    GlProgram program_;
    unsigned vao_ = 0;
    unsigned vbo_ = 0;
};
}  // namespace lsw::suites
