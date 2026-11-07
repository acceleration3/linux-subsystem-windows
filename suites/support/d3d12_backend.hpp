#pragma once

#include "support/d3d12_device.hpp"
#include "support/gpu_backend.hpp"

#include <memory>

namespace lsw::suites {
class D3d12Backend : public GpuBackend {
public:
    ~D3d12Backend() override;

    [[nodiscard]] std::string name() const override { return "d3d12"; }

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

    [[nodiscard]] std::uint64_t available_memory() override;

    [[nodiscard]] std::uint64_t oversize_request() override;

    [[nodiscard]] bool supports_copy_to(BufferKind kind) const override { return kind == BufferKind::Device || kind == BufferKind::Readback; }

    [[nodiscard]] bool supports_map() const override { return true; }

    [[nodiscard]] bool map(GpuBuffer& buffer, void** pointer, std::string& error) override;

    [[nodiscard]] bool unmap(GpuBuffer& buffer, std::string& error) override;

    [[nodiscard]] std::uint64_t map_alignment() override { return 0; }

    [[nodiscard]] std::unique_ptr<GpuBackend> new_context() override;

    [[nodiscard]] bool evict(GpuBuffer& buffer, std::string& error);

    [[nodiscard]] bool make_resident(GpuBuffer& buffer, std::string& error);

    [[nodiscard]] std::unique_ptr<GpuBuffer> open_shared(HANDLE handle, std::uint64_t size, std::string& error);

    [[nodiscard]] D3d12Device& shared() { return *device_; }

private:
    [[nodiscard]] bool begin(std::string& error);

    [[nodiscard]] bool submit(std::string& error);

    [[nodiscard]] bool stage(const void* data, std::uint64_t size, std::uint64_t& offset, std::string& error);

    [[nodiscard]] bool ensure_readback(std::uint64_t size, std::string& error);

    void release_queue();

    std::shared_ptr<D3d12Device> device_;
    LUID luid_{};
    ID3D12CommandQueue* queue_ = nullptr;
    ID3D12CommandAllocator* allocator_ = nullptr;
    ID3D12GraphicsCommandList* list_ = nullptr;
    ID3D12Fence* fence_ = nullptr;
    HANDLE event_ = nullptr;
    std::uint64_t fence_value_ = 0;
    bool open_ = false;
    ID3D12Resource* ring_ = nullptr;
    std::uint8_t* ring_map_ = nullptr;
    std::uint64_t ring_size_ = 0;
    std::uint64_t ring_head_ = 0;
    ID3D12Resource* readback_ = nullptr;
    std::uint64_t readback_size_ = 0;
};
}  // namespace lsw::suites
