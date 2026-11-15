#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace lsw::suites::sanity::sync {
enum class SyncState { Done, Timeout, Lost, Error };

class SyncDevice {
public:
    static constexpr std::uint32_t kSlots = 64;

    virtual ~SyncDevice() = default;

    [[nodiscard]] virtual bool open(std::string& error) = 0;

    [[nodiscard]] virtual bool set(std::uint32_t slot, std::uint32_t value, std::string& error) = 0;

    [[nodiscard]] virtual bool add(std::uint32_t slot, std::uint32_t value, std::string& error) = 0;

    [[nodiscard]] virtual bool signal(std::string& error) = 0;

    [[nodiscard]] virtual SyncState wait(int timeout_ms, std::string& detail) = 0;

    [[nodiscard]] virtual bool finish(std::string& error) = 0;

    [[nodiscard]] virtual bool read(std::uint32_t slot, std::uint32_t& out, std::string& error) = 0;

    [[nodiscard]] static std::unique_ptr<SyncDevice> make(const std::string& api);
};

std::unique_ptr<SyncDevice> make_sync_d3d11();
std::unique_ptr<SyncDevice> make_sync_d3d12();
std::unique_ptr<SyncDevice> make_sync_vulkan();
std::unique_ptr<SyncDevice> make_sync_opencl();
}  // namespace lsw::suites::sanity::sync
