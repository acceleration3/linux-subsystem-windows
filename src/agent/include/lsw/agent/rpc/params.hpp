#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace lsw::agent::rpc {

// A request's ``params`` object, read on behalf of one command.
//
// Most handlers start by pulling a window handle, a PID or a string out
// of the request and refusing it when absent, and each spelled the
// refusal by hand. The wording is part of the contract — the host shows
// "<cmd>: missing <what>" as it stands — so it is built in one place, from
// the command's own name.
//
// Every ``require_*`` throws std::invalid_argument when the value is
// absent, zero or empty; the transport turns that into an error reply.
class Params {
public:
    Params(const nlohmann::json& params, std::string_view cmd) : json_(params), cmd_(cmd) {}

    // ``hwnd`` as a window handle, or 0 when absent.
    [[nodiscard]] std::uintptr_t hwnd() const {
        return static_cast<std::uintptr_t>(get("hwnd", std::uint64_t{0}));
    }

    [[nodiscard]] std::uintptr_t require_hwnd() const;
    [[nodiscard]] std::uint32_t require_pid() const;
    [[nodiscard]] std::string require_string(const char* key) const;

    // ``key`` when present, ``fallback`` otherwise.
    template<class T>
    [[nodiscard]] T get(const char* key, T fallback) const {
        return json_.value(key, std::move(fallback));
    }

    // Throw the standard refusal for ``what`` — for a handler whose
    // requirement is a combination ("hwnd or selector") rather than one
    // field.
    [[noreturn]] void fail_missing(std::string_view what) const;

private:
    const nlohmann::json& json_;
    std::string_view cmd_;
};

}  // namespace lsw::agent::rpc
