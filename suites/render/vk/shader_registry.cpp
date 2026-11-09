#include "render/vk/shader_registry.hpp"

#include <map>

namespace lsw::render::vk {

namespace {

std::map<std::string, std::pair<const std::uint32_t*, std::size_t>>& table() {
    static std::map<std::string, std::pair<const std::uint32_t*, std::size_t>> value;
    return value;
}

}  // namespace

void ShaderRegistry::add(const char* name, const std::uint32_t* words, std::size_t count) {
    table()[name] = {words, count};
}

bool ShaderRegistry::find(const std::string& name, std::vector<std::uint32_t>& out) {
    const auto it = table().find(name);
    if (it == table().end()) {
        return false;
    }
    out.assign(it->second.first, it->second.first + it->second.second);
    return true;
}

}  // namespace lsw::render::vk
