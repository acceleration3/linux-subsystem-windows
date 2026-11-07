#pragma once

#include "support/d3d11_device.hpp"
#include "support/gpu_backend.hpp"

#include <memory>

namespace lsw::suites {
class D3d11Backend : public GpuBackend {
public:
    [[nodiscard]] std::string name() const override { return "d3d11"; }

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

    [[nodiscard]] std::uint64_t max_allocation() override;

    [[nodiscard]] std::uint64_t available_memory() override;

    [[nodiscard]] bool supports_map() const override { return true; }

    [[nodiscard]] bool map(GpuBuffer& buffer, void** pointer, std::string& error) override;

    [[nodiscard]] bool unmap(GpuBuffer& buffer, std::string& error) override;

    [[nodiscard]] std::unique_ptr<GpuBackend> new_context() override;

    [[nodiscard]] std::unique_ptr<GpuBuffer> create_shared(std::uint64_t size, HANDLE& handle, std::string& error);

    [[nodiscard]] D3d11Device& shared() { return *device_; }

private:
    std::shared_ptr<D3d11Device> device_;
    LUID luid_{};
};
}  // namespace lsw::suites
