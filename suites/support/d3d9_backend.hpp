#pragma once

#include "support/gpu_backend.hpp"
#include "support/test_window.hpp"

#include <d3d9.h>

namespace lsw::suites {
class D3d9Backend : public GpuBackend {
public:
    ~D3d9Backend() override;

    [[nodiscard]] std::string name() const override { return "d3d9"; }

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

    [[nodiscard]] IDirect3DDevice9Ex* device() { return device_; }

private:
    TestWindow window_;
    IDirect3D9Ex* d3d_ = nullptr;
    IDirect3DDevice9Ex* device_ = nullptr;
    LUID luid_{};
};
}  // namespace lsw::suites
