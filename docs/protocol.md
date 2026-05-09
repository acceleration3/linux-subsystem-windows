# Wire Protocol

The on-the-wire format spoken between the client ICD and the render-server. Designed for:
- Cheap framing (one length-prefixed read per call).
- Stable command IDs so a newer client can talk to an older server (and vice versa) within the same major version.
- Out-of-band channels for replies, async errors, and bulk memory transfer without head-of-line blocking on the command stream.

## Layered model

```
┌─────────────────────────────────────────────────┐
│  Vulkan calls (generated dispatch)              │  call layer
├─────────────────────────────────────────────────┤
│  Frames (cmd_id, args, optional reply token)    │  frame layer
├─────────────────────────────────────────────────┤
│  Streams (cmd, reply, bulk, control)            │  stream layer
├─────────────────────────────────────────────────┤
│  AF_VSOCK (HV_SOCKET on Windows guest)          │  transport
└─────────────────────────────────────────────────┘
```

Multiple streams over a single connection. We avoid TCP-style multiplexing complexity by opening **one vsock connection per stream**. Vsock connection setup is cheap (no TLS, no DNS); a Vulkan instance opens 3-4 connections at startup and that's it.

| Stream | Direction | Carries |
|---|---|---|
| **control** | bidirectional | handshake, cap negotiation, shutdown, async errors |
| **cmd** | client→server | command stream (vkXxx calls) |
| **reply** | server→client | replies to sync calls, demuxed by token |
| **bulk** | bidirectional | large memory transfers (memory flush/invalidate) |

A v1 simplification: collapse `cmd` and `reply` into a single full-duplex stream. The complication is that a long-running async chunk (e.g., a giant `vkCmdUpdateBuffer`) can stall replies behind it. We start with one stream and split if measurements demand.

## Connection setup

```
client                                       server
─────                                        ──────
WSAConnect AF_HYPERV / SOCKET_HV_PARENT  ──▶
                                         ◀── accept

  HELLO frame:
    magic = 0x4C5357 ('LSW')
    proto_major = 1
    proto_minor = 0
    client_role = "icd" | "manager" | "agent"
    app_name (string)
    app_exe_path (string)
    pid (u32)
                                         ──▶
                                         ◀── HELLO_ACK frame:
                                                proto_major = 1
                                                proto_minor (negotiated)
                                                context_id (u64, opaque)
                                                server_caps (extension list,
                                                             feature flags,
                                                             physical-device count)
                                                shared_mem (offset, size, fd-token) | absent

(further frames are command frames over the same socket)
```

The context_id is just for human-readable logs — frames don't carry it.

## Frame layout

All integers little-endian. `varint`s use LEB128 unsigned encoding for sizes.

```
struct Frame {
    u32  cmd_id;                 // see vk_protocol_ids.h
    u32  payload_len;            // bytes following
    u8   flags;                  //   bit 0: NEEDS_REPLY
                                 //   bit 1: HAS_BULK_REF
                                 //   bit 2: TRACE
                                 //   bit 3..7: reserved (must be 0)
    u8   reserved[3];
    u64  reply_token;            // present iff NEEDS_REPLY; else 0
    u8   payload[payload_len];
}
```

12 bytes header + 8 bytes optional token + payload.

A reply frame:

```
struct Reply {
    u32  cmd_id;                 // mirrors the request, for sanity
    u32  payload_len;
    u8   flags;                  //   bit 0: ERROR (server-side dispatch error)
                                 //   bit 1: TRUNCATED (reply too big, see bulk)
    u8   reserved[3];
    u64  reply_token;
    i32  vk_result;              // VkResult or LSW-internal error
    u8   payload[payload_len - 4];
}
```

Reply frames are matched to requests by `reply_token`. Tokens are monotonic per stream; the client owns the namespace.

## Payload encoding

The codegen emits two functions per type:

```cpp
size_t serialize(Buffer& out, const VkBufferCreateInfo& v);
size_t deserialize(BufferReader& in, VkBufferCreateInfo* v);
```

POD types are written with their natural alignment, no length prefix (caller knows the type). Variable-length data is length-prefixed with a varint.

Pointers are encoded as one of:

| Tag | Meaning |
|---|---|
| `0x00` | NULL |
| `0x01` | Inline payload follows (length-prefixed) |
| `0x02` | Reference into the bulk channel (offset + length) |
| `0x03` | Reference into shared memory (offset, only if shared mem is enabled) |

`pNext` chains:

```
[ varint count ]
  [ uint32 sType ]  [ struct payload ]   ← repeated `count` times
```

