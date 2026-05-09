# Render Server

Linux-host daemon that accepts vsock connections from guest applications and replays Vulkan command streams against the host GPU, drawing each application into its own host window.

## Responsibilities

1. Listen on `AF_VSOCK` for new guest application connections.
2. For each connection: own a Vulkan instance + device + a host-native window + a swapchain.
3. Decode wire frames using the generated deserializer; replay against the real Vulkan driver.
4. Maintain handle remap tables for the few handle types that need it (surfaces, externals).
5. Service synchronous reply requests (returns `VkResult`, `VkBool32`, query data, etc.).
6. Manage memory transfers — staging buffer pool, optional shared-memory window, dirty-range tracking.
7. Forward presentation timing back to the client.
8. Tear down cleanly on disconnect, VM shutdown, or `vkDeviceWaitIdle` on exit.

## Process layout

Single process, multi-threaded. No fork-per-connection — Vulkan instances are heavy and we want to share `VkPhysicalDevice` across contexts.

```
render-server
├── main thread          configuration, signal handling, lifecycle
├── acceptor thread      vsock listener, hands off accepted FDs
├── per-connection
│   ├── reader thread    drains vsock, parses frames, enqueues to ring
│   ├── replay thread    consumes ring, replays Vulkan, owns the VkDevice
│   └── (optional) sender thread — for async reply traffic
└── present coordinator  Wayland/X11 frame callbacks, fps stats
```

## Module breakdown

```
src/server/
├── main.cpp                      # bootstrap, signal handling, drop privileges
├── config.{h,cpp}                # CLI flags, config file (XDG)
├── server.{h,cpp}                # acceptor, connection table
├── connection.{h,cpp}            # per-connection state, threads, lifecycle
├── replay.{h,cpp}                # command stream replay loop
├── handle_table.{h,cpp}          # client-id → server VkHandle remap (only for remapped types)
├── memory_manager.{h,cpp}        # staging pool, dirty range tracker, shared-mem window
├── window/
│   ├── window.h                  # interface
│   ├── window_wayland.{h,cpp}    # Wayland implementation (xdg-shell)
│   └── window_x11.{h,cpp}        # X11 fallback (Xlib or xcb)
├── presentation.{h,cpp}          # swapchain wrap, present timing feedback
├── vulkan/
│   ├── instance.{h,cpp}          # one VkInstance per connection (or shared, see below)
│   ├── device.{h,cpp}            # device creation, queue management
│   └── caps.{h,cpp}              # negotiating extensions/features with the client
├── reply.{h,cpp}                 # response writer (ack, sync return values)
└── log.{h,cpp}
```

Generated files (from `vk.xml` codegen) are linked in:

```
generated/
├── vk_deserialize.cpp            # bytes → typed call args
├── vk_dispatch_server.cpp        # typed call args → vkXxx(...) on real driver
└── vk_protocol_ids.h
```

## VkInstance: one or many?

Two viable models:
- **Per-connection instance**: simpler, isolation. Each VM/app gets its own `VkInstance`. Wastes some memory.
- **Shared instance, per-connection device**: less overhead, but extension enablement becomes the union of what any client needs, and validation layers behave oddly.

Default to **per-connection instance** for simplicity in v1; optimize later if instance creation cost shows up in startup profiles.

## Window creation timing

The server cannot create the window when the connection arrives — it doesn't yet know if this is a Vulkan app at all (it might handshake, send `vkCreateInstance`, then exit). Create the window lazily on the first `vkCreate{Win32,Xlib,Wayland,Xcb}SurfaceKHR` from the client. The client always uses the Win32 variant; the server interprets it as "create a window with the hints I provided in the handshake."

