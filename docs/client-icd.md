# Client ICD (Windows guest)

A Vulkan Installable Client Driver (ICD) DLL that registers itself with the Windows Vulkan loader, intercepts every Vulkan call from the application, serializes it, and ships it over `AF_VSOCK` to the host's render-server.

## What an ICD is

The Vulkan loader on Windows discovers ICDs from registry keys under `HKLM\SOFTWARE\Khronos\Vulkan\Drivers\<json-path>`. Each JSON points at a DLL that exports `vk_icdGetInstanceProcAddr` (and a few more entry points per the LunarG ICD interface). The loader fans out instance/device calls across all installed ICDs. Each ICD reports physical devices; the application picks one.

Our ICD reports **one physical device** (the host GPU as seen by the server) per active connection. It does so even though no real GPU exists in the guest from this DLL's perspective — every property comes from the server.

## Responsibilities

1. Implement the ICD ABI: `vk_icdGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion`, etc.
2. Handle `VK_EXT_debug_utils` and other instance-level meta calls locally where reasonable.
3. Connect to the host vsock on first instance creation; one connection per Vulkan instance.
4. Serialize every dispatched call using the generated client dispatch tables.
5. Block on a reply for sync calls, fire-and-forget for async.
6. Manage a guest-side **mapped memory shadow** so `vkMapMemory` returns a usable pointer.
7. Translate Win32 surfaces into "open a host window with these hints" requests.
8. Forward presentation results back to the application.
9. Tear down cleanly when the application destroys its instance, or on process exit.

## Registry installation

```
HKEY_LOCAL_MACHINE\SOFTWARE\Khronos\Vulkan\Drivers
  Value: C:\Program Files\LSW\lsw_icd.json   (DWORD = 0)
```

`lsw_icd.json`:

```json
{
  "file_format_version": "1.0.1",
  "ICD": {
    "library_path": "C:\\Program Files\\LSW\\lsw_icd.dll",
    "api_version": "1.3.290"
  }
}
```

For 32-bit applications we also need a 32-bit ICD listed under the `WOW6432Node` registry path, with a 32-bit DLL build. Both DLLs share the connection multiplexing (or not — simpler to keep them independent processes, since each app is its own connection anyway).

## Module layout

```
src/client/
├── icd_entry.cpp            # vk_icdGetInstanceProcAddr, ICD negotiation, exports
├── instance.{h,cpp}         # VkInstance lifetime, vsock connect on first create
├── device.{h,cpp}           # VkDevice creation, per-device sender thread
├── dispatch.{h,cpp}         # function-pointer tables (generated entries plug in here)
├── transport/
│   ├── vsock.{h,cpp}        # AF_VSOCK on Windows (uses HV_SOCKET via WSA)
│   └── framing.{h,cpp}      # write_frame / read_reply
├── handle_table.{h,cpp}     # remap for surfaces, externals
├── memory_shadow.{h,cpp}    # vkMapMemory backing buffers, dirty tracking
├── surface_win32.{h,cpp}    # vkCreateWin32SurfaceKHR → host window request
├── present.{h,cpp}          # vkQueuePresentKHR / vkAcquireNextImageKHR plumbing
├── handshake.{h,cpp}        # protocol version, app metadata, caps negotiation
└── log.{h,cpp}              # log to %LOCALAPPDATA%\LSW\client.log

generated/
├── vk_serialize.cpp         # arg-list → bytes
├── vk_dispatch_client.cpp   # vkXxx wrappers that call serialize then transport
└── vk_protocol_ids.h
```

## Vsock on Windows

Windows exposes vsock-equivalent transport as **Hyper-V Sockets** (HV_SOCKET) via the WSA family `AF_HYPERV`. The QEMU `vhost-vsock-pci` device, when running with WHPX/HVCI off and with a Linux host, presents to the Windows guest as a vsock-capable transport. There are two practical paths:

1. **Pure HV_SOCKET**: `socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW)`, set `SOCKADDR_HV` with `VmId = HV_GUID_PARENT` and a service GUID. Works on Windows 10+ with Hyper-V components installed.
2. **vsock through virtio-vsock driver**: requires a third-party Windows virtio-vsock driver; not all builds have one. Less portable than HV_SOCKET for now.

Pick HV_SOCKET as primary, document the VM config that makes it work (`-device vhost-vsock-pci,guest-cid=<N>` plus the appropriate guest-side service GUID registration). On the Linux side, the render-server still uses ordinary `AF_VSOCK`; QEMU's vhost-vsock bridges between them.

The connect call:

```c
SOCKET s = WSASocketW(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW, NULL, 0, WSA_FLAG_OVERLAPPED);
SOCKADDR_HV addr = {0};
addr.Family = AF_HYPERV;
addr.VmId = HV_GUID_PARENT;        // talk to the host
addr.ServiceId = LSW_SERVICE_GUID; // pre-registered
WSAConnect(s, (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL);
```

