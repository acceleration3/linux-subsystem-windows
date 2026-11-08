#include "sanity/kmd/query_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "sanity/enumeration/memory_checks.hpp"
#include "support/child_runs.hpp"
#include "support/kmt_api.hpp"
#include "support/kmt_extra.hpp"
#include "support/problems.hpp"
#include "support/robust_child.hpp"
#include "support/swap_rig.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace lsw::suites::sanity::kmd {

namespace {

constexpr std::uint32_t kBuffer = 4096;
constexpr std::uint32_t kKnownTypes[] = {1, 3, 4, 6, 8, 13};

struct Session {
    KmtApi api;
    AvAdapter adapter;
    std::uint32_t handle = 0;

    bool open(std::string& error) {
        if (!TargetAdapter::pick(adapter, error) || !api.load(error)) {
            return false;
        }
        const std::int32_t status = api.open_from_luid(adapter.luid, handle);
        if (status != 0) {
            error = "D3DKMTOpenAdapterFromLuid returned 0x" + Text::hex(static_cast<std::uint32_t>(status), 8);
            return false;
        }
        return true;
    }

    ~Session() {
        if (handle != 0) {
            static_cast<void>(api.close(handle));
        }
    }
};

std::string dump(Session& session) {
    std::string text;
    for (std::uint32_t type : kKnownTypes) {
        std::vector<std::uint8_t> bytes(kBuffer, 0xCD);
        const std::int32_t status = session.api.query(session.handle, type, bytes.data(), kBuffer);
        text += std::to_string(type) + ":" + Text::hex(static_cast<std::uint32_t>(status), 8) + ":";
        std::uint64_t hash = 1469598103934665603ull;
        for (std::uint8_t byte : bytes) {
            hash = (hash ^ byte) * 1099511628211ull;
        }
        text += Text::hex(static_cast<std::uint32_t>(hash), 8) + Text::hex(static_cast<std::uint32_t>(hash >> 32), 8) + ";";
    }
    return text;
}

Outcome role(Context& context, const std::string& name) {
    if (name != "dump") {
        return Outcome::fail("unknown role " + name);
    }
    Session session;
    std::string error;
    if (!session.open(error)) {
        return Outcome::fail(error);
    }
    context.measure("dump", dump(session));
    return Outcome::ok();
}

Outcome with_session(Session& session) {
    std::string error;
    return session.open(error) ? Outcome::ok() : Outcome::fail(error);
}

}  // namespace

Outcome QueryChecks::adapter_info_stable(Context& context) {
    if (ChildMode::active()) {
        return RobustChild::child(context, role);
    }
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    const std::string first = dump(session);
    Problems problems;
    for (int i = 0; i < 20; ++i) {
        if (dump(session) != first) {
            problems.add("the answers to the documented adapter queries changed on repeat " + std::to_string(i));
            break;
        }
    }
    const CheckResult child = RobustChild::run(context.id(), "dump", {}, 60, -1);
    if (child.status != Status::Ok) {
        problems.add("the second process: " + child.reason);
    } else if (ChildRuns::text(child, "dump") != first) {
        problems.add("a second process gets other answers: [" + ChildRuns::text(child, "dump") + "] against [" + first + "]");
    }
    context.measure("dump", first);
    return problems.outcome();
}