`sType` is the discriminator; the deserializer dispatches by it.

## Bulk channel

For payloads larger than a threshold (default 64 KiB), the client sends:

```
cmd frame (cmd_id, args with HAS_BULK_REF, bulk metadata: { offset, length, sha256 hint })
```

then writes the actual bytes onto the bulk stream. The server reads from bulk, hashes optionally, applies. Order is preserved between cmd and bulk: the cmd frame says "bulk slot N follows on the bulk stream"; the server pulls slot N before applying the cmd.

For memory uploads, an even tighter path:

```
client → server (cmd):  vkFlushMappedMemoryRanges, ranges = [(memory, offset, size)]
client → server (bulk): raw bytes for each range, in order
server: deserialize, look up memory by handle, copy bytes (or vkCmdUpdateBuffer)
```

## Async errors

Most async commands cannot deliver a `VkResult` synchronously. If the server fails (e.g., `vkQueueSubmit` returns `VK_ERROR_DEVICE_LOST`), it sends an **async-error frame** on the control stream:

```
struct AsyncError {
    u32  cmd_id;
    u64  approximate_seq;        // counter of all cmd frames sent on cmd stream
    i32  vk_result;
    u32  message_len;
    char message[];
}
```

The client surfaces this on the next sync call's return, or on the next `vkDeviceWaitIdle`, conforming to Vulkan's "device-lost is sticky" semantics.

## Cap negotiation

The server's `HELLO_ACK` includes its capability set:

```
struct ServerCaps {
    char     vk_version[16];        // e.g., "1.3.290"
    u32      enabled_extensions_count;
    string[] enabled_extensions;    // names
    u64      features_v10;          // packed VkPhysicalDeviceFeatures
    u8       features_extra[...];   // VkPhysicalDeviceFeatures2 chain
    u32      max_inflight_cmds;
    u32      max_payload;           // single frame ceiling
    u8       has_shared_memory;
    u8       has_present_timing;
    u8       has_external_memfd;
}
```

The client filters its requested instance/device extensions against this list before exposing them to the application via `vkEnumerateInstanceExtensionProperties`.

## Protocol versioning

`proto_major.proto_minor`:
- **major** bump = breaking change. Older client refuses to talk to newer server with different major.
- **minor** bump = additive (new commands, new fields appended via `pNext`-style optional payload).

Command IDs are stable. Removed commands keep their slot reserved (see [serialization-codegen.md](serialization-codegen.md) on the curated `protocol_ids.toml`).

When a server sees an unknown `cmd_id`, it replies with an error reply (if NEEDS_REPLY) or pushes an async error and disconnects.

## Shutdown

Either side can initiate. Sender writes a `BYE` frame on the control stream (`cmd_id = 0xFFFFFFFF`) with reason text and closes its half. The receiver drains, replies if expected, closes its half. The Vulkan instance the connection backs is destroyed.

A timeout (default 30 s) fires if no progress; the server tears down regardless. The client interprets the disconnect as device-lost.

## Trace mode

When `flags & TRACE`, both sides emit structured log lines:

```
[ts][stream=cmd][cmd=vkCmdDraw][len=12][token=42] payload_hash=ab12...
```

A `--record path.lswstream` mode on the server captures every frame; replay-only mode lets us reproduce a guest's behavior without the guest. Indispensable for debugging.

## Worked example: vkCreateBuffer

Client:

```
cmd_id      = ID_vkCreateBuffer
flags       = NEEDS_REPLY
reply_token = 17
payload =
  device:     u64 handle
  pCreateInfo:
    sType:                  VkStructureType
    pNext_count:            varint(0)
    flags:                  VkBufferCreateFlags
    size:                   VkDeviceSize
    usage:                  VkBufferUsageFlags
    sharingMode:            VkSharingMode
    queueFamilyIndexCount:  u32 = N
    pQueueFamilyIndices:    [u32 × N]
  pAllocator:               u8 tag = NULL
```

Server replies:

```
reply_token = 17
vk_result   = VK_SUCCESS
payload =
  pBuffer:    u64 (server-issued handle)
```

Client copies the handle into the application's `*pBuffer` and returns `VK_SUCCESS`.

## Worked example: vkCmdDraw (async)

Client:

```
cmd_id      = ID_vkCmdDraw
flags       = 0           # async; no reply
payload =
  commandBuffer:  u64
  vertexCount:    u32
  instanceCount:  u32
  firstVertex:    u32
  firstInstance:  u32
```

No reply. The server appends to the relevant command buffer.
