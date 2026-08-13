// B5: the host/guest topology, minus the hypervisor.
//
// Two processes, no shared heap, no shared file descriptor, no inherited
// pointer. The child is started with ``execv`` rather than left as a
// ``fork`` — a forked child inherits the parent's mappings at the same
// addresses, so a pointer wrongly stored in the region would keep
// working and the test would prove nothing. After an exec the child's
// address space is its own, which is exactly the guest's situation.
//
// What that catches, on this machine, in milliseconds:
//
//   * a pointer stored in the shared region (the classic one)
//   * a missing fence, since the two ends are genuinely concurrent on
//     different cores rather than interleaved threads
//   * a layout assumption that only held because both ends compiled the
//     same struct into the same address space
//
// It also carries the region parity assertion: the child re-derives the
// channel table from ``plan_region`` — arithmetic alone, no memory — and
// compares it against what it actually found by walking the table. If
// the manager's layout for a VM and the loopback's layout for two local
// processes ever diverge, this is where it shows.

#include "lsw/linkproto/region.hpp"
#include "lsw/linkproto/region_layout.hpp"
#include "lsw/protocol/endpoint.hpp"
#include "lsw/protocol/frame_channel.hpp"
#include "lsw/protocol/frame_flags.hpp"
#include "lsw/protocol/posix_shm_region.hpp"
#include "lsw/protocol/shmem_endpoint.hpp"
#include "transport_hammer.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int g_failures = 0;

// Where the child stamps its pattern, relative to the hostmem ARENA.
// Deliberately non-zero: an offset of 0 would still look right if the
// two ends disagreed about which base it is relative to.
constexpr std::uint64_t kArenaProbeOffset = 64 * 1024;
constexpr std::size_t kArenaProbeSize = 256;

void check(bool ok, const char* label) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}

// ---- child ---------------------------------------------------------------

// Everything the child knows is the region's name. It gets its own
// address space from the exec, and its own view of the layout from the
// channel table.
int run_child(const std::string& shm_name) {
    using namespace lsw::protocol;
    namespace linkproto = lsw::linkproto;

    // Parity first, before any traffic: attach raw and check that what
    // the table says matches what the arithmetic says it must say.
    {
        PosixShmRegion region;
        if (region.open(shm_name) != 0) {
            std::fprintf(stderr, "child: cannot open %s\n", shm_name.c_str());
            return 2;
        }
        const auto mapped = static_cast<std::uint64_t>(region.size());
        check(linkproto::region_is_readable(region.base(), mapped),
              "child: region header is one this build understands");

        const auto stated_size = linkproto::load_field<std::uint64_t>(
            static_cast<const std::uint8_t*>(region.base()) + linkproto::region_off::region_size);
        check(stated_size == mapped, "child: region_size matches the mapping");

        const auto expected = linkproto::plan_region(stated_size);
        check(expected.valid, "child: the stated size is one that can be laid out");

        const auto rpc = linkproto::find_channel(region.base(), linkproto::kChannelRpc);
        const auto cmd = linkproto::find_channel(region.base(), linkproto::kChannelCmdStream);
        const auto hostmem = linkproto::find_channel(region.base(), linkproto::kChannelHostMem);
        const auto frames = linkproto::find_channel(region.base(), linkproto::kChannelFrames);

        check(rpc.offset == expected.rpc.offset && rpc.size == expected.rpc.size,
              "child: rpc channel is where the layout says");
        check(cmd.offset == expected.cmdstream.offset && cmd.size == expected.cmdstream.size,
              "child: cmdstream channel is where the layout says");
        check(hostmem.offset == expected.hostmem.offset && hostmem.size == expected.hostmem.size,
              "child: hostmem channel is where the layout says");
        // The arena the server imports from has to lie wholly inside the
        // mapping, or an import would hand the driver a pointer past the
        // end of the region.
        check(hostmem.offset + hostmem.size <= stated_size,
              "child: the hostmem arena fits inside the region");
        check(frames.offset == expected.frames.offset && frames.size == expected.frames.size,
              "child: frames channel is where the layout says");
        // The frame channel absorbs the remainder, so this also asserts
        // that nothing was silently lost to padding.
        check(frames.offset + frames.size == stated_size,
              "child: the frame channel runs to the end of the region");
    }

    // Write a pattern into the hostmem arena at a known arena-relative
    // offset. The parent checks it at region_base + channel.offset +
    // arena_offset — which is the calculation the import path performs,
    // and which was wrong once in a way nothing caught: the server
    // resolved arena offsets against the REGION base instead of the
    // CHANNEL base. Everything still ran. The cube came out black.
    {
        PosixShmRegion region;
        if (region.open(shm_name) != 0) {
            return 2;
        }
        const auto channel = linkproto::find_channel(region.base(), linkproto::kChannelHostMem);
        check(channel.valid(), "child: hostmem channel present");
        auto* arena = static_cast<std::uint8_t*>(region.base()) + channel.offset;
        for (std::size_t i = 0; i < kArenaProbeSize; ++i) {
            arena[kArenaProbeOffset + i] = static_cast<std::uint8_t>(0xA5 ^ (i & 0xFF));
        }
    }

    auto transport = open_transport("shm://" + shm_name);
    if (transport == nullptr) {
        std::fprintf(stderr, "child: open_transport failed\n");
        return 2;
    }

    test::HammerResult r;
    test::hammer_transport(*transport, r);
    transport.reset();

    check(r.transport_errors.load() == 0, "child: no transport errors");
    check(r.mismatched_replies.load() == 0, "child: every reply matched its request");
    check(r.short_replies.load() == 0, "child: no truncated replies");
    check(r.sync_calls.load() > 0, "child: sync calls actually ran");

    if (g_failures != 0) {
        return 1;
    }
    std::printf("  child: %llu sync, %llu async across the process boundary\n",
                static_cast<unsigned long long>(r.sync_calls.load()),
                static_cast<unsigned long long>(r.async_calls.load()));
    return 0;
}

