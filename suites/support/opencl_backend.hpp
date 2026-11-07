#pragma once

#include "support/dyn_library.hpp"
#include "support/gpu_backend.hpp"

#include <memory>
#include <vector>

namespace lsw::suites {
struct OpenClRuntime;

class OpenClBackend : public GpuBackend {
public:
    OpenClBackend();
    ~OpenClBackend() override;

    [[nodiscard]] std::string name() const override { return "opencl"; }

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

    [[nodiscard]] std::uint64_t oversize_request() override;

    [[nodiscard]] bool supports_map() const override { return true; }

    [[nodiscard]] bool map(GpuBuffer& buffer, void** pointer, std::string& error) override;

    [[nodiscard]] bool unmap(GpuBuffer& buffer, std::string& error) override;

    [[nodiscard]] std::uint64_t map_alignment() override;

    [[nodiscard]] std::unique_ptr<GpuBackend> new_context() override;

    [[nodiscard]] std::uint64_t free_memory_kb(bool& exposed);

    [[nodiscard]] bool d3d11_sharing_advertised();

    [[nodiscard]] bool read_d3d11_texture(void* d3d11_device, void* d3d11_texture, std::uint32_t width,
                                          std::uint32_t height, std::vector<std::uint8_t>& tight, std::string& error);

    [[nodiscard]] bool churn_program(std::string& error);

    [[nodiscard]] bool run_kernel_load(double seconds, std::string& error);

private:
    std::shared_ptr<OpenClRuntime> runtime_;
    void* queue_ = nullptr;
};
}  // namespace lsw::suites
