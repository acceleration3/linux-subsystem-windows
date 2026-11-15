#include "render/render.hpp"

#include "render/d3d10/d3d10.hpp"
#include "render/d3d11/d3d11.hpp"
#include "render/d3d12/d3d12.hpp"
#include "render/d3d9/d3d9.hpp"
#include "render/gl/gl.hpp"
#include "render/vk/vk.hpp"

namespace lsw::suites::render {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("render", Policy::Parallel);
    d3d9::add(suite);
    d3d10::add(suite);
    d3d11::add(suite);
    d3d12::add(suite);
    vk::add(suite);
    gl::add(suite);
}

}  // namespace lsw::suites::render