// ---- parent --------------------------------------------------------------

[[nodiscard]] pid_t spawn_child(const std::string& shm_name) {
    const pid_t pid = ::fork();
    if (pid != 0) {
        return pid;  // parent, or -1 if the fork failed
    }
    // exec, not just fork: the child must not inherit our mappings.
    std::string self = "/proc/self/exe";
    std::string flag = "--child";
    std::vector<char*> argv{self.data(), flag.data(), const_cast<char*>(shm_name.c_str()),
                            nullptr};
    ::execv("/proc/self/exe", argv.data());
    std::fprintf(stderr, "parent: execv failed: %s\n", std::strerror(errno));
    ::_exit(127);
}

int run_parent() {
    using namespace lsw::protocol;
    namespace linkproto = lsw::linkproto;

    const std::string shm_name = "/lsw-shmem-xp-" + std::to_string(::getpid());

    ShmemListener listener;
    check(listener.create(shm_name) == 0, "parent: region created and laid out");

    auto channel = listener.accept();
    check(channel != nullptr, "parent: server channel bound to the cmdstream channel");
    if (channel == nullptr) {
        return 1;
    }

    std::atomic<bool> serving{true};
    std::thread server{[&] {
        while (serving.load()) {
            Frame f;
            const IoResult r = channel->read_frame(f);
            if (r == IoResult::Timeout) {
                continue;
            }
            if (r != IoResult::Ok) {
                break;
            }
            if ((f.flags & FrameFlags::needs_reply) == 0) {
                continue;
            }
            if (channel->write_reply(f, {f.payload.data(), f.payload.size()}) != IoResult::Ok) {
                break;
            }
        }
    }};

    const pid_t child = spawn_child(shm_name);
    check(child > 0, "parent: child spawned");

    int status = 0;
    ::waitpid(child, &status, 0);
    check(WIFEXITED(status) != 0, "parent: child exited normally");
    check(WIFEXITED(status) != 0 && WEXITSTATUS(status) == 0, "parent: child reported success");

    serving.store(false);
    channel->shutdown_read();
    server.join();

    // The parent resolves the child's arena-relative offset exactly the
    // way ImportHandler does. If the two ends ever disagree about which
    // base an arena offset is relative to, this is where it shows —
    // rather than as a black cube in a screenshot nobody took.
    {
        auto region = listener.backing_region();
        check(region != nullptr, "parent: listener exposes its region");
        if (region != nullptr) {
            const auto channel_view =
                linkproto::find_channel(region->base(), linkproto::kChannelHostMem);
            check(channel_view.valid(), "parent: hostmem channel present");
            const auto* arena =
                static_cast<const std::uint8_t*>(region->base()) + channel_view.offset;
            bool matched = true;
            for (std::size_t i = 0; i < kArenaProbeSize; ++i) {
                if (arena[kArenaProbeOffset + i] != static_cast<std::uint8_t>(0xA5 ^ (i & 0xFF))) {
                    matched = false;
                    break;
                }
            }
            check(matched,
                  "parent: arena offsets resolve to the same bytes on both sides "
                  "(relative to the CHANNEL, not the region)");
        }
    }

    // Attach mode: a SECOND listener serving a region it did not create.
    // That is the VM shape — the manager owns the region, the render
    // server only serves its cmdstream channel — and the property that
    // matters is that attaching neither re-lays-out the region nor
    // unlinks the name on close.
    {
        ShmemListener attached;
        check(attached.attach(shm_name) == 0, "parent: attach to an existing region");
        check(attached.backing_region() != nullptr, "parent: attached listener has the region");
        auto ch = attached.accept();
        check(ch != nullptr, "parent: attached listener yields a cmdstream channel");
        if (ch != nullptr) {
            ch->close();
        }
        attached.close();
        // The name must survive: it belongs to whoever created it.
        PosixShmRegion still_there;
        check(still_there.open(shm_name) == 0,
              "parent: closing an ATTACHED listener does not unlink the owner's region");
    }
    {
        ShmemListener nothing;
        check(nothing.attach("/lsw-does-not-exist-" + std::to_string(::getpid())) == -ENOENT,
              "parent: attaching to a missing region reports ENOENT");
    }

    listener.close();
    return g_failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string_view{argv[1]} == "--child") {
        return run_child(argv[2]);
    }
    const int rc = run_parent();
    if (rc == 0) {
        std::puts("OK");
    }
    return rc;
}
