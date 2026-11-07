#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lsw::suites::sanity::err {
enum class SpinState { Done, Lost, Pending, Error };

class SpinDevice {
public:
    static constexpr std::uint32_t kInner = 1u << 20;
    static constexpr std::uint32_t kThreads = 64;

    virtual ~SpinDevice() = default;

    [[nodiscard]] virtual bool open(bool forever, std::string& error) = 0;

    [[nodiscard]] virtual bool submit(std::uint32_t outer, std::string& error) = 0;

    [[nodiscard]] virtual SpinState poll(int timeout_ms, std::string& detail) = 0;

    [[nodiscard]] virtual SpinState wait_native(int timeout_ms, std::string& detail) { return poll(timeout_ms, detail); }

    [[nodiscard]] virtual bool read(std::vector<std::uint32_t>& out, std::string& error) = 0;

    [[nodiscard]] static std::unique_ptr<SpinDevice> make(const std::string& api);

    [[nodiscard]] static bool calibrate(SpinDevice& device, double target_ms, std::uint32_t& outer, double& took_ms, std::string& error);

    [[nodiscard]] static std::uint32_t expected(std::uint32_t thread, std::uint64_t iterations);
};

std::unique_ptr<SpinDevice> make_spin_d3d11();
std::unique_ptr<SpinDevice> make_spin_d3d12();
std::unique_ptr<SpinDevice> make_spin_vulkan();
std::unique_ptr<SpinDevice> make_spin_opencl();
}  // namespace lsw::suites::sanity::err
