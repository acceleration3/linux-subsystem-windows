#pragma once

#include "support/av_adapters.hpp"

#include <functional>
#include <string>

namespace lsw::suites {
class GlIdentity {
public:
    using Resolver = std::function<void*(const char*)>;
    using ErrorDrain = std::function<unsigned()>;

    [[nodiscard]] static std::string device_uuid(const Resolver& resolve, const ErrorDrain& drain_errors);

    [[nodiscard]] static std::string normalized(const std::string& name);

    [[nodiscard]] static bool names_match(const std::string& renderer, const std::string& adapter_name);

    [[nodiscard]] static bool matches(const AvAdapter& adapter, const std::string& gl_uuid, const std::string& renderer, std::string& how);
};
}  // namespace lsw::suites
