# Plan — transport, threading, and the guest Vulkan path

> **Historical.** The guest GPU path this plan builds towards — the
> render server, its ICDs, and a command stream over IVSHMEM — has been
> replaced by [accel-virt](../external/accel-virt): a WDDM paravirtual
> adapter whose own QEMU device, `virtio-gpu-av-pci`, carries GPU work,
> with no region of LSW's involved. The IVSHMEM region, its doorbell
> server, the guest's ivshmem.sys and the shared-memory RPC ring this
> plan describes have since been removed outright: the agent link is a
> TCP connection and nothing else. So has virtio-win: the VM's disk is
> NVMe and its NIC an e1000e, both driven by Windows' in-box drivers,
> and no driver ISO is fetched or attached. Read it as the record of how the
> transport got its shape; [link-protocol.md](link-protocol.md) is what
> is true now.

Everything discussed, ordered by what unblocks what. Sizes are S / M / L
relative to each other, not calendar time.

---

## The two rules everything else answers to

### Rule 1 — the VM runs the same code as local

Not "an equivalent path". The same classes, the same ring, the same
framing, the same dispatch. The complete list of things allowed to differ
between "two processes on this machine" and "host and guest":

| concern | local | VM |
|---|---|---|
| mapping the pages | `PosixShmRegion` | `IvshmemRegion` |
| waking the peer | `SpinPollDoorbell` (or eventfd) | doorbell guest→host, poll host→guest |

That is the whole list. **Anything else that needs to know which
environment it is in is a design failure**, and the review question for
every patch in this plan is: does this add a third row to that table?

Concretely that means no `#ifdef GUEST`, no `if (in_vm)`, no parallel
"local" implementation of a guest feature. `ShmemFrameChannel`,
`ChannelTransport`, the ring, the region/channel layout, the ICD and the
render server are single implementations used unchanged on both sides.

The host half of the VM case is *already* a `PosixShmRegion` — the
manager creates POSIX shm and hands the fd to QEMU. So the local path is
not a simulation of the VM path; it is one end of it, with the guest end
swapped for a second local process.

### Rule 2 — every phase is demonstrable on this machine

If a change cannot be shown working locally, the abstraction is in the
wrong place — that is the signal, not an inconvenience. Each phase below
names its local verification. Only E2/E3 (guest loader + registration)
and I (D3D in the guest) genuinely require a VM, and by then everything
underneath them has already been exercised locally.

Two harnesses carry this:

- **`scripts/vkcube-loopback.sh`** — the end-to-end one. Real app, real
  driver, real window. Gains a transport switch so the same script runs
  the cube over `unix://` or `shm://` and the two can be diffed.
- **a two-process ring harness** (new, `lsw_shmem_transport_test`) — one
  process creates the region and serves, another attaches *by name only*
  and drives it. That is exactly the host/guest topology minus the
  hypervisor: neither side shares a heap, a file descriptor, or a
  pointer. Bugs that only appear across an address-space boundary —
  stale pointers stored in the region, missing fences, layout drift —
  surface here, on this machine, in milliseconds.

Plus a **region parity test**: the channel table the manager writes for a
VM and the one the loopback writes locally must be byte-identical for the
same size, asserted directly rather than assumed.

---

## Phase A — correctness first (do before anything builds on it)

### A1. Thread-safe client transport — **LIVE BUG** — S
`UnixSocketTransport::send_sync` (`src/protocol/src/unix_endpoint.cpp:146`)
has no lock: `next_token_++` races, two threads interleave header+payload
on one fd, and either may consume the other's reply. One transport per
`VkInstance`, so *any* multithreaded Vulkan app corrupts the stream
today. DXVK and vkd3d-proton both run submission threads, so this fires
the moment the guest path is real.

Fix with the shape already proven in `AgentRpcClient`: short lock around
the write, `token -> waiter` map, one reader thread delivering replies,
each caller waiting on its own condvar. Async sends take only the write
lock. Applies to whatever implements `Transport`, so do it once in the
new `ChannelTransport` (B3) rather than twice.

