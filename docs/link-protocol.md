# The host↔guest link

Normative description of everything the Linux manager and the Windows
guest agent must agree on. The code that implements it is
`src/linkproto/` — a header-only module compiled by **both** toolchains,
so this document and that module are the two places the contract exists,
and nowhere else.

Previously each side carried its own copy of the framing constants, its
own big-endian helpers and its own list of shared-memory byte offsets.
They had already drifted: the agent knew about a slot field the manager
never read, the manager wrote two header fields the agent never parsed.

## Transport

A single TCP connection, guest-side listener on port **5102**, reached
from the Linux host through QEMU user-mode networking's `hostfwd` rule.

AF_VSOCK would be the natural choice and was tried; host kernels in this
class have no working `vhost_vsock`, so TCP-over-hostfwd is the reliable
channel. Nothing above the transport depends on which it is.

Window pixels do **not** travel on this connection — see *Frame ring*
below.

## Envelope framing

    [type:u8][len:u32 big-endian][payload:len bytes]

Length prefixing (rather than newline delimiting) is what lets binary
payloads share the connection with JSON without escaping.

| type | name         | payload                                      |
|-----:|--------------|----------------------------------------------|
| 1    | `Control`    | one JSON object, UTF-8, no trailing newline  |
| 2    | *retired*    | was PNG window frames; id stays burnt        |
| 3    | `FrameGone`  | `[hwnd:u64-BE]`                              |

Big-endian because that is still the network byte order convention and
it keeps packet dumps readable.

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

### Commands

The vocabulary lives in `lsw/linkproto/commands.hpp` as constants that
both sides name, so a rename is a compile error rather than a runtime
`unknown command`. Grouped as: system, apps, windows, input, capture,
install, console, UI Automation.

Each command is one agent-side handler TU under
`src/agent/src/rpc/handlers/<group>/<name>.cpp`, self-registering via
`LSW_REGISTER_RPC`. The transport does one registry lookup and knows no
command names at all — including for logging, which asks the handler
(`RpcHandler::silent`, `RpcHandler::summarize`).

### Events

Agent-pushed, one-way, delivered on the connection's tick. Topics are in
the same header (`linkproto::topic`): agent logs, install-job progress
and completion, console output and exit, caption-drag notification, and
the UI Automation lifecycle/focus/state/title/modal notifications.

Agent-side, every event source implements `rpc::EventSource` and is
registered with the connection's `EventPump`; adding a topic touches no
loop. A source may implement `undeliverable` to have events survive a
connection that dies mid-drain — the installer does, because the wizard
blocks on `setup_done`.

## The shared region (IVSHMEM)

The manager creates a POSIX shm region and hands the fd to QEMU; the
guest maps the same memory through the IVSHMEM PCI BAR. It is divided
into **channels**, described by a table at the front
(`lsw/linkproto/region.hpp`, region format version 2):

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

Three channels exist today: `frames`, `rpc`, `cmdstream`. The frame
channel is placed last so it absorbs whatever is left of the region;
pixel demand scales with what the user opens, message rings do not.

### Doorbells

The guest wakes the manager with an IVSHMEM MSI-X doorbell. Vectors
`0..63` are frame slots (vector == slot index); the channels take the
vectors past the end, so a ring notification is never mistaken for a
frame. `<msi vectors='66'/>` in domain.xml mirrors
`linkproto::kDoorbellVectorCount`.

There is no interrupt in the other direction — the manager's in-process
ivshmem server holds only its own eventfds — so host→guest messages are
picked up on the agent's existing tick rather than on a wakeup.

### frames channel

Window pixels, the region's original and still largest tenant. Only the
`FrameGone` envelope travels on the socket, so the manager can
invalidate a slot for a window that died between ticks.

Layout, in `lsw/linkproto/frame_ring.hpp`, every offset `static_assert`ed:

    +0x0000  channel header  magic, version, slot count, slot size,
                             table offset, buffers offset/size
    +0x0100  slot table      64 slots x 64 bytes
    +0x1100  buffer area     per-slot front/back buffer pairs

Each slot carries its hwnd, state, pixel format (BGRA8 premultiplied),
geometry, its buffer pair's offset and total size, a seqno and the
agent's QPC timestamp for the frame.

### Seqlock

`seqno` carries two flags in its low bits — bit 0 set while a write is in
progress, bit 1 selecting which half of the buffer pair is readable — and
a counter above them that advances by 4 per commit.

Producer: compute the next commit value (counter advanced, half flipped),
publish it with bit 0 set, write pixels into the now-unused half, publish
it again with bit 0 clear, ring the doorbell.

Consumer: read the seqno, retry if bit 0 is set, copy from the half bit 1
selects, re-read the seqno and retry if it changed. Bounded retries, so a
wedged producer cannot pin the manager's thread.

The arithmetic is `seqno_next_commit` / `seqno_begin_write` /
`half_offset` in the shared header — neither peer reimplements it.

### rpc and cmdstream channels

Both are duplex: two independent single-producer/single-consumer message
rings back to back, guest→host in the first half, host→guest in the
second (`lsw/linkproto/byte_ring.hpp`).

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

The **rpc** channel carries exactly the control messages described above —
same requests, replies and events as the socket, so the two transports
are interchangeable at the message level. The agent announces the channel
with a `channel_hello` event as soon as it maps the region; requests
arriving on the ring are dispatched by the same registry and answered on
the ring they came from. The socket remains the primary path: moving
traffic onto shared memory is a policy change, not a protocol one.

The **cmdstream** channel is reserved for the Vulkan command stream. It
is created and mapped by both sides but has no producer yet — that
arrives with a Windows build of the ICD.
