#pragma once

#include "render/common/case.hpp"
#include "render/vk/kit.hpp"

namespace lsw::suites {
class Suite;
}

namespace lsw::render::vk {
using VkScene = Scene<Kit>;
using VkSingleScene = SingleScene<Kit>;

template <class SceneType>
using VkRegister = Register<Kit, SceneType>;

class VkCases {
public:
    [[nodiscard]] static VkPipelineLayout push_layout(Kit& kit, std::uint32_t bytes, VkShaderStageFlags stages,
                                                      const std::vector<VkDescriptorSetLayout>& sets, std::string& error);
};
}  // namespace lsw::render::vk

namespace lsw::suites::render::vk {
void add(Suite& suite);
}