Verify: a test that hammers one transport from N threads with mixed
sync/async calls and checks every reply matches its request.

### A2. Concurrency audit of the shared server/ICD state — S
`SurfaceManager` has a mutex; check and document the rest: `HandleTable`,
`MemoryShadow`, `connection_of`/handle wrapping, `Session`. Anything an
app can reach from two threads needs a stated rule, even if the rule is
"single replay thread, so no lock needed".

### A3. Codegen — decide and land — S
The parse fix (feature blocks split into `VK_BASE_/COMPUTE_/GRAPHICS_`)
is done: 219 commands again, not 13. Still open: the committed
`generated/` does not match the current emitter (it contains reserved-id
cases the emitter no longer produces), so `check-codegen` fails even now.
Either regenerate and commit, or find what diverged. Until this is
settled nobody can safely run `regenerate-vulkan`.

Also: `src/vkproto/CMakeLists.txt` puts
`third_party/Vulkan-Headers/include` on the include path and that
directory does not exist — headers currently come from the system. Must
be fixed before any cross-build (D1).

### A4. IVSHMEM driver as a wizard step — S
The driver is now staged onto the payload ISO and injected offline, but
only if the host already has it: `IvshmemDriver::locate` checks
`$LSW_IVSHMEM_DRIVER_DIR`, then the cache, then any Looking Glass package
sitting in the cache. Nothing puts one there, so a clean machine still
needs a manual step.

Close it with a **host-side** wizard step, in the same download phase
that already fetches the Windows ISO and virtio-win — download the
Looking Glass host package into the data dir, before the payload ISO is
built. `locate`'s third branch already scans for exactly that filename,
so the download step is all that is missing; extraction and staging are
done.

Host-side is the whole point, and worth stating explicitly since the old
step was guest-side: a driver fetched *by the guest* arrives over the
channel it is supposed to provide. Fetching it on the host, before the VM
exists, is what breaks that circle — and it is also what lets the wizard
report "no ivshmem driver" as a normal download failure with a retry,
rather than as a mysteriously missing device after install.

Verify locally: point the downloader at the cache, assert `locate`
returns a directory that `looks_like_driver_dir` accepts, and that
`stage_files` puts it on the ISO. No VM needed — the existing ISO test
already covers the staging half.

Once this lands, delete the guest-side `IVSHMEM driver` entry from
`setup-steps.json` (currently marked as a fallback).

---

## Phase B — finish the transport abstraction, locally — M

The keystone. After this, "local" and "in a VM" differ only in which
`SharedRegion` is handed over.

### B1. Finish what is half-written — S
Written already: `frame_channel.hpp`, `shared_region.hpp`,
`doorbell.hpp`, `posix_shm_region.{hpp,cpp}`,
`shmem_frame_channel.hpp`. Still needed:
- `doorbell.cpp` — `SpinPollDoorbell::wait` (spin, then sleep in slices)
- `shmem_frame_channel.cpp` — frame = one ring record:
  `[cmd_id:u32][flags:u8][reply_token:u64][payload]`
- `fd_frame_channel.{hpp,cpp}` — wraps the existing `FrameStream`, so the
  socket becomes the *other* implementation rather than the only one

### B2. `Listener` yields channels, not fds — S
`Listener::accept()` currently returns `AcceptedFd`. Change to
`std::unique_ptr<FrameChannel>`; `UnixListener` returns an
`FdFrameChannel`, and a new `ShmemListener` returns a
`ShmemFrameChannel`. `render::Connection` holds a `FrameChannel` instead
of a `FrameStream`. Mechanical, touches few files.

### B3. `ChannelTransport` — S
Client-side `Transport` over any `FrameChannel`, with A1's threading
model built in. Replaces `UnixSocketTransport`'s role; the socket keeps
working because it is now just a channel.

### B4. `shm://` in the URL layer — S
`open_transport` / `open_listener` learn the scheme, so
`LSW_SERVER_SOCKET=shm:///lsw-vk-0` is the only difference between
running the proxy over a socket and over shared memory.

