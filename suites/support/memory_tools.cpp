#include "support/memory_tools.hpp"

#include "support/pattern.hpp"

#include <algorithm>

namespace lsw::suites {

bool MemoryTools::touch(GpuBackend& backend, GpuBuffer& buffer, std::uint64_t seed, std::string& error) {
    const std::uint64_t length = std::min<std::uint64_t>(buffer.size, 4096);
    std::vector<std::uint8_t> head(static_cast<std::size_t>(length));
    Pattern::fill(seed, 0, head.data(), head.size());
    if (!backend.upload(buffer, 0, head.data(), length, error)) {
        return false;
    }
    if (buffer.size > length) {
        Pattern::fill(seed, buffer.size - length, head.data(), head.size());
        return backend.upload(buffer, buffer.size - length, head.data(), length, error);
    }
    return true;
}

bool MemoryTools::verify_ends(GpuBackend& backend, GpuBuffer& buffer, std::uint64_t seed, Context& context,
                              std::string& error) {
    const std::uint64_t length = std::min<std::uint64_t>(buffer.size, 4096);
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(length));
    std::vector<std::uint8_t> actual(expected.size());
    Pattern::fill(seed, 0, expected.data(), expected.size());
    if (!backend.download(buffer, 0, actual.data(), length, error) ||
        !BackendHarness::verify(context, backend.name() + " head", expected.data(), actual.data(), length, error)) {
        return false;
    }
    if (buffer.size > length) {
        Pattern::fill(seed, buffer.size - length, expected.data(), expected.size());
        return backend.download(buffer, buffer.size - length, actual.data(), length, error) &&
               BackendHarness::verify(context, backend.name() + " tail", expected.data(), actual.data(), length, error);
    }
    return true;
}

}  // namespace lsw::suites
