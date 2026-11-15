#pragma once

#include "core/context.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace lsw::suites {
class ArtifactWriter {
public:
    explicit ArtifactWriter(Context& context) : context_(context) {}

    bool bytes(const std::string& name, const void* data, std::size_t size);

    bool image(const std::string& name, const void* bgra, std::uint32_t width, std::uint32_t height,
               std::uint32_t stride);

private:
    Context& context_;
};
}  // namespace lsw::suites