### B5. Prove it, two ways — S
1. **Two-process ring harness** (`lsw_shmem_transport_test`): a child
   process attaches to the region *by name only* and echoes frames.
   Separate address spaces, so it catches exactly the class of bug the
   VM would — a pointer stored in the region, a missing fence, a layout
   assumption that only held because both ends shared a heap.
2. **`scripts/vkcube-loopback.sh --transport shm`**: same script, same
   assertions, different scheme. Run both schemes and diff behaviour;
   a divergence is a shmem bug with a socket-shaped control already in
   hand.

Also land the **region parity test** here: assert the channel table the
manager writes for a VM equals the one the loopback writes locally.

**Milestone: vkcube renders through shared memory, locally, across a
process boundary — the same code path the guest will use.**

---

## Phase C — command-buffer batching — M

Independent of B; the single biggest throughput change available, and
the thing that makes multithreaded apps scale rather than merely work.

Every `vkCmd*` is already `send_async` (111 of 219 commands are). A
command buffer is externally synchronized, so exactly one thread owns it
while recording. Therefore: accumulate that thread's `vkCmd*` stream in a
**per-`VkCommandBuffer` local buffer** and ship one message at
`vkEndCommandBuffer`.

- no shared state in the hot path — N recording threads, zero contention
- thousands of tiny writes become one large one, which the ring prefers
- per-buffer order holds by construction; recorded-before-submitted holds
  because End flushes before any submit can name the buffer
- `vkResetCommandBuffer` / pool reset must drop the pending batch
- secondary buffers (`vkCmdExecuteCommands`) need the same treatment

Local verification: a multithreaded recorder against the loopback (N
threads, own pool each), message count per frame before/after, and
identical rendered output. Runs on this machine over both transports —
nothing here is VM-specific, which is the point: by the time the guest
exists, batching is already proven.

**Milestone: N threads record concurrently with no transport contention.**

---

## Phase D — build the ICD for Windows — M

Prerequisites live here, not in the guest.

### D1. Hermetic Vulkan headers — S
FetchContent `Vulkan-Headers` the way `nlohmann_json` is already fetched;
drop the dead `third_party/` include path. Cross-builds need headers the
mingw toolchain can see.

### D2. Logging that cross-compiles — S
`LSW::Base` is spdlog, and `LSWDependencies.cmake` skips spdlog entirely
for mingw. Either fetch spdlog header-only for the cross build, or give
`lsw/base/log.hpp` a fallback sink. The agent already solved this
problem its own way (`lsw/agent/log.hpp`) — do not add a third.

### D3. Portability of the ICD + protocol — M
- `icd/src/connection.cpp` reads `/proc/self/comm` — needs a Windows path
- `protocol/src/frame_stream.cpp`, `unix_endpoint.cpp` are POSIX sockets
  — they stay, but must not be *required* by a Windows build; with B done
  the ICD needs only `ShmemFrameChannel`, which is portable
- `vkCreateWin32SurfaceKHR` — trivial now: ignore the HWND and call the
  same `create_host_surface`, exactly like the xcb entry does

### D4. `lsw_icd.dll` — S
Build the ICD as a DLL under the mingw toolchain (drop the
`if(NOT UNIX) return()` guard, add the target), alongside the existing
`.so`. Same sources.

**Milestone: `lsw_icd.dll` exists and is built by `scripts/build-agent.sh`.**

---

## Phase E — guest bring-up — M

### E1. Guest `SharedRegion` — S
Wrap the agent's existing IVSHMEM open (`agent/src/transport/shmem.cpp`)
as a `SharedRegion`, and point the ICD's `cmdstream` channel at it. The
channel is already in the region, already mapped by both sides, with a
doorbell vector reserved — it has simply had no producer.

This is the **only** new code the guest needs for the transport: rule 1's
second row. Everything above it — channel, transport, batching, ICD —
shipped and was tested locally in B, C and D. If E1 turns out to need
more than a `SharedRegion` implementation, something upstream leaked an
environment assumption and that is the bug to fix.

