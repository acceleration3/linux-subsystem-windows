#pragma once

#include "core/json.hpp"

namespace lsw::suites {
class StatusTransport {
public:
    virtual ~StatusTransport() = default;

    [[nodiscard]] virtual int post(const Json& batch) = 0;
};
}  // namespace lsw::suites
