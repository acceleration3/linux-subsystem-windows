#pragma once

#include <cstdint>
#include <vector>

namespace lsw::suites {
enum class VulkanShader {
    Ops,
    Subgroup,
    Limits,
    Bindings,
    MemoryModel,
    RectVert,
    PointVert,
    RectFrag,
    Rect2Frag,
    RectGeom,
    TessControl,
    TessEval,
    Texel,
    Sample,
    StoreUint,
    StoreRgba,
    VertexInput,
    SpecKernel,
    SpecFrag,
    DetKernel,
    Spin,
    SpinForever
};

class VulkanShaders {
public:
    [[nodiscard]] static std::vector<std::uint32_t> words(VulkanShader shader);
};
}  // namespace lsw::suites
