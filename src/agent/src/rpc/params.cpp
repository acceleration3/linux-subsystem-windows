#include "lsw/agent/rpc/params.hpp"

#include <stdexcept>

namespace lsw::agent::rpc {

std::uintptr_t Params::require_hwnd() const {
    const std::uintptr_t value = hwnd();
    if (value == 0) {
        fail_missing("hwnd");
    }
    return value;
}

std::uint32_t Params::require_pid() const {
    const auto value = get("pid", std::uint64_t{0});
    if (value == 0) {
        fail_missing("pid");
    }
    return static_cast<std::uint32_t>(value);
}

std::string Params::require_string(const char* key) const {
    std::string value = get(key, std::string{});
    if (value.empty()) {
        fail_missing(key);
    }
    return value;
}

void Params::fail_missing(std::string_view what) const {
    throw std::invalid_argument(std::string{cmd_} + ": missing " + std::string{what});
}

}  // namespace lsw::agent::rpc
