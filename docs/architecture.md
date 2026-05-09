# Architecture

## Goals and non-goals

**Goals**
- Run Windows GPU applications on a Linux host with per-application host windows.
- Vulkan command-stream proxy with low enough overhead to be playable for non-AAA games and usable for productivity apps.
- Single, generated serialization layer driven by `vk.xml` so adding a new Vulkan version or extension is a regeneration, not a rewrite.
- A polished setup experience: download Windows, install, configure DXVK/VKD3D, register the ICD, all from a wizard.
- A manager UI that treats VMs and the apps inside them as first-class objects.

**Non-goals (initially)**
- Running multiple guest GPUs.
- Audio passthrough (deferred — PulseAudio/PipeWire shim later).
- USB / arbitrary device passthrough (deferred — handled by libvirt config later).
- Bit-exact reproduction of Windows-only Vulkan extensions that have no Linux equivalent.
- Sub-frame latency suitable for competitive esports.

## Process and host topology

```
host
├── render-server (one process, multi-threaded)
│   ├── vsock listener thread
│   ├── per-connection IO threads
│   ├── per-context replay threads
│   └── presentation thread (Wayland/X11 frame callbacks)
├── vm-manager (one process)
│   ├── UI thread (Qt or GTK)
│   ├── libvirt / QEMU control
│   └── guest-agent IPC
├── QEMU/KVM process (one per VM)
└── compositor (Wayland/X11)

guest (Windows)
├── application
│   └── client-icd.dll loaded by Vulkan loader
└── lsw-guest-agent.exe (status, app launching, ISO post-install)
```

The render-server is **shared across VMs**. One server, many clients. Each guest application opens its own vsock stream, the server treats each stream as a Vulkan instance + per-application window.

## Connection lifecycle

```
guest app starts
  │
  ▼
Vulkan loader loads client-icd.dll
  │
  ▼
client-icd connects vsock to host CID 2, port LSW_PORT
  │   ── handshake: protocol version, app name, exe path, hints (window title) ──▶
  │   ◀── handshake-ack: assigned context id, server caps ──
  │
  ▼
vkCreateInstance is the first real call; goes over the wire
  │
  ▼
vkCreateDevice → server picks the host physical device, opens a real device
  │
  ▼
swapchain / surface
  │   server creates an actual Wayland/X11 window, attaches a VkSurfaceKHR
  │   client sees a fake surface handle that maps to the real one server-side
  │
  ▼
draw loop
  │   commands streamed; vkQueuePresentKHR triggers a real present on the host window
  │
  ▼
app exit / disconnect → server tears down device, instance, window
```

## Threading model

**Server**
- One acceptor thread on the vsock listener.
- Per-connection: a dedicated **read thread** and a **replay thread**, plus a small ring buffer between them. Reading is bounded by vsock; replay is bounded by GPU. Decoupling lets reads keep draining while the GPU is busy.
- A single **presentation coordinator** for vkQueuePresentKHR side effects (compositor frame callbacks).
- Vulkan command submission can happen on the replay thread directly — the host GPU driver handles its own synchronization. Cross-thread submission rules of Vulkan are honored because each guest thread maps to its own server-side queue submission point.

**Client ICD**
- No threads of its own beyond a sender thread per Vulkan device. The application's own threads call into the ICD; the ICD serializes onto a per-device queue. A dedicated sender drains the queue onto the vsock.
- Synchronous calls (anything returning a non-void result the app reads, like `vkAllocateMemory`'s `VkResult`, `vkAcquireNextImageKHR`'s image index, queries) block on a reply.
- Asynchronous calls (most `vkCmd*` recording, `vkQueueSubmit`) are fire-and-forget — the server replays in order, errors come back out-of-band.

## Handle remapping

Vulkan handles are 64-bit opaque. A `VkBuffer` value the client sees is generated server-side (real driver-issued) and tunneled through the protocol; the client just stores and echoes it. This avoids a translation table entirely for the common path.

