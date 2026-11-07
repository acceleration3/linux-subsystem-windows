#include "sanity/iso/fresh_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/iso/magic.hpp"
#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/d3d11_tools.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>

namespace lsw::suites::sanity::iso {

namespace {

constexpr std::uint64_t kChunk = 16ull << 20;
constexpr std::uint64_t kVramCap = 4ull << 30;
const std::vector<std::string> kApis = {"d3d11", "d3d12", "vulkan", "opencl"};

void fill_magic(std::uint8_t* data, std::size_t size, std::uint64_t first_word) {
    Magic::fill(data, size, first_word);
}

std::uint64_t count_magic(const std::uint8_t* data, std::size_t size) {
    return Magic::count(data, size);
}

std::vector<std::uint64_t> sizes_for(GpuBackend& backend) {
    std::uint64_t limit = backend.max_allocation() != 0 ? backend.max_allocation() : 256ull << 20;
    const std::uint64_t available = backend.available_memory();
    if (available != 0) {
        limit = std::min(limit, available / 3);
    }
    std::vector<std::uint64_t> out;
    for (std::uint64_t size : {4ull << 10, 64ull << 10, 1ull << 20, 16ull << 20, 64ull << 20, 256ull << 20}) {
        if (size <= limit) {
            out.push_back(size);
        }
    }
    return out;
}

bool write_buffer(GpuBackend& backend, GpuBuffer& buffer, std::string& error) {
    std::vector<std::uint8_t> block(static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size, kChunk)));
    for (std::uint64_t at = 0; at < buffer.size; at += block.size()) {
        const std::size_t length = static_cast<std::size_t>(std::min<std::uint64_t>(block.size(), buffer.size - at));
        fill_magic(block.data(), length, at / 8);
        if (!backend.upload(buffer, at, block.data(), length, error)) {
            return false;
        }
    }
    return backend.finish(error);
}

std::uint64_t scan_buffer(GpuBackend& backend, GpuBuffer& buffer, std::string& error) {
    std::vector<std::uint8_t> block(static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size, kChunk)));
    std::uint64_t found = 0;
    for (std::uint64_t at = 0; at < buffer.size; at += block.size()) {
        const std::size_t length = static_cast<std::size_t>(std::min<std::uint64_t>(block.size(), buffer.size - at));
        if (!backend.download(buffer, at, block.data(), length, error)) {
            return ~0ull;
        }
        found += count_magic(block.data(), length);
    }
    return found;
}

constexpr UINT kTexture = 4096;

Outcome texture_pass(bool write, std::string& detail) {
    AvAdapter adapter;
    D3d11Device device;
    if (!TargetAdapter::pick(adapter, detail) || !device.create(adapter.luid, detail)) {
        return Context::skip(detail);
    }
    Com<ID3D11Texture2D> texture = D3d11Tools::texture(device, D3d11Tools::desc2d(kTexture, kTexture, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE));
    if (!texture) {
        return Outcome::fail("creating the 64 MiB texture failed");
    }
    if (write) {
        std::vector<std::uint8_t> rows(static_cast<std::size_t>(kTexture) * 4 * 64);
        for (UINT y = 0; y < kTexture; y += 64) {
            fill_magic(rows.data(), rows.size(), static_cast<std::uint64_t>(y) * kTexture / 2);
            D3D11_BOX box{0, y, 0, kTexture, y + 64, 1};
            device.context->UpdateSubresource(texture.get(), 0, &box, rows.data(), kTexture * 4, 0);
        }
        return D3d11Tools::wait_event(device, device.context, detail) ? Outcome::ok() : Outcome::fail(detail);
    }
    Com<ID3D11Texture2D> staging = D3d11Tools::texture(device, D3d11Tools::desc2d(kTexture, kTexture, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
    TexelImage image;
    if (!staging) {
        return Outcome::fail("creating the staging texture failed");
    }
    device.context->CopyResource(staging.get(), texture.get());
    if (!D3d11Tools::read_texture(device, staging.get(), 0, image, detail)) {
        return Outcome::fail(detail);
    }
    const std::uint64_t found = count_magic(image.data.data(), image.data.size());
    return found > 1 ? Outcome::fail("a new texture of 4096x4096 holds " + std::to_string(found) + " words of another process's data") : Outcome::ok();
}

Outcome buffers_pass(Context& context, const std::string& api, bool write) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
    if (backend == nullptr) {
        return Context::skip(error);
    }
    Problems problems;
    std::vector<std::unique_ptr<GpuBuffer>> held;
    for (std::uint64_t size : sizes_for(*backend)) {
        AllocStatus status = AllocStatus::Ok;
        std::unique_ptr<GpuBuffer> buffer = backend->create(size, BufferKind::Device, status, error);
        if (buffer == nullptr) {
            problems.add(api + " size " + std::to_string(size) + ": " + error);
            continue;
        }
        if (write) {
            if (!write_buffer(*backend, *buffer, error)) {
                problems.add(api + " size " + std::to_string(size) + ": " + error);
            }
        } else {
            const std::uint64_t found = scan_buffer(*backend, *buffer, error);
            if (found == ~0ull) {
                problems.add(api + " size " + std::to_string(size) + ": " + error);
            } else if (found > 1) {
                problems.add(api + ": a new " + std::to_string(size) + " byte buffer holds " + std::to_string(found) + " words of another process's data");
            }
        }
        held.push_back(std::move(buffer));
    }
    if (api == "d3d11") {
        std::string detail;
        const Outcome texture = texture_pass(write, detail);
        if (texture.status() == Status::Fail) {
            problems.add(texture.reason());
        }
    }
    context.measure("sizes", static_cast<long long>(held.size()));
    return problems.outcome();
}

Outcome vram_pass(Context& context, const std::string& api, bool write) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
    if (backend == nullptr) {
        return Context::skip(error);
    }
    std::uint64_t chunk = 256ull << 20;
    if (backend->max_allocation() != 0) {
        chunk = std::min(chunk, backend->max_allocation());
    }
    std::vector<std::unique_ptr<GpuBuffer>> held;
    std::uint64_t total = 0;
    while (total < kVramCap) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        std::unique_ptr<GpuBuffer> next = backend->create(chunk, BufferKind::Device, status, detail);
        if (next == nullptr) {
            if (chunk <= (16ull << 20)) {
                break;
            }
            chunk /= 2;
            continue;
        }
        total += chunk;
        held.push_back(std::move(next));
    }
    context.measure("bytes", static_cast<long long>(total));
    Problems problems;
    std::uint64_t stale = 0;
    for (std::unique_ptr<GpuBuffer>& buffer : held) {
        if (write) {
            if (!write_buffer(*backend, *buffer, error)) {
                problems.add(api + ": " + error);
                break;
            }
        } else {
            const std::uint64_t found = scan_buffer(*backend, *buffer, error);
            if (found == ~0ull) {
                problems.add(api + ": " + error);
                break;
            }
            stale += found;
        }
    }
    if (stale > 1) {
        problems.add(api + ": " + std::to_string(stale) + " words of the previous process's pattern survived in " + std::to_string(total >> 20) + " MiB of new device memory");
    }
    return problems.outcome();
}

