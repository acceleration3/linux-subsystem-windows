#pragma once

#include "core/suite.hpp"
#include "render/d3d11/kit.hpp"

namespace lsw::suites::render::d3d11 {
using D3d11Scene = rr::Scene<Kit>;
using D3d11SingleScene = rr::SingleScene<Kit>;

template <class SceneType>
using D3d11Register = rr::Register<Kit, SceneType>;

void add(Suite& suite);
}  // namespace lsw::suites::render::d3d11
