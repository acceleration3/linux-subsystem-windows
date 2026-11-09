#pragma once

#include "core/suite.hpp"
#include "render/d3d9/kit.hpp"

namespace lsw::suites::render::d3d9 {
using D3d9Scene = rr::Scene<Kit>;
using D3d9SingleScene = rr::SingleScene<Kit>;

template <class SceneType>
using D3d9Register = rr::Register<Kit, SceneType>;

void add(Suite& suite);
void add_caps(Suite& suite);
}  // namespace lsw::suites::render::d3d9
