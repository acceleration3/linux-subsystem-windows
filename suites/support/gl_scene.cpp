#include "support/gl_scene.hpp"

namespace lsw::suites {

const char* GlScene::vertex_source() {
    return "#version 330 core\n"
           "layout(location = 0) in vec2 position;\n"
           "layout(location = 1) in vec4 color;\n"
           "out vec4 v_color;\n"
           "void main() { gl_Position = vec4(position, 0.0, 1.0); v_color = color; }\n";
}

const char* GlScene::fragment_source() {
    return "#version 330 core\n"
           "in vec4 v_color;\n"
           "layout(location = 0) out vec4 out_color;\n"
           "void main() { out_color = v_color; }\n";
}

std::vector<GlScene::Vertex> GlScene::vertices() const {
    std::vector<Vertex> result;
    result.reserve(rects_.size() * 6);
    const float w = static_cast<float>(width_);
    const float h = static_cast<float>(height_);
    for (const GlRect& rect : rects_) {
        const float x0 = static_cast<float>(rect.x0) / w * 2.0f - 1.0f;
        const float x1 = static_cast<float>(rect.x1) / w * 2.0f - 1.0f;
        const float y0 = static_cast<float>(rect.y0) / h * 2.0f - 1.0f;
        const float y1 = static_cast<float>(rect.y1) / h * 2.0f - 1.0f;
        result.push_back({x0, y0, rect.rgba});
        result.push_back({x1, y0, rect.rgba});
        result.push_back({x1, y1, rect.rgba});
        result.push_back({x0, y0, rect.rgba});
        result.push_back({x1, y1, rect.rgba});
        result.push_back({x0, y1, rect.rgba});
    }
    return result;
}

bool GlScene::draw(const GlApi& api, std::string& error, bool per_rect) {
    if (api_ == nullptr) {
        api_ = &api;
        if (api.glGenVertexArrays == nullptr || !program_.build(api, vertex_source(), fragment_source(), error)) {
            if (error.empty()) {
                error = "the context has no vertex array objects";
            }
            return false;
        }
        api.glGenVertexArrays(1, &vao_);
        api.glGenBuffers(1, &vbo_);
    }
    const std::vector<Vertex> data = vertices();
    api.glBindVertexArray(vao_);
    api.glBindBuffer(glc::ARRAY_BUFFER, vbo_);
    api.glBufferData(glc::ARRAY_BUFFER, static_cast<std::ptrdiff_t>(data.size() * sizeof(Vertex)), data.data(), glc::STATIC_DRAW);
    api.glEnableVertexAttribArray(0);
    api.glVertexAttribPointer(0, 2, glc::FLOAT, 0, sizeof(Vertex), nullptr);
    api.glEnableVertexAttribArray(1);
    api.glVertexAttribPointer(1, 4, glc::UNSIGNED_BYTE, 1, sizeof(Vertex), reinterpret_cast<const void*>(2 * sizeof(float)));
    api.glViewport(0, 0, width_, height_);
    api.glDisable(glc::SCISSOR_TEST);
    api.glDisable(glc::BLEND);
    api.glDisable(glc::DEPTH_TEST);
    api.glColorMask(1, 1, 1, 1);
    api.glClearColor(static_cast<float>(clear_ & 0xFF) / 255.0f, static_cast<float>((clear_ >> 8) & 0xFF) / 255.0f,
                     static_cast<float>((clear_ >> 16) & 0xFF) / 255.0f, static_cast<float>(clear_ >> 24) / 255.0f);
    api.glClear(glc::COLOR_BUFFER_BIT);
    program_.use();
    if (per_rect) {
        for (std::size_t i = 0; i < rects_.size(); ++i) {
            api.glDrawArrays(glc::TRIANGLES, static_cast<int>(i * 6), 6);
        }
    } else {
        api.glDrawArrays(glc::TRIANGLES, 0, static_cast<int>(data.size()));
    }
    api.glBindVertexArray(0);
    api.glUseProgram(0);
    const unsigned failure = api.drain_errors();
    if (failure != 0) {
        error = "drawing the scene gave " + GlApi::error_name(failure);
        return false;
    }
    return true;
}

void GlScene::release() {
    if (api_ == nullptr) {
        return;
    }
    if (vbo_ != 0) {
        api_->glDeleteBuffers(1, &vbo_);
    }
    if (vao_ != 0) {
        api_->glDeleteVertexArrays(1, &vao_);
    }
    program_.destroy();
    vbo_ = vao_ = 0;
    api_ = nullptr;
}

std::vector<std::uint8_t> GlScene::expected() const {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4);
    auto put = [&image, this](int x, int y, std::uint32_t rgba) {
        std::uint8_t* pixel = &image[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)) * 4];
        pixel[0] = static_cast<std::uint8_t>(rgba & 0xFF);
        pixel[1] = static_cast<std::uint8_t>((rgba >> 8) & 0xFF);
        pixel[2] = static_cast<std::uint8_t>((rgba >> 16) & 0xFF);
        pixel[3] = static_cast<std::uint8_t>(rgba >> 24);
    };
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            put(x, y, clear_);
        }
    }
    for (const GlRect& rect : rects_) {
        for (int y = rect.y0; y < rect.y1; ++y) {
            for (int x = rect.x0; x < rect.x1; ++x) {
                put(x, y, rect.rgba);
            }
        }
    }
    return image;
}

}  // namespace lsw::suites