Note the asymmetry that is already known: guest→host can doorbell, host→
guest cannot, so the guest polls for replies. `SpinPollDoorbell` is
exactly that, and a thread blocked on a reply is not doing anything else
anyway.

### E2. Make the guest able to load a Vulkan ICD at all — M
- ship a Vulkan **loader** (`vulkan-1.dll`) — a VM with no GPU driver has
  none, and without it nothing can find our ICD
- register the ICD: `HKLM\SOFTWARE\Khronos\Vulkan\Drivers` → our JSON
- both belong on the payload ISO / autounattend, for the same reason the
  IVSHMEM driver now does: they must exist before anything runs

### E3. Run it — S
vkcube inside the guest, rendering into a host window — through the same
`ShmemFrameChannel`, the same rings, the same render server the local
loopback has been exercising since B5. The comparison to make: local
`shm://` run and guest run should differ in the region mapper and in
nothing else, including logs.

**Milestone: a guest application's pixels reach a host window via the GPU path.**

---

## Phase F — move the control plane, retire TCP — S/M

The IVSHMEM driver now ships on the payload ISO and is injected offline
(done), so the device is live on first boot and the bootstrap objection
is gone.

- switch `AgentRpcClient` / `HostEndpoint` to prefer the shmem `rpc`
  channel; the plumbing and the `channel_hello` handshake already exist
- TCP moves behind a flag: debug transport and future remote-host path,
  not the default
- while both planes are on rings, drop the now-redundant length prefixes
  — the ring frames records atomically, so the envelope's `len` and the
  Vulkan `FrameHeader`'s `payload_len` are both restating what the record
  already knows

Also fix here: the agent serves **one connection at a time**
(`HostEndpoint::run` calls `handle_connection` inline), so MCP and the
manager contend. Needs per-connection handling and multi-consumer event
sources.

---

## Phase G — memory without the copy — M

