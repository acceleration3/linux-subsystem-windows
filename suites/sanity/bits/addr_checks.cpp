#include "sanity/bits/addr_checks.hpp"

#include "core/child_mode.hpp"
#include "sanity/enumeration/identity_checks.hpp"
#include "sanity/err/delegate.hpp"
#include "sanity/err/recovery_checks.hpp"
#include "sanity/iso/loads.hpp"
#include "support/api_policy.hpp"
#include "support/backend_harness.hpp"
#include "support/child_runs.hpp"
#include "support/pattern.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace lsw::suites::sanity::bits {

namespace {

constexpr std::uint64_t kChunk = 128ull << 20;

struct Mapped {
    std::unique_ptr<GpuBuffer> buffer;
    std::uint8_t* pointer = nullptr;
};

Outcome probe(Context& context, const std::string& api, bool strict_payload) {
    AvAdapter adapter;
    std::string error;
    std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
    if (backend == nullptr) {
        return Context::skip(error);
    }
    if (!backend->supports_map()) {
        return Context::skip(api + " exposes no mapped host-visible buffers");
    }
    std::vector<Mapped> held;
    std::uint64_t total = 0;
    std::uintptr_t highest = 0;
    std::string first_failure;
    while (total < (3800ull << 20)) {
        AllocStatus status = AllocStatus::Ok;
        std::string detail;
        Mapped next;
        next.buffer = backend->create(kChunk, BufferKind::Upload, status, detail);
        void* pointer = nullptr;
        if (next.buffer == nullptr) {
            first_failure = "create: " + GpuBackends::describe(status) + " " + detail;
            break;
        }
        if (!backend->map(*next.buffer, &pointer, detail) || pointer == nullptr) {
            first_failure = "map: " + detail;
            break;
        }
        next.pointer = static_cast<std::uint8_t*>(pointer);
        highest = std::max(highest, reinterpret_cast<std::uintptr_t>(pointer) + static_cast<std::uintptr_t>(kChunk));
        Pattern::fill(held.size() + 1, 0, next.pointer, 4096);
        Pattern::fill(held.size() + 1, kChunk - 4096, next.pointer + kChunk - 4096, 4096);
        total += kChunk;
        held.push_back(std::move(next));
    }
    Problems problems;
    for (std::size_t i = 0; i < held.size(); ++i) {
        std::uint8_t head[4096];
        std::uint8_t tail[4096];
        Pattern::fill(i + 1, 0, head, sizeof(head));
        Pattern::fill(i + 1, kChunk - 4096, tail, sizeof(tail));
        if (std::memcmp(head, held[i].pointer, sizeof(head)) != 0 || std::memcmp(tail, held[i].pointer + kChunk - 4096, sizeof(tail)) != 0) {
            problems.add(api + ": mapping " + std::to_string(i) + " lost its data (wrapped or overlapping addresses)");
            break;
        }
    }
    context.measure(api + ".mapped_bytes", static_cast<long long>(total));
    context.measure(api + ".highest_address", static_cast<long long>(highest));
    context.measure(api + ".first_failure", first_failure);
    if (strict_payload && highest > 0x80000000u && sizeof(void*) == 4) {
        context.measure(api + ".above_2gib", 1);
    }
    for (Mapped& item : held) {
        std::string detail;
        static_cast<void>(backend->unmap(*item.buffer, detail));
    }
    return problems.outcome();
}

Outcome laa_role(Context& context, const std::string& name) {
    if (name != "laa") {
        return Outcome::fail("unknown role " + name);
    }
    for (const std::string& api : ApiPolicy::filter({"d3d12", "vulkan", "opencl", "d3d11"})) {
        const Outcome outcome = probe(context, api, true);
        if (outcome.status() == Status::Fail) {
            return outcome;
        }
    }
    return Outcome::ok();
}

bool patched_copy(bool aware, std::filesystem::path& out, std::string& error) {
    out = std::filesystem::temp_directory_path() / (aware ? "lsw-suites-laa-on.exe" : "lsw-suites-laa-off.exe");
    std::error_code code;
    std::filesystem::copy_file(ChildRuns::own_exe(), out, std::filesystem::copy_options::overwrite_existing, code);
    if (code) {
        error = "copying the executable failed: " + code.message();
        return false;
    }
    std::fstream file(out, std::ios::in | std::ios::out | std::ios::binary);
    std::uint32_t pe_offset = 0;
    file.seekg(0x3c);
    file.read(reinterpret_cast<char*>(&pe_offset), 4);
    std::uint16_t characteristics = 0;
    file.seekg(pe_offset + 4 + 18);
    file.read(reinterpret_cast<char*>(&characteristics), 2);
    characteristics = aware ? (characteristics | 0x0020) : static_cast<std::uint16_t>(characteristics & 0xFFDFu);
    file.seekp(pe_offset + 4 + 18);
    file.write(reinterpret_cast<const char*>(&characteristics), 2);
    if (!file) {
        error = "patching the PE header failed";
        return false;
    }
    return true;
}

Outcome laa(Context& context, bool aware) {
    if (ChildMode::active()) {
        return RobustChild::child(context, [](Context& inner, const std::string& role) {
            if (role == "laa") {
                return laa_role(inner, role);
            }
            return err::RecoveryChecks::child_role(inner, role);
        });
    }
    std::filesystem::path exe;
    std::string error;
    if (!patched_copy(aware, exe, error)) {
        return Outcome::fail(error);
    }
    ChildRunOptions options;
    options.exe = exe;
    options.timeout_seconds = 600;
    options.env = {{"LSW_SUITES_ROLE", "laa"}};
    CheckResult child;
    if (!ChildRuns::run_sibling(context.id(), options, child, error)) {
        return Outcome::fail(error);
    }
    std::filesystem::remove(exe);
    if (child.status != Status::Ok && child.status != Status::Skip) {
        return Outcome::fail(std::string(Outcome::name(child.status)) + " " + child.reason);
    }
    Problems problems;
    int measured = 0;
    for (const char* name : {"d3d12", "vulkan", "opencl", "d3d11"}) {
        const std::string api = name;
        if (!ChildRuns::has(child, api + ".mapped_bytes")) {
            continue;
        }
        ++measured;
        RobustChild::forward(context, "", child);
        const long long total = ChildRuns::number(child, api + ".mapped_bytes");
        const long long highest = ChildRuns::number(child, api + ".highest_address");
        if (!aware) {
            if (highest > 0x80000000ll) {
                problems.add(api + ": a mapping reached address " + std::to_string(highest) + " without the large-address-aware flag");
            }
            if (total > 2048ll << 20) {
                problems.add(api + ": " + std::to_string(total >> 20) + " MiB were mapped in a 2 GiB address space");
            }
        } else if (total > 1536ll << 20 && ChildRuns::number(child, api + ".above_2gib") != 1) {
            problems.add(api + ": " + std::to_string(total >> 20) + " MiB were mapped but no mapping lies above 2 GiB");
        }
    }
    if (measured == 0) {
        return Context::skip("no API exposes mapped host-visible buffers");
    }
    return problems.outcome();
}

}  // namespace

