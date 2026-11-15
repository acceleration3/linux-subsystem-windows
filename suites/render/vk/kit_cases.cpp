#include "render/vk/vk.hpp"

namespace lsw::render::vk {

VkPipelineLayout VkCases::push_layout(Kit& kit, std::uint32_t bytes, VkShaderStageFlags stages,
                                      const std::vector<VkDescriptorSetLayout>& sets, std::string& error) {
    std::vector<VkPushConstantRange> push;
    if (bytes != 0) {
        push.push_back({stages, 0, bytes});
    }
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (!kit.make_pipeline_layout(layout, sets, push, error)) {
        return VK_NULL_HANDLE;
    }
    return layout;
}

}  // namespace lsw::render::vk