Window hints sent in handshake:
- App name (used for window title and `WM_CLASS` / app-id).
- Initial size hint (from the guest's swapchain sizing if known, else 1280x720 default).
- Icon path (if the guest agent extracts one).
- DPI scaling preference.

## Replay loop

```cpp
while (!stop_requested) {
    Frame f = ring.pop();           // blocks
    auto cmd_id = f.cmd_id;
    auto& fn = dispatch_table[cmd_id];   // generated
    fn(state, f.payload, f.payload_len);  // generated; calls real Vulkan
    if (f.flags & FRAME_NEEDS_REPLY) {
        reply.send(f.reply_token, last_result);
    }
}
```

Critical invariants:
- Order is preserved per-context. Vulkan ordering rules apply across queue submissions; recording is single-threaded per command buffer; we never reorder.
- Replay can call `vkQueueSubmit` and friends without blocking the reader, because the GPU operates on submitted work asynchronously.

## Memory management

See `memory_manager`. Three regimes:

1. **Pre-shared-memory (Phase 1)**: no shared memory. Every `vkMapMemory` returns a guest-side staging buffer in the ICD; on `vkUnmapMemory` or `vkFlushMappedMemoryRanges`, the dirty bytes are sent over the wire and `vkCmdUpdateBuffer` (or staged copy) applies them server-side.
2. **virtio-shmem (Phase 2)**: a host-mapped shared region; guest's host-visible allocations are sub-allocations within it. Server `mmap`s the same region, no copies on flush.
3. **Hybrid**: device-local memory always uses staging; host-visible uses shared mem.

Tracking dirty ranges in Phase 1: the ICD can either bracket every write (slow) or assume the entire mapped range is dirty on flush (overshoots). Default to "the range the app passed to `vkFlushMappedMemoryRanges` is the dirty range; for `VK_WHOLE_SIZE`, ship the whole allocation."

## Surface and swapchain

```
client                          server
─────────                       ─────────
vkCreateWin32SurfaceKHR    ──▶  create real Wayland/X11 window
                                create real VkSurfaceKHR from it
                                store surface_id ↔ VkSurfaceKHR mapping
                           ◀──  return surface_id (synthetic)

vkCreateSwapchainKHR       ──▶  resolve surface_id; create real swapchain
                           ◀──  swapchain handle

vkAcquireNextImageKHR      ──▶  call host-side; wait for sem/fence
                           ◀──  image index, result code

vkQueuePresentKHR          ──▶  present on host swapchain
                                trigger frame callback
                           ◀──  present id, optional timing
```

Swapchain images are not transferred — the guest only ever sees image handles. All rendering targets server-side memory.

## Extensions: enabling, disabling, faking

The handshake includes the list of extensions the guest device wants. The server replies with the subset it can support. Some are always disabled because they make no cross-machine sense:

- `VK_KHR_external_memory_win32` (handles) — disable, or shim via vsock-side memfd.
- `VK_KHR_external_semaphore_win32` — same.
- `VK_KHR_swapchain` from `VK_KHR_win32_surface` — silently translate to host swapchain.
- `VK_EXT_full_screen_exclusive` — make a no-op or fullscreen the host window via xdg-shell.

Some are faked even if the host doesn't have them:
- `VK_KHR_get_physical_device_properties2` — universally available now, fine.
- DXVK's required feature set (timeline semaphores, descriptor indexing, etc.) must be exposed because both DXVK and VKD3D-Proton hard-require them.

## Configuration

CLI flags + config file at `$XDG_CONFIG_HOME/lsw/render-server.toml`:

```toml
[listen]
cid = -1            # -1 = VMADDR_CID_ANY
port = 5101

[gpu]
device = "default"  # or PCI bus ID
power_pref = "high" # for hybrid GPU systems

[window]
backend = "auto"    # "wayland" | "x11" | "auto"
default_size = [1280, 720]

[memory]
shared_region_mb = 4096   # 0 = disabled (Phase 1)
staging_pool_mb = 256

[debug]
validation = false
trace_calls = false
```

## Logging and tracing

- Per-connection log files (`/tmp/lsw/<vm>-<app>.log`) with structured records.
- Optional Tracy / perfetto / systemtrace integration on the replay thread.
- A `--trace-calls` mode that emits every replayed call with payload size.

## Run as

A user-level systemd-user service is the right deployment target:

```
~/.config/systemd/user/lsw-render.service
```

Not a system service. The renderer needs the user's display session.

## Open questions

- Does the host's compositor support enough of present-timing that we can give the guest accurate vblank info? If not, ship our own pacing heuristic.
- Multi-GPU host: pin the server to one GPU per VM, or expose all? Default to one (configurable per VM).
- HDR / wide gamut: defer until v2.
