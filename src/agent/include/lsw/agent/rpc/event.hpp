#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace lsw::agent::rpc {

// A push event the transport will serialise as a type=1 JSON envelope
// with the ``event`` field set to ``topic`` and the body merged in.
// Sources (Installer, Console, Log, Uia) emit these into the
// per-connection queue; transport drains on its 33 ms tick.
struct Event {
    std::string topic;
    nlohmann::json payload;

    Event() = default;
    Event(std::string t, nlohmann::json p)
        : topic(std::move(t)), payload(std::move(p)) {}
};

}  // namespace lsw::agent::rpc
