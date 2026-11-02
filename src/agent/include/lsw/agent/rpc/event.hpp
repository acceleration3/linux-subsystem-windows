#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace lsw::agent::rpc {
struct Event {
    std::string topic;
    nlohmann::json payload;

    Event() = default;
    Event(std::string t, nlohmann::json p)
        : topic(std::move(t)), payload(std::move(p)) {}
};

}
