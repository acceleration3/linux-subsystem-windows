#pragma once

#include "core/context.hpp"
#include "core/outcome.hpp"
#include "core/text.hpp"
#include "support/gl_rig.hpp"
#include "support/gl_tools.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/wgl_enums.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::suites::sanity::gl {
class Kit {
public:
    [[nodiscard]] static bool open(GlRig& rig, Context& context, std::string& error, int width = 128, int height = 128,
                                   const GlContextRequest& request = {}) {
        GlRigOptions options;
        options.width = width;
        options.height = height;
        options.request = request;
        if (!rig.open(options, error)) {
            return false;
        }
        rig.record(context);
        return true;
    }

    [[nodiscard]] static std::vector<std::uint8_t> bytes(std::uint64_t seed, std::size_t size) {
        std::vector<std::uint8_t> data(size);
        Pattern::fill(seed, 0, data.data(), size);
        return data;
    }

    [[nodiscard]] static std::string hex(unsigned value) { return GlTools::hex(value); }

    [[nodiscard]] static std::string number(long long value) { return std::to_string(value); }

    [[nodiscard]] static bool core_profile_available(GlRig& rig) {
        return rig.wgl.has(rig.dc(), "WGL_ARB_create_context_profile") && rig.wgl.create_context_attribs != nullptr;
    }

    [[nodiscard]] static Outcome not_advertised(const std::string& what) {
        return Context::skip(what + " is not advertised by this context");
    }

    [[nodiscard]] static std::string formats_text(unsigned flags) { return GlTools::buffer_flags(flags); }

};
}  // namespace lsw::suites::sanity::gl
