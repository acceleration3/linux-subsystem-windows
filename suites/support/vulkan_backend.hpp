#pragma once

#include "support/gpu_backend.hpp"
#include "support/vulkan_device.hpp"

#include <memory>
#include <vector>

namespace lsw::suites {
class VulkanBackend : public GpuBackend {
public:
    ~VulkanBackend() override;

    [[nodiscard]] std::string name() const override { return "vulkan"; }

    [[nodiscard]] bool init(const LUID& luid, std::string& error) override;

    [[nodiscard]] std::unique_ptr<GpuBuffer> create(std::uint64_t size, BufferKind kind, AllocStatus& status,
                                                    std::string& detail) override;

    [[nodiscard]] bool upload(GpuBuffer& buffer, std::uint64_t offset, const void* data, std::uint64_t size,
                              std::string& error) override;

    [[nodiscard]] bool download(GpuBuffer& buffer, std::uint64_t offset, void* out, std::uint64_t size,
                                std::string& error) override;

    [[nodiscard]] bool copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                            std::uint64_t destination_offset, std::uint64_t size, std::string& error) override;

    [[nodiscard]] bool finish(std::string& error) override;
    [[nodiscard]] bool release_staging(std::string& error);

    [[nodiscard]] std::uint64_t max_allocation() override { return state_->max_allocation_size; }

    [[nodiscard]] std::uint64_t available_memory() override;

    [[nodiscard]] std::uint64_t oversize_request() override;

    [[nodiscard]] bool supports_map() const override { return true; }

    [[nodiscard]] bool map(GpuBuffer& buffer, void** pointer, std::string& error) override;

    [[nodiscard]] bool unmap(GpuBuffer& buffer, std::string& error) override;

    [[nodiscard]] std::uint64_t map_alignment() override { return state_->min_map_alignment; }

    [[nodiscard]] std::unique_ptr<GpuBackend> new_context() override;

    void drain();

    [[nodiscard]] bool texture_import_supported(std::uint32_t handle_type, std::string& detail);

    [[nodiscard]] bool read_imported_texture(HANDLE handle, std::uint32_t handle_type, std::uint32_t width,
                                             std::uint32_t height, std::vector<std::uint8_t>& tight,
                                             std::string& error);

    [[nodiscard]] VulkanDeviceState& shared() { return *state_; }

    [[nodiscard]] std::uint64_t heap_usage(bool device_local);

private:
    [[nodiscard]] bool setup_commands(std::string& error);

    [[nodiscard]] bool begin(std::string& error);

    [[nodiscard]] bool submit(std::string& error);

    [[nodiscard]] bool stage(const void* data, std::uint64_t size, std::uint64_t& offset, std::string& error);

    [[nodiscard]] bool ensure_readback(std::uint64_t size, std::string& error);

    std::shared_ptr<VulkanDeviceState> state_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commands_ = nullptr;
    bool open_ = false;
    VkBuffer ring_ = VK_NULL_HANDLE;
    VkDeviceMemory ring_memory_ = VK_NULL_HANDLE;
    std::uint8_t* ring_map_ = nullptr;
    std::uint64_t ring_size_ = 0;
    std::uint64_t ring_head_ = 0;
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    std::uint8_t* readback_map_ = nullptr;
    std::uint64_t readback_size_ = 0;
};
}  // namespace lsw::suites