Exceptions (must be remapped):
- `VkSurfaceKHR` — the client's "surface" is a synthetic value because no Win32 surface call should reach the host driver; the server creates a real `VkSurfaceKHR` from a Wayland/X11 surface and stashes the mapping.
- `HANDLE`-typed external memory / external semaphore — Windows OS handles are meaningless on Linux. Either disable export/import extensions on the client or implement a guest-host shared-memory shim.
- `HWND` for `vkCreateWin32SurfaceKHR` — never actually creates a Win32 surface on the host; the ICD intercepts and turns it into "create a window on the host with these hints."

## Memory: the hard part

Vulkan apps allocate device memory and fill it with buffer/image data. Naively shipping every byte is the death of performance.

Strategy:
1. **Guest-host shared memory region**: virtio-mem / virtio-shmem mapped into both the QEMU guest physical address space and the host process. Allocations the guest sees as host-visible memory are backed by this region; writes are visible to the host without copy.
2. **Staging buffers** for non-shared paths: when memory must be copied (e.g., upload to non-host-visible device-local memory), the client streams a write list and the server replays it with `vkCmdCopyBuffer` from a staging buffer the server owns.
3. **Coherency**: `vkFlushMappedMemoryRanges` explicitly turns into a "page X..Y is dirty, replay before next submit" message. Apps that rely on coherent memory need an extra fence we synthesize.

Phase 1 implementation may skip shared memory entirely and use bulk-copy with `vkCmdUpdateBuffer` / staging. It will be slow but correct.

See [protocol.md](protocol.md) for the on-the-wire layout.

## Presentation

`vkQueuePresentKHR` on the client → server submits the present on its real swapchain attached to the host window. Frame pacing is the host compositor's job. Server-side `VK_KHR_present_wait` and `VK_EXT_present_timing` (when available) feed back acquire/present timestamps so the guest's frame pacing logic doesn't go sideways.

Swapchain image acquisition is synchronous from the client's perspective: `vkAcquireNextImageKHR` blocks for a server reply with the image index. Hide this latency by allowing the client to issue an "early acquire hint" right after present, so the next acquire is already pipelined.

## Shader and pipeline data

SPIR-V binaries are part of pipeline create info — they get serialized as length-prefixed byte blobs. The codegen handles this via the `<len>` attributes in `vk.xml`. Pipeline cache blobs (`vkGetPipelineCacheData`) are similarly opaque blobs and can be cached on the host filesystem keyed by guest-supplied app id, so warm starts are fast.

## Failure modes

| Failure | What happens |
|---|---|
| Vsock connection drops mid-frame | Server tears down the context; client sees `VK_ERROR_DEVICE_LOST` on the next call |
| Server-side Vulkan returns an error | Result piggybacks on the next sync reply (or out-of-band channel for async-only calls) |
| Out-of-order command stream | Hard error — kill the connection, the client is buggy or we have a bug |
| VM crashes | libvirt notices; manager UI shows the VM as stopped |
| Host GPU lost | All connections invalidated; manager UI reports it |

## Build-time data flow

```
Khronos vk.xml          serialization-codegen          generated/
  ────────────────────▶  (parses, emits)        ────▶   ├── vk_serialize.cpp
                                                        ├── vk_deserialize.cpp
                                                        ├── vk_dispatch_client.cpp
                                                        ├── vk_dispatch_server.cpp
                                                        └── vk_protocol_ids.h

generated/ is consumed by both render-server and client-icd at compile time.
```

A pinned `vk.xml` lives in `third_party/Vulkan-Headers/`. Bumping Vulkan = bump submodule + regenerate.

## Security boundary

The render-server runs at the user's privilege level on the host and has full host-GPU access. A malicious guest can attempt:
- Driver fuzzing through malformed Vulkan calls — unavoidable, mitigated by validation layer in debug builds and SPIR-V validation on shader uploads in release.
- Resource exhaustion — server enforces per-context budgets (memory, descriptor sets, command buffers).
- Reading host memory — the protocol only exposes what the guest already owns. The shared-memory region is per-VM.

The render-server should not run as root. It should drop any unneeded capabilities on startup.