Keep the shadow as the *model* (explicit map/flush points are needed for
write-combined memory and for Vulkan's memory-type rules), change what
backs it:

- host-visible + importable → the mapped pointer lives in the shared
  region, imported server-side via `VK_EXT_external_memory_host`,
  respecting `minImportedHostPointerAlignment`; "flush" becomes a no-op
- everything else (device-local especially) → the existing copy path

This also removes the submit-time `flush_all_mapped` hack for the
allocations that matter. It cannot remove it universally — that claim was
overstated earlier.

Local verification: the two-process harness imports a region-backed
allocation exactly as the guest would — `VK_EXT_external_memory_host`
does not care that both processes are on the host. So the import path,
the alignment handling and the fallback-to-copy decision are all provable
before a VM is involved.

Caveat to measure, not assume: the guest maps IVSHMEM **write-combined**.
Writes are fast, reads are dreadful. An app that reads back through a
mapped pointer may be better off on the copy path, which is normal cached
memory. Decide per memory type, with numbers.

---

## Phase H — one window per app — M

Today the manager owns mirror windows fed by the frame ring, and the
render server creates its own xcb window per Vulkan surface. A guest app
using Vulkan would end up with both.

The `Presenter` seam exists for this. Finish it:
- `VulkanPresenter` renders into the mirror's native window
  (`SurfaceManager::create_from_xcb` already accepts a foreign window id,
  which is what makes this possible)
- `lsw_render` becomes a library the manager hosts, not a separate
  executable
- a mirror picks its pixel source; the control plane stays authoritative
  for window management either way, because focus/resize/close/input are
  Win32 concepts regardless of who draws

---

## Phase I — D3D apps — M

DXVK 2.7.1 and VKD3D-Proton 3.0.1 are already installed to
`C:\ProgramData\LSW\` by the wizard. What is missing is making an app use
them: the DLLs must sit next to the target executable (Windows DLL search
order), so this is a per-app launcher option, not a system-wide install.

**Milestone: a Direct3D application renders through DXVK → LSW ICD → host.**

---

## Potential improvement — interrupt-driven wakeups instead of polling

Not scheduled. Written down because the mechanism already exists and is
unused, and because the reasoning for *not* doing it yet is a
measurement rather than an opinion.

### What is polled today, and why

Neither direction of the command stream wakes its peer:

- **guest -> host** *does* raise an MSI-X doorbell, but QEMU delivers it
  to the eventfd held by the manager's `IvshmemServer` — not to the
  render server, which is a different process and simply polls the ring.
- **host -> guest** has no interrupt at all. The manager's ivshmem
  server holds only its own eventfds.

So a synchronous Vulkan call waits out a poll interval on *each* leg.
`ShmemFrameChannel` therefore polls on a two-tier backoff (150 us while
traffic is recent, 4 ms once idle), which is a compromise between
latency and an idle server burning a core.

### What the work would be

**guest -> host** is the tractable half. The ivshmem doorbell protocol is
multi-peer: the server hands each peer its own set of eventfds and any
peer may ring any other. The render server would connect to the same
`ivshmem_socket` the manager already serves, register as a peer, and
wait on its own eventfd instead of polling. Then `Doorbell` grows a
third implementation — `EventfdDoorbell` — alongside `SpinPollDoorbell`
and `IvshmemDoorbell`, and nothing above it changes. That is the shape
the abstraction was built for.

The guest would need to ring that peer's vector rather than peer 0's,
which is a one-line change in `Shmem::ring_channel_doorbell` plus a way
for the guest to learn the render server's peer id (a field in the
region header, or an `rpc` message).

**host -> guest** is the harder half and the one to verify before
promising. The Looking Glass ivshmem driver exposes an event-registration
IOCTL (0x805, alongside the four already used in `IvshmemRegion`) that
lets user mode hand the driver a Windows `EVENT` to signal on a given
MSI vector. If that works on the shipped driver (**unverified** — the
build in use is `DriverVer 08/03/2022`), the ICD would register an event
per channel and `WaitForSingleObject` instead of polling, and the host
would ring the guest's vector through QEMU. If it does not work, this
half is not available at all and the guest keeps polling.

### Why it is not scheduled

Measured on the real VM, guest vkcube:

    46 fps   4 ms poll cap
    116 fps  two-tier poll cap
    123 fps  after cutting 7.05 -> 3.03 blocking round trips per frame
    131 fps  the same app running natively on the host, no LSW at all

At 123 vs 131 the whole remaining gap to native is ~6%, and interrupts
would only attack part of that — the latency of three round trips per
frame that are mostly waiting on the GPU and on present pacing anyway.
The measurement to justify starting this work is not fps; it is
**frame-time variance**. If frames arrive unevenly at a good average,
the quantisation of a poll interval is the first suspect and this
becomes worth doing. Nobody has measured that yet.

## Deliberately deferred (do not build on a hunch)

- **Per-queue / per-thread rings** with submit watermarks. After Phase C
  the remaining traffic is creates/gets/submits — likely fine on one ring
  with token demux. Split only when measurement says so.
- **Lock-free MPSC ring.** Only if the write lock shows up in a profile.
- **Parallel replay on the server.** Single-threaded replay satisfies
  Vulkan's external-sync rules for free; client-side parallelism is about
  removing client stalls, not about the server going wide.
- **Deleting the socket transport.** It costs one implementation of an
  interface we want anyway, and it is the only inspectable transport —
  the thing to reach for when debugging a shared-memory race.

## Known loose ends

- Region format went v1 → v2; a running VM needs its agent redeployed.
- The ivshmem-on-payload-ISO path is tested only as far as "the answer
  file asks for it and the files are on the ISO" — the actual offline
  injection needs a real install run.
- `IvshmemDriver` can only find the driver automatically with `7z` /
  `innoextract` plus a cached Looking Glass package; otherwise
  `LSW_IVSHMEM_DRIVER_DIR`. A wizard download step would close that.