Outcome QueryChecks::size_semantics(Context& context) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    Problems problems;
    std::vector<std::uint8_t> bytes(64, 0xCD);
    const std::int32_t small = session.api.query(session.handle, 3, bytes.data(), 8);
    if (small == 0) {
        problems.add("a 24-byte segment size query with an 8-byte buffer succeeded");
    }
    for (std::size_t i = 8; i < bytes.size(); ++i) {
        if (bytes[i] != 0xCD) {
            problems.add("the undersized query wrote at byte " + std::to_string(i));
            break;
        }
    }
    std::fill(bytes.begin(), bytes.end(), 0xCD);
    const std::int32_t large = session.api.query(session.handle, 3, bytes.data(), 64);
    constexpr std::int32_t kInvalidParameter = static_cast<std::int32_t>(0xC000000D);
    context.measure("segment_size_query_64_bytes", "0x" + Text::hex(static_cast<std::uint32_t>(large), 8));
    if (large != 0 && large != kInvalidParameter) {
        problems.add("the segment size query with a 64-byte buffer returned 0x" + Text::hex(static_cast<std::uint32_t>(large), 8) + ", expected success or STATUS_INVALID_PARAMETER");
    }
    for (std::size_t i = 24; large == 0 && i < bytes.size(); ++i) {
        if (bytes[i] != 0xCD) {
            problems.add("the oversized query changed the extra byte " + std::to_string(i));
            break;
        }
    }
    return problems.outcome();
}

Outcome QueryChecks::unsupported_type_status(Context&) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    const std::string before = dump(session);
    Problems problems;
    for (std::uint32_t type : {0x7fffffffu, 0xffffffffu, 100000u, 4096u}) {
        std::vector<std::uint8_t> bytes(64, 0);
        if (session.api.query(session.handle, type, bytes.data(), 64) == 0) {
            problems.add("querying the undefined type " + std::to_string(type) + " succeeded");
        }
    }
    if (dump(session) != before) {
        problems.add("the answers changed after the undefined queries");
    }
    return problems.outcome();
}

Outcome QueryChecks::segment_info_consistent(Context& context) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    KmtSegmentSizes sizes;
    if (!session.api.segment_sizes(session.adapter.luid, sizes)) {
        return Outcome::fail("the segment size query failed");
    }
    context.measure("dedicated_video", static_cast<long long>(sizes.dedicated_video));
    context.measure("shared_system", static_cast<long long>(sizes.shared_system));
    Problems problems;
    if (sizes.dedicated_video + sizes.dedicated_system + sizes.shared_system == 0) {
        problems.add("every segment size is zero");
    }
    const Outcome shared = enumeration::MemoryChecks::dedicated_vs_shared_consistent(context);
    if (shared.status() == Status::Fail) {
        problems.add(shared.reason());
    }
    return problems.outcome();
}

Outcome QueryChecks::driver_caps_self_consistent(Context& context) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    std::uint32_t version = 0;
    const std::int32_t status = session.api.query(session.handle, 13, &version, sizeof(version));
    if (status != 0) {
        return Outcome::fail("KMTQAITYPE_DRIVERVERSION returned 0x" + Text::hex(static_cast<std::uint32_t>(status), 8));
    }
    context.measure("wddm_version", version);
    Problems problems;
    if (version < 2000) {
        problems.add("the WDDM driver version is " + std::to_string(version) + ", below 2.0");
    }
    SwapRig rig;
    SwapOptions options;
    options.effect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    std::string error;
    if (!rig.open(options, error)) {
        problems.add("a WDDM 2.0 adapter must support flip-model swap chains: " + error);
    }
    return problems.outcome();
}

