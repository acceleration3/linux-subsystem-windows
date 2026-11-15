#pragma once

#include "support/av_adapters.hpp"

#include <string>
#include <vector>

namespace lsw::suites {
class TargetAdapter {
public:
    [[nodiscard]] static bool pick(AvAdapter& out, std::string& error);

    [[nodiscard]] static bool resolve(const std::string& spec, AvAdapter& out, std::size_t& index, std::string& error);

    [[nodiscard]] static std::string spec();

    [[nodiscard]] static bool find_uuid(const std::vector<std::string>& uuids, const std::string& wanted, std::size_t& index);

    [[nodiscard]] static std::string stable_spec(const std::string& uuid, const std::string& luid_spec);

    [[nodiscard]] static std::string vulkan_uuid(const AvAdapter& adapter);

    [[nodiscard]] static std::string gl_unavailable(const AvAdapter& adapter);

    static void export_identity(const AvAdapter& adapter, std::size_t index);

    static int list();
};
}  // namespace lsw::suites
