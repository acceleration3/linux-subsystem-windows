#include "support/backend_harness.hpp"

#include "support/artifact_writer.hpp"
#include "support/compare.hpp"
#include "support/pattern.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>

namespace lsw::suites {

std::unique_ptr<GpuBackend> BackendHarness::open(const std::string& api, AvAdapter& adapter, std::string& error) {
    if (!TargetAdapter::pick(adapter, error)) {
        return nullptr;
    }
    std::unique_ptr<GpuBackend> backend = GpuBackends::make(api);
    if (backend == nullptr) {
        error = "unknown api " + api;
        return nullptr;
    }
    std::string detail;
    if (!backend->init(adapter.luid, detail)) {
        error = api + ": " + detail;
        return nullptr;
    }
    return backend;
}

std::vector<std::uint64_t> BackendHarness::small_sizes() {
    return {1, 2, 3, 4, 7, 15, 16, 17, 255, 256, 4095, 4096, 4097, 65535};
}

std::vector<std::uint64_t> BackendHarness::boundary_sizes() {
    std::vector<std::uint64_t> sizes = small_sizes();
    for (double mib : {3.9, 4.0, 4.1, 15.9, 16.0, 16.1, 31.9, 32.0, 32.1, 63.9, 64.0, 64.1, 255.9, 256.0, 256.1}) {
        sizes.push_back(static_cast<std::uint64_t>(mib * 1048576.0));
    }
    return sizes;
}

bool BackendHarness::verify(Context& context, const std::string& label, const void* expected, const void* actual,
                            std::uint64_t size, std::string& error) {
    const std::optional<Mismatch> mismatch = Compare::bytes(expected, actual, static_cast<std::size_t>(size));
    if (!mismatch) {
        return true;
    }
    error = label + ": " + mismatch->describe() + " of " + std::to_string(size);
    ArtifactWriter writer(context);
    const std::size_t keep = static_cast<std::size_t>(std::min<std::uint64_t>(size, 1u << 20));
    const std::size_t start = static_cast<std::size_t>(mismatch->offset >= keep / 2 ? mismatch->offset - keep / 2 : 0);
    const std::size_t length = std::min<std::size_t>(keep, static_cast<std::size_t>(size) - start);
    std::string name = label;
    for (char& c : name) {
        c = (c == ' ' || c == '/' || c == ':') ? '_' : c;
    }
    writer.bytes(name + ".expected", static_cast<const char*>(expected) + start, length);
    writer.bytes(name + ".actual", static_cast<const char*>(actual) + start, length);
    return false;
}

bool BackendHarness::roundtrip(GpuBackend& backend, std::uint64_t size, std::uint64_t seed, AllocStatus& status,
                               Context& context, std::string& error) {
    std::string detail;
    std::unique_ptr<GpuBuffer> buffer = backend.create(size, BufferKind::Device, status, detail);
    if (buffer == nullptr) {
        error = detail;
        return status != AllocStatus::Failed;
    }
    return fill_and_verify(backend, *buffer, seed, size, context, error);
}

bool BackendHarness::fill_and_verify(GpuBackend& backend, GpuBuffer& buffer, std::uint64_t seed, std::uint64_t size,
                                     Context& context, std::string& error) {
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(size));
    Pattern::fill(seed, 0, expected.data(), expected.size());
    std::vector<std::uint8_t> actual(expected.size(), 0xCD);
    if (!backend.upload(buffer, 0, expected.data(), size, error) || !backend.download(buffer, 0, actual.data(), size, error)) {
        return false;
    }
    return verify(context, backend.name() + " size " + std::to_string(size), expected.data(), actual.data(), size, error);
}

}  // namespace lsw::suites
