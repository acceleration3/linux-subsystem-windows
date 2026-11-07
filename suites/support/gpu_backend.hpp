#pragma once

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites {
enum class AllocStatus { Ok, OutOfMemory, TooLarge, Failed };

enum class BufferKind { Device, Upload, Readback };

class GpuBuffer {
public:
    virtual ~GpuBuffer() = default;

    std::uint64_t size = 0;
    BufferKind kind = BufferKind::Device;
};

class GpuBackend {
public:
    virtual ~GpuBackend() = default;

    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual bool init(const LUID& luid, std::string& error) = 0;

    [[nodiscard]] virtual std::unique_ptr<GpuBuffer> create(std::uint64_t size, BufferKind kind,
                                                            AllocStatus& status, std::string& detail) = 0;

    [[nodiscard]] virtual bool upload(GpuBuffer& buffer, std::uint64_t offset, const void* data,
                                      std::uint64_t size, std::string& error) = 0;

    [[nodiscard]] virtual bool download(GpuBuffer& buffer, std::uint64_t offset, void* out,
                                        std::uint64_t size, std::string& error) = 0;

    [[nodiscard]] virtual bool copy(GpuBuffer& source, std::uint64_t source_offset, GpuBuffer& destination,
                                    std::uint64_t destination_offset, std::uint64_t size,
                                    std::string& error) = 0;

    [[nodiscard]] virtual bool finish(std::string& error) = 0;

    [[nodiscard]] virtual std::uint64_t max_allocation() { return 0; }

    [[nodiscard]] virtual std::uint64_t available_memory() = 0;

    [[nodiscard]] virtual std::uint64_t oversize_request() { return 0; }

    [[nodiscard]] virtual bool supports_copy_to(BufferKind) const { return true; }

    [[nodiscard]] virtual bool supports_map() const { return false; }

    [[nodiscard]] virtual bool map(GpuBuffer&, void**, std::string& error) {
        error = "mapping is not supported";
        return false;
    }

    [[nodiscard]] virtual bool unmap(GpuBuffer&, std::string& error) {
        error = "mapping is not supported";
        return false;
    }

    [[nodiscard]] virtual std::uint64_t map_alignment() { return 0; }

    [[nodiscard]] virtual std::unique_ptr<GpuBackend> new_context() { return nullptr; }
};

class GpuBackends {
public:
    [[nodiscard]] static std::vector<std::string> names();

    [[nodiscard]] static std::unique_ptr<GpuBackend> make(const std::string& name);

    [[nodiscard]] static std::string describe(AllocStatus status);
};
}  // namespace lsw::suites
