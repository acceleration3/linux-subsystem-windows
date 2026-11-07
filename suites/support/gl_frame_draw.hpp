#pragma once

#include "support/gl_api.hpp"

namespace lsw::suites {
class GlFrameDraw {
public:
    static void frame(const GlApi& api, int width, int height, int index);
};
}  // namespace lsw::suites