Outcome host_pass(Context& context, const std::string& api, bool write) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
    if (backend == nullptr) {
        return Context::skip(error);
    }
    if (!backend->supports_map()) {
        return Context::skip(api + " backend does not expose mapped host-visible buffers");
    }
    Problems problems;
    int mapped_kinds = 0;
    for (BufferKind kind : {BufferKind::Upload, BufferKind::Readback}) {
        for (std::uint64_t size : {4ull << 10, 256ull << 10, 4ull << 20, 64ull << 20}) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> buffer = backend->create(size, kind, status, detail);
            void* pointer = nullptr;
            if (buffer == nullptr || !backend->map(*buffer, &pointer, detail)) {
                continue;
            }
            ++mapped_kinds;
            if (write) {
                fill_magic(static_cast<std::uint8_t*>(pointer), static_cast<std::size_t>(size), 0);
            } else {
                const std::uint64_t found = count_magic(static_cast<const std::uint8_t*>(pointer), static_cast<std::size_t>(size));
                if (found > 1) {
                    problems.add(api + ": a new " + std::to_string(size) + " byte host-visible buffer holds " + std::to_string(found) + " words of another process's data");
                }
            }
            static_cast<void>(backend->unmap(*buffer, detail));
        }
    }
    context.measure("mapped", mapped_kinds);
    if (mapped_kinds == 0) {
        return Context::skip(api + " exposed no mappable host-visible allocation");
    }
    return problems.outcome();
}

using Pass = Outcome (*)(Context&, const std::string&, bool);

Outcome role_for(Context& context, const std::string& role, Pass pass) {
    const bool write = role.rfind("write-", 0) == 0;
    if (!write && role.rfind("scan-", 0) != 0) {
        return Outcome::fail("unknown role " + role);
    }
    return pass(context, role.substr(role.find('-') + 1), write);
}

Outcome run_pairs(Context& context, Pass pass, const std::vector<std::string>& apis, int timeout) {
    if (ChildMode::active()) {
        return RobustChild::child(context, [&](Context& inner, const std::string& role) { return role_for(inner, role, pass); });
    }
    const std::vector<std::string> enabled = ApiPolicy::filter(apis);
    if (enabled.empty()) {
        return Context::skip("all APIs of the row are excluded by LSW_SUITES_SKIP_APIS");
    }
    Problems problems;
    int ran = 0;
    std::string skips;
    for (const std::string& api : enabled) {
        const CheckResult writer = RobustChild::run(context.id(), "write-" + api, {}, timeout, -1);
        if (writer.status == Status::Skip) {
            skips += api + ": " + writer.reason + "; ";
            continue;
        }
        if (writer.status != Status::Ok) {
            problems.add(api + " writer: " + Outcome::name(writer.status) + " " + writer.reason);
            continue;
        }
        const CheckResult reader = RobustChild::run(context.id(), "scan-" + api, {}, timeout, -1);
        RobustChild::forward(context, api + ".", reader);
        if (reader.status == Status::Ok) {
            ++ran;
        } else if (reader.status == Status::Skip) {
            skips += api + ": " + reader.reason + "; ";
        } else {
            problems.add(api + " second process: " + Outcome::name(reader.status) + " " + reader.reason);
        }
    }
    if (!problems.empty()) {
        return problems.outcome();
    }
    return ran == 0 ? Context::skip(skips) : Outcome::ok();
}

}  // namespace

Outcome FreshChecks::fresh_alloc_zero_or_unspecified(Context& context) {
    return run_pairs(context, buffers_pass, kApis, 600);
}

Outcome FreshChecks::fresh_vram_pattern(Context& context) {
    return run_pairs(context, vram_pass, kApis, 900);
}

Outcome FreshChecks::fresh_host_visible(Context& context) {
    return run_pairs(context, host_pass, kApis, 300);
}

}  // namespace lsw::suites::sanity::iso