Outcome QueryChecks::open_adapter_from_hdc_and_name(Context&) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    if (session.adapter.outputs.empty()) {
        return Context::skip("the adapter has no output to open by name");
    }
    KmtExtra extra;
    std::string error;
    if (!extra.load(error)) {
        return Outcome::fail(error);
    }
    const AvOutput& output = session.adapter.outputs.front();
    const std::wstring device(output.device_name.begin(), output.device_name.end());
    Problems problems;
    std::uint32_t by_name = 0;
    std::uint32_t source = 0;
    LUID name_luid{};
    const std::int32_t named = extra.open_from_display_name(device, by_name, name_luid, source);
    HDC dc = ::CreateDCW(L"DISPLAY", device.c_str(), nullptr, nullptr);
    std::uint32_t by_dc = 0;
    LUID dc_luid{};
    std::uint32_t dc_source = 0;
    const std::int32_t from_dc = dc != nullptr ? extra.open_from_hdc(dc, by_dc, dc_luid, dc_source) : -1;
    if (dc != nullptr) {
        ::DeleteDC(dc);
    }
    auto same = [&](const LUID& luid) { return luid.LowPart == session.adapter.luid.LowPart && luid.HighPart == session.adapter.luid.HighPart; };
    if (named != 0 || !same(name_luid)) {
        problems.add("D3DKMTOpenAdapterFromGdiDisplayName returned 0x" + Text::hex(static_cast<std::uint32_t>(named), 8) + " and another LUID");
    }
    if (from_dc != 0 || !same(dc_luid)) {
        problems.add("D3DKMTOpenAdapterFromHdc returned 0x" + Text::hex(static_cast<std::uint32_t>(from_dc), 8) + " and another LUID");
    }
    if (named == 0 && from_dc == 0 && source != dc_source) {
        problems.add("the two opens report different VidPn sources");
    }
    for (std::uint32_t handle : {by_name, by_dc}) {
        if (handle != 0) {
            static_cast<void>(session.api.close(handle));
        }
    }
    return problems.outcome();
}

Outcome QueryChecks::close_adapter_twice(Context&) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    std::uint32_t other = 0;
    if (session.api.open_from_luid(session.adapter.luid, other) != 0) {
        return Outcome::fail("a second open failed");
    }
    Problems problems;
    if (session.api.close(other) != 0) {
        problems.add("the first close failed");
    }
    if (session.api.close(other) == 0) {
        problems.add("closing the same adapter handle a second time succeeded");
    }
    KmtSegmentSizes sizes;
    if (!session.api.segment_sizes(session.adapter.luid, sizes)) {
        problems.add("the adapter is unusable after the double close");
    }
    if (session.api.query(session.handle, 3, &sizes, sizeof(sizes)) != 0) {
        problems.add("the first handle stopped working after the other handle was closed twice");
    }
    return problems.outcome();
}

Outcome QueryChecks::escape_malformed_rejected(Context&) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    KmtExtra extra;
    std::string error;
    if (!extra.load(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::vector<std::uint8_t> garbage(256);
    for (std::size_t i = 0; i < garbage.size(); ++i) {
        garbage[i] = static_cast<std::uint8_t>(i * 37 + 11);
    }
    struct Case {
        const char* what;
        void* data;
        std::uint32_t size;
    };
    const Case cases[] = {{"null data with size 0", nullptr, 0}, {"null data with a size", nullptr, 64}, {"garbage with size 1", garbage.data(), 1},
                          {"garbage with size 4", garbage.data(), 4}, {"garbage with 256 bytes", garbage.data(), 256}, {"garbage with an absurd size", garbage.data(), 0x7fffffff}};
    for (const Case& item : cases) {
        const std::int32_t status = extra.escape(session.handle, 0, 0, item.data, item.size);
        if (status == 0 && item.data == nullptr && item.size != 0) {
            problems.add(std::string("an escape with ") + item.what + " succeeded");
        }
    }
    return problems.outcome();
}

Outcome QueryChecks::escape_unknown_command_status(Context&) {
    Session session;
    const Outcome opened = with_session(session);
    if (opened.status() != Status::Ok) {
        return opened;
    }
    KmtExtra extra;
    std::string error;
    if (!extra.load(error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::uint32_t command[16] = {0xDEADBEEFu, 0x12345678u};
    const std::string before = dump(session);
    const std::int32_t status = extra.escape(session.handle, 0, 0, command, sizeof(command));
    if (status == 0) {
        problems.add("an escape with an unrecognised command value succeeded");
    }
    SwapRig rig;
    SwapOptions options;
    if (!rig.open(options, error) || !rig.fill(0.2f, 0.4f, 0.6f, 1.0f, error) || FAILED(rig.chain->Present(1, 0))) {
        problems.add("rendering after the unknown escape failed: " + error);
    }
    if (dump(session) != before) {
        problems.add("the adapter answers changed after the unknown escape");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::kmd
