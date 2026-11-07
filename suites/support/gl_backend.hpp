#pragma once

#include "support/gpu_backend.hpp"
#include "support/opengl_context.hpp"
#include "support/test_window.hpp"

namespace lsw::suites {
class GlBackend : public GpuBackend {
public:
    [[nodiscard]] std::string name() const override { return "gl"; }

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

    [[nodiscard]] bool supports_map() const override { return persistent_; }

    [[nodiscard]] bool map(GpuBuffer& buffer, void** pointer, std::string& error) override;

    [[nodiscard]] bool unmap(GpuBuffer& buffer, std::string& error) override;

    [[nodiscard]] std::uint64_t map_alignment() override { return 0; }

private:
    TestWindow window_;
    OpenGlContext gl_;
    LUID luid_{};
    bool persistent_ = false;
    void(__stdcall* gen_buffers_)(int, unsigned*) = nullptr;
    void(__stdcall* delete_buffers_)(int, const unsigned*) = nullptr;
    void(__stdcall* bind_buffer_)(unsigned, unsigned) = nullptr;
    void(__stdcall* buffer_data_)(unsigned, std::ptrdiff_t, const void*, unsigned) = nullptr;
    void(__stdcall* buffer_sub_data_)(unsigned, std::ptrdiff_t, std::ptrdiff_t, const void*) = nullptr;
    void(__stdcall* get_buffer_sub_data_)(unsigned, std::ptrdiff_t, std::ptrdiff_t, void*) = nullptr;
    void(__stdcall* copy_buffer_sub_data_)(unsigned, unsigned, std::ptrdiff_t, std::ptrdiff_t, std::ptrdiff_t) = nullptr;
    void(__stdcall* buffer_storage_)(unsigned, std::ptrdiff_t, const void*, unsigned) = nullptr;
    void*(__stdcall* map_buffer_range_)(unsigned, std::ptrdiff_t, std::ptrdiff_t, unsigned) = nullptr;
    unsigned char(__stdcall* unmap_buffer_)(unsigned) = nullptr;
    void(__stdcall* get_buffer_parameter_)(unsigned, unsigned, int*) = nullptr;
    unsigned(__stdcall* get_error_)() = nullptr;
    void(__stdcall* finish_)() = nullptr;

    friend class BufferGl;
};
}  // namespace lsw::suites
