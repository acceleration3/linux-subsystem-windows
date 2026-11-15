#pragma once

#include "core/suite.hpp"
#include "render/d3d12/kit.hpp"

namespace lsw::suites::render::d3d12 {
using D3d12Scene = rr::Scene<Kit>;
using D3d12SingleScene = rr::SingleScene<Kit>;

template <class SceneType>
using D3d12Register = rr::Register<Kit, SceneType>;

void add(Suite& suite);
}  // namespace lsw::suites::render::d3d12
