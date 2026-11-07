#include "support/gl_tools.hpp"

#include "core/text.hpp"
#include "support/artifact_writer.hpp"

#include <algorithm>
#include <cstdlib>

namespace lsw::suites {

std::string GlTools::to_hex(unsigned value) {
    return Text::hex(value, 4);
}

bool GlTools::same_image(Context& context, const std::string& name, const std::vector<std::uint8_t>& expected,
                         const std::vector<std::uint8_t>& actual, int width, int height, int tolerance, std::string& detail) {
    const std::size_t size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (expected.size() < size || actual.size() < size) {
        detail = name + ": image buffers are smaller than " + std::to_string(width) + "x" + std::to_string(height);
        return false;
    }
    std::size_t bad = 0;
    std::size_t first = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (std::abs(static_cast<int>(expected[i]) - static_cast<int>(actual[i])) > tolerance) {
            if (bad == 0) {
                first = i;
            }
            ++bad;
        }
    }
    if (bad == 0) {
        return true;
    }
    const std::size_t pixel = first / 4;
    const int x = static_cast<int>(pixel % static_cast<std::size_t>(width));
    const int y = static_cast<int>(pixel / static_cast<std::size_t>(width));
    auto texel = [&](const std::vector<std::uint8_t>& image) {
        return Text::hex(static_cast<unsigned>(image[pixel * 4]) | (static_cast<unsigned>(image[pixel * 4 + 1]) << 8) |
                             (static_cast<unsigned>(image[pixel * 4 + 2]) << 16) | (static_cast<unsigned>(image[pixel * 4 + 3]) << 24),
                         8);
    };
    detail = name + ": " + std::to_string(bad) + " bytes differ by more than " + std::to_string(tolerance) + ", first at pixel (" +
             std::to_string(x) + "," + std::to_string(y) + ") expected 0x" + texel(expected) + " (RGBA bytes), actual 0x" + texel(actual);
    ArtifactWriter writer(context);
    auto to_bgra = [&](const std::vector<std::uint8_t>& image) {
        std::vector<std::uint8_t> out(size);
        for (std::size_t i = 0; i < size; i += 4) {
            out[i] = image[i + 2];
            out[i + 1] = image[i + 1];
            out[i + 2] = image[i];
            out[i + 3] = image[i + 3];
        }
        return out;
    };
    const std::vector<std::uint8_t> a = to_bgra(expected);
    const std::vector<std::uint8_t> b = to_bgra(actual);
    const std::uint32_t stride = static_cast<std::uint32_t>(width) * 4;
    (void)writer.image(name + ".expected", a.data(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), stride);
    (void)writer.image(name + ".actual", b.data(), static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), stride);
    return false;
}

bool GlTools::wait_fence(const GlApi& api, unsigned long long timeout_ns, unsigned& result) {
    void* fence = api.glFenceSync(glc::SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (fence == nullptr) {
        result = glc::SYNC_WAIT_FAILED;
        return false;
    }
    result = api.glClientWaitSync(fence, glc::SYNC_FLUSH_COMMANDS_BIT, timeout_ns);
    api.glDeleteSync(fence);
    return result == glc::ALREADY_SIGNALED || result == glc::CONDITION_SATISFIED;
}

std::string GlTools::wait_name(unsigned result) {
    switch (result) {
    case glc::ALREADY_SIGNALED: return "GL_ALREADY_SIGNALED";
    case glc::TIMEOUT_EXPIRED: return "GL_TIMEOUT_EXPIRED";
    case glc::CONDITION_SATISFIED: return "GL_CONDITION_SATISFIED";
    case glc::SYNC_WAIT_FAILED: return "GL_SYNC_WAIT_FAILED";
    default: return "wait result " + hex(result);
    }
}

void GlTools::expect_error(Problems& problems, const GlApi& api, const std::string& what, unsigned wanted) {
    const unsigned first = api.glGetError();
    unsigned extra = 0;
    for (int i = 0; i < 16; ++i) {
        const unsigned more = api.glGetError();
        if (more == 0) {
            break;
        }
        if (extra == 0) {
            extra = more;
        }
    }
    if (first != wanted) {
        problems.add(what + " gave " + GlApi::error_name(first) + ", expected " + GlApi::error_name(wanted));
    } else if (extra != 0) {
        problems.add(what + " left " + GlApi::error_name(extra) + " set after " + GlApi::error_name(first) + " was read");
    }
}

void GlTools::expect_clean(Problems& problems, const GlApi& api, const std::string& what) {
    const unsigned first = api.drain_errors();
    if (first != 0) {
        problems.add(what + " gave " + GlApi::error_name(first));
    }
}

std::string GlTools::buffer_flags(unsigned flags) {
    struct Name {
        unsigned bit;
        const char* text;
    };
    static const Name names[] = {{glc::MAP_READ_BIT, "MAP_READ"},
                                 {glc::MAP_WRITE_BIT, "MAP_WRITE"},
                                 {glc::MAP_PERSISTENT_BIT, "PERSISTENT"},
                                 {glc::MAP_COHERENT_BIT, "COHERENT"},
                                 {glc::DYNAMIC_STORAGE_BIT, "DYNAMIC_STORAGE"},
                                 {glc::CLIENT_STORAGE_BIT, "CLIENT_STORAGE"},
                                 {glc::MAP_INVALIDATE_RANGE_BIT, "INVALIDATE_RANGE"},
                                 {glc::MAP_INVALIDATE_BUFFER_BIT, "INVALIDATE_BUFFER"},
                                 {glc::MAP_FLUSH_EXPLICIT_BIT, "FLUSH_EXPLICIT"},
                                 {glc::MAP_UNSYNCHRONIZED_BIT, "UNSYNCHRONIZED"}};
    std::string text;
    for (const Name& name : names) {
        if ((flags & name.bit) != 0) {
            text += text.empty() ? "" : "|";
            text += name.text;
        }
    }
    return (text.empty() ? std::string("0") : text) + " (" + hex(flags) + ")";
}

}  // namespace lsw::suites