Outcome AddrChecks::allocation_fails_cleanly(Context& context) {
    Problems problems;
    int ran = 0;
    for (const std::string& api : ApiPolicy::filter({"d3d12", "vulkan", "opencl", "d3d11"})) {
        AvAdapter adapter;
        std::string error;
        std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
        if (backend != nullptr) {
            AllocStatus status = AllocStatus::Ok;
            std::string detail;
            std::unique_ptr<GpuBuffer> huge = backend->create(5ull << 30, BufferKind::Upload, status, detail);
            void* pointer = nullptr;
            if (huge != nullptr && backend->supports_map() && backend->map(*huge, &pointer, detail) && pointer != nullptr) {
                static_cast<volatile std::uint8_t*>(pointer)[0] = 1;
                static_cast<volatile std::uint8_t*>(pointer)[4096] = 1;
            }
        }
        const Outcome outcome = probe(context, api, false);
        if (outcome.status() == Status::Fail) {
            problems.add(outcome.reason());
        } else if (outcome.status() == Status::Ok) {
            ++ran;
        }
    }
    if (ran == 0 && problems.empty()) {
        return Context::skip("no API exposes mapped host-visible buffers");
    }
    return problems.outcome();
}

Outcome AddrChecks::large_address_aware_on(Context& context) {
    return laa(context, true);
}

Outcome AddrChecks::large_address_aware_off(Context& context) {
    return laa(context, false);
}

Outcome AddrChecks::icd_loading(Context& context) {
    return err::Delegate::all(context, {enumeration::IdentityChecks::dxgi_vs_kmt_luid, enumeration::IdentityChecks::vulkan_luid_matches, enumeration::IdentityChecks::opencl_luid_matches,
                                        enumeration::IdentityChecks::gl_renderer_matches});
}

Outcome AddrChecks::exit_cleanup(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, err::RecoveryChecks::child_role);
    }
    Problems problems;
    int measured = 0;
    for (const std::string& api : ApiPolicy::filter({"d3d12", "vulkan", "opencl", "d3d11"})) {
        const CheckResult before = RobustChild::run(context.id(), "free-" + api, {}, 300, -1);
        if (before.status != Status::Ok) {
            continue;
        }
        const CheckResult holder = RobustChild::run(context.id(), "mapper-" + api, std::vector<std::pair<std::string, std::string>>{{iso::Loads::kSeconds, "1.5"}}, 120, -1);
        if (holder.status != Status::Ok && holder.status != Status::Skip) {
            problems.add(api + ": the mapping process: " + holder.reason);
        }
        const CheckResult after = RobustChild::run(context.id(), "free-" + api, {}, 300, -1);
        if (after.status != Status::Ok) {
            problems.add(api + ": the process after the exit failed: " + after.reason);
            continue;
        }
        ++measured;
        for (const char* key : {"device_bytes", "host_bytes"}) {
            const long long was = ChildRuns::number(before, key);
            if (was > 0 && ChildRuns::number(after, key) * 100 < was * 90) {
                problems.add(api + ": " + key + " available to a new process fell from " + std::to_string(was >> 20) + " to " + std::to_string(ChildRuns::number(after, key) >> 20) + " MiB after an exit with live mappings");
            }
        }
    }
    if (measured == 0 && problems.empty()) {
        return Context::skip("no API could run the allocation probe");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::bits
