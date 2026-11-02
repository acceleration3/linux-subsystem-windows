#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace lsw::agent::rpc {
class Params {
public:
    Params(const nlohmann::json& params, std::string_view cmd) : json_(params), cmd_(cmd) {}

    [[nodiscard]] std::uintptr_t hwnd() const {
        return static_cast<std::uintptr_t>(get("hwnd", std::uint64_t{0}));
    }

    [[nodiscard]] std::uintptr_t require_hwnd() const;
    [[nodiscard]] std::uint32_t require_pid() const;
    [[nodiscard]] std::string require_string(const char* key) const;

    template<class T>
    [[nodiscard]] T get(const char* key, T fallback) const {
        return json_.value(key, std::move(fallback));
    }

    [[noreturn]] void fail_missing(std::string_view what) const;

private:
    const nlohmann::json& json_;
    std::string_view cmd_;
};

}
