#pragma once

#include "core/suite.hpp"
#include "render/d3d10/kit.hpp"

namespace lsw::suites::render::d3d10 {
using D3d10Scene = rr::Scene<Kit>;
using D3d10SingleScene = rr::SingleScene<Kit>;

template <class SceneType>
using D3d10Register = rr::Register<Kit, SceneType>;

void add(Suite& suite);
}  // namespace lsw::suites::render::d3d10
