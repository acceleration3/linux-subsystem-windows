#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lsw::render::vk {
class ShaderRegistry {
public:
    static void add(const char* name, const std::uint32_t* words, std::size_t count);
    [[nodiscard]] static bool find(const std::string& name, std::vector<std::uint32_t>& out);
};

class ShaderRegistrar {
public:
    ShaderRegistrar(const char* name, const std::uint32_t* words, std::size_t count) { ShaderRegistry::add(name, words, count); }
};
}  // namespace lsw::render::vk
