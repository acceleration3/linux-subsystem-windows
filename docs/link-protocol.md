# The host↔guest link

Normative description of everything the Linux manager and the Windows
guest agent must agree on. The code that implements it is
`src/linkproto/` — a header-only module compiled by **both** toolchains,
so this document and that module are the two places the contract exists,
and nowhere else. Where they disagree, the headers win and this file is
the bug.

An application's windows and its GPU work do not travel here. Windows
are carried by [remotewin](../external/remotewin), one connection per
application; GPU calls by [render-server](../external/render-server),
over a shared-memory region of its own. What is left is the control
plane: asking the agent to do things, and hearing what it has to say.

## Transport

A single TCP connection to a guest-side listener on port **5102**
(`linkproto::kAgentTcpPort`), reached from the Linux host through QEMU
user-mode networking's `hostfwd` rule in `src/host/embedded/domain.xml`.

AF_VSOCK would be the natural choice and was tried; host kernels in this
class have no working `vhost_vsock`, so TCP-over-hostfwd is the reliable
channel. Nothing above the transport depends on which it is.

## Envelope framing

    [type:u8][len:u32 big-endian][payload:len bytes]

Length prefixing (rather than newline delimiting) is what lets a payload
contain anything without escaping.

| type | name               | payload                                     |
|-----:|--------------------|---------------------------------------------|
| 1    | `Control`          | one JSON object, UTF-8, no trailing newline |
| 2    | `RetiredFrame`     | retired — was window pixels                 |
| 3    | `RetiredFrameGone` | retired — was "this window's slot is dead"  |

The retired ids stay burnt so an old peer's frames are never mistaken
for something else. Big-endian because that is still the network byte
order convention and it keeps packet dumps readable.

Implementation: `lsw/linkproto/envelope.hpp`, `byte_order.hpp`, and
`frame_reader.hpp` (the stream reassembler both ends run).

## Control messages

    Request  {"id": N, "cmd": "<command>", "params": { ... }}
    Reply    {"id": N, "result": { ... }}
             {"id": N, "error":  "<message>"}
    Event    {"event": "<topic>", ...payload}

Ids are sender-allocated per direction. Concurrent in-flight requests are
fine; replies route back by id. The manager gives up on a request after
`AgentRpcClient::kDefaultCallTimeout` and discards a late reply.

The agent defines every command; the manager sends them. There are no
agent-initiated requests today — both sides drop what they do not expect.

### Failure

A request fails in one of two ways, and a peer must treat both as
failure:

* an `error` reply — the command is unknown, or its handler threw;
* a `result` that says so itself, with `"ok": false` and/or a non-empty
  `"error"` string. A handler answers this way when the request was
  well-formed but the guest refused it (an executable that would not
  start, a folder Windows would not open), and keeps any other fields it
  has — a refused `list_dir` still names its `path`.

On the manager both forms, and every transport failure, arrive as one
`link::AgentReply` with `ok == false` and a sentence in `error`.

### Commands

The vocabulary lives in `lsw/linkproto/commands.hpp` as constants that
both sides name, so a rename is a compile error rather than a runtime
`unknown command`. Grouped as: system, installed apps, browsing the
guest, display, the window mirror, windows, install/provisioning,
console, UI Automation.

Each command is one agent-side handler TU under
`src/agent/src/rpc/handlers/<group>/<name>.cpp`, self-registering via
`LSW_REGISTER_RPC`. The transport does one registry lookup and knows no
command names at all — including for logging, which asks the handler
(`RpcHandler::silent`, `RpcHandler::summarize`).

### Events

Agent-pushed, one-way, delivered on the connection's tick. Topics are in
the same header (`linkproto::topic`): agent logs, install-job progress
and completion, console output and exit, the shared-memory channel's
`channel_hello`, and the UI Automation notifications.

Agent-side, every event source implements `rpc::EventSource` and is
registered with the connection's `EventPump`; adding a topic touches no
loop. A source may implement `undeliverable` to have events survive a
connection that dies mid-drain — the installer does, because the wizard
blocks on `setup_done`.

Manager-side, `AgentRpcClient::subscribe` returns an `EventSubscription`
that unregisters its handler when destroyed, waiting out a call already
in progress on the reader thread.

## The shared region (IVSHMEM)

The manager creates a POSIX shm region and hands the fd to QEMU; the
guest maps the same memory through the IVSHMEM PCI BAR. It is divided
into **channels**, described by a table at the front
(`lsw/linkproto/region.hpp`, region format version 3):

    0x0000  region header    magic 'LSWF', version, channel count,
                             entry size, table offset, region size
    0x0040  channel table    up to 8 x ChannelDescriptor {kind, flags,
                             offset, size}
    0x1000  first channel
            ...

Channels are located **by kind, never by offset** — the manager owns the
layout and may move or resize anything by rewriting one table entry. A
kind the guest does not know about costs it nothing. Contents are
addressed relative to the channel's own base.

The layout is a pure function of the region size
(`lsw/linkproto/region_layout.hpp`, `plan_region` / `lay_out_region`),
so the manager, the local loopback and the parity test all derive the
same table instead of three agreeing copies of it.

One channel exists today: `rpc` (kind 2). Kinds 1, 3 and 4 were the
frame ring, the Vulkan command stream and its memory arena; they are
retired, not reused, because the values are a wire contract.

The magic has never changed. A peer checks the version, so one that
knows an older format refuses a newer region cleanly instead of reading
the table as whatever it used to be.

### Doorbells

The guest wakes the manager with an IVSHMEM MSI-X doorbell, one vector
per channel: `rpc` is vector 0. `<msi vectors='N'/>` in domain.xml
mirrors `linkproto::kDoorbellVectorCount`; a device that exposes more is
a VM defined by an older manager, and the extra vectors go unused.

There is no interrupt in the other direction — the manager's in-process
ivshmem server holds only its own eventfds — so host→guest messages are
picked up on the agent's existing tick rather than on a wakeup. A
manager that attached to a VM it did not start has no doorbells either,
and sweeps the ring on a timer instead.

### The rpc channel

Duplex: two independent single-producer/single-consumer message rings
back to back, guest→host in the first half, host→guest in the second
(`lsw/linkproto/byte_ring.hpp`).

A ring is a 64-byte control block — monotonic `write_pos` and `read_pos`,
a power-of-two `capacity`, a `dropped` counter — followed by the data
area. `write_pos` is written only by the producer and `read_pos` only by
the consumer, which is what makes the ring lock-free without a
compare-and-swap. Records are `[len:u32][payload]` in native byte order
(this is memory, not a network protocol), and writes are all-or-nothing:
a message that does not fit is refused whole, so a reader never sees half
a record. A record claiming more than was published means the two ends
have lost sync; the consumer discards what is outstanding and resumes
from the producer's position rather than wedging forever.

The guest maps the BAR write-combined, which is weakly ordered in both
directions, so every publish and consume goes through the explicit
fences in `region_fence.hpp` rather than relying on
`std::atomic_thread_fence`, which compiles to nothing on x86.

The channel carries exactly the control messages described above — same
requests, replies and events as the socket, so the two transports are
interchangeable at the message level. The agent announces the channel
with a `channel_hello` event as soon as it maps the region; requests
arriving on the ring are dispatched by the same registry and answered on
the ring they came from. The socket remains the primary path: moving
traffic onto shared memory is a policy change (`$LSW_RPC_OVER_SHMEM=1`
on the manager), not a protocol one.