The service GUID is registered via the `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Virtualization\GuestCommunicationServices` key by the installer.

## Per-instance and per-device state

```
VkInstance (client-side)
├── connection (HV_SOCKET)
├── sender thread
├── handshake info
├── physical devices (1, populated from server reply)
└── enabled extensions / api version

VkDevice
├── parent VkInstance
├── queues (handles passed through; the server-issued VkQueue is what the client sees)
├── per-thread record buffers (optional — to coalesce vkCmd* before flushing)
└── memory_shadow regions
```

Synchronization:
- The send queue is MPSC; many app threads can call into the ICD, the sender thread serializes onto the wire.
- Reply demultiplexing uses a token assigned at write time; the sender thread waits on the right condvar.

## Synchronous vs asynchronous calls

The codegen tags each function:

| Category | Examples | Behavior |
|---|---|---|
| Async, no return-by-pointer | `vkCmdDraw`, `vkCmdBindPipeline`, `vkQueueSubmit` (in optimistic mode) | Fire-and-forget; errors out-of-band |
| Sync — small reply | `vkCreateBuffer`, `vkAllocateMemory`, `vkAcquireNextImageKHR`, `vkGetFenceStatus` | Block on reply |
| Sync — large reply | `vkGetPhysicalDeviceProperties2`, `vkGetPipelineCacheData` | Block on reply, may stream |

`vkQueueSubmit` can be optimistically async if the guest doesn't care about the immediate `VkResult` — most apps treat a non-success there as fatal anyway. A debug mode forces full sync.

## Memory shadow

`vkMapMemory` must return a CPU pointer the app can read/write. Two implementations:

**Phase 1 (no shared mem)**: allocate a guest-side buffer of the same size, return its address. On `vkUnmapMemory` or `vkFlushMappedMemoryRanges`, send the dirty bytes upstream. On `vkInvalidateMappedMemoryRanges`, request the bytes back from the server.

**Phase 2 (shared mem)**: the server pre-allocates a region inside virtio-shmem; on `vkAllocateMemory` the server replies with an offset into that region; `vkMapMemory` translates the offset to a pointer using the shared mapping in the guest. No copies needed for host-visible allocations.

Coherent memory: the spec allows writes to be visible without explicit flush. In Phase 1 we either:
- Lie about coherency: don't expose the coherent bit. Apps will use explicit flush. DXVK / VKD3D handle this case.
- Synthesize fences: too expensive in Phase 1.

Default Phase 1 behavior: report `HOST_VISIBLE | HOST_CACHED`, not `HOST_COHERENT`. Apps that demand coherent memory may misbehave; document the limitation. Phase 2 fixes it.

## Surface and swapchain

```c
VkResult vkCreateWin32SurfaceKHR(VkInstance, const VkWin32SurfaceCreateInfoKHR* info, ...)
{
    // Don't pass HWND; instead send a "create host window" command.
    // Hints come from the handshake (app name) plus optional client-side
    // GetWindowText() of the HWND for a friendlier title.
    HostWindowHints hints = derive_hints(info);
    return rpc_create_surface(hints, &out_surface);
}
```

The HWND in the guest still exists (DXVK / VKD3D need somewhere to think they're rendering to). The ICD treats the HWND as a hint, not a real render target. Any GDI-style operations on the HWND are unrelated to our render path. When the app destroys the HWND we destroy the host window in response.

`vkAcquireNextImageKHR` and `vkQueuePresentKHR` are sync RPCs. Pipelining: the ICD can issue a "prefetch acquire" right after present completes so the sync wait on the next acquire is shorter.

## Win32-specific quirks

- **DLL load order**: ensure the loader (`vulkan-1.dll`) finds our ICD before any virtio-gpu ICD that might be installed.
- **AppContainer / UWP**: low-rights processes can't open arbitrary HV_SOCKETs without manifest entries. Document this; v1 doesn't target sandboxed apps.
- **Wow64**: 32-bit and 64-bit ICDs must coexist. Build both. Their connections are independent.
- **Driver signing**: an ICD is just a user-mode DLL, no driver signing required. The installer should still be Authenticode-signed for SmartScreen.

## Build

- MSVC (CMake) primary toolchain because the loader and Hyper-V socket headers are happiest there.
- MinGW-w64 secondary, useful for cross-compiling from Linux during development.
- Both 32 and 64 bit.

## Debugging

- A `LSW_TRACE=1` env var writes every call ID + size to a log.
- A `LSW_LOOPBACK_TCP=host:port` mode lets the ICD talk to a server over TCP instead of vsock — invaluable for development on a single machine.
- `LSW_DUMP_FRAMES=path` writes raw protocol frames for offline replay, which is the killer feature for bug repro.
