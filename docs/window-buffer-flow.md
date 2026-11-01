# The life of a window's pixels

From the moment a Windows application draws a window to the moment those pixels are on the host's screen. This note is about one ordinary mirrored window. Monitor capture and full-screen flips come in as variations.

It describes the working trees as they stand on 2026-11-01. They hold uncommitted latency work, and another change is adding a refresh-rate setting to the display-mode files. Line numbers are of that state and will drift. Where a fact depends on uncommitted work, it says so.

Tags: **[C]** confirmed from the code or a doc citing a decompile or trace. **[M]** measured, with the source named. **[P]** plausible only. A claim about Windows' or KWin's inside that no source here settles is [P] at best.

Paths:

- `av:` is `external/accel-virt/src/`
- `avdoc:` is `external/accel-virt/docs/`
- `rw:` is `external/remotewin/source/`
- `rwinc:` is `external/remotewin/include/`
- `lsw:` is `src/host/`

Sizes: **W** is the window's pixels, **M** the monitor's. A pass of 2560x1440 BGRA is 14.7 MB, by arithmetic.

## 1. Overview

```
GUEST (Windows, vCPUs, in QEMU's process on the host)        | HOST
                                                             |
application                                                  |
  GDI ----------- CPU writes -----------------+              |
  D3D9/10/11/12, OpenGL, Vulkan               |              |
    | commands, not pixels                    |              |
    v                                         |              |
avumd / avgl / avvk (user-mode drivers)       |              |
    | ring per context, doorbell ------------------------->  QEMU: virtio-gpu-av worker thread
    |                                         |              |    |
    |                                         |              |    v
    |                                         |              | libav_executor.so in QEMU
    |                                         |              | DXVK D3D11, DXVK D3D9, vkd3d-proton, Vulkan, GL
    |                                         |              | on the render GPU: draws, stages, copies
    | present                                 |              |    ^
    v                                         |              |    | ticket
dxgkrnl / dxgmms2 --> avgpu.sys (miniport) ----- control queue -> present engine thread
    |                                         |              |    | GPU copy into lent memory,
    |                                         v              |    v or CPU copy into its pages
    |                      window's redirection surface: guest pages, with host
    |                      system memory laid under them (lending)
    v                                         |              |
DWM (dwm.exe, avumd) composes, its commands also run on the host's DXVK D3D11 device
    | renders the window into: 1. DWM's primary  2. the WGC frame pool buffer
    v                                                        |
remotewin-guest.exe: pool buffer copied to a shared slot     |
    | commands via ring; "slot k, copy n" via                |
    | e1000e and SLIRP (TCP) ------------------------------> remotewin host process
    |                                                        |   export socket: dma-buf once, sync_file per frame
    |                                                        |   wl_buffer once, explicit sync per commit
    |                                                        v
    |                                                      KWin: composes, or scans out the buffer directly
    |                                                        v
    |                                                      display GPU's scan-out: the screen
    <--- SlotFree (TCP) once the compositor released the buffer
```

In order, with machine, process and GPU:

| # | Stage | Machine, process | Who touches pixels |
|---|---|---|---|
| 1 | Application draws | guest process; for 3D, its commands run in host QEMU | guest CPU for GDI; render GPU for 3D |
| 2 | Present point staged or `show` copy | host QEMU, executor | render GPU |
| 3 | Present reaches the kernel and the present engine | guest kernel, then QEMU's engine thread | render GPU, or host CPU |
| 4 | DWM composes the primary and the capture | guest dwm.exe, commands run in QEMU | render GPU |
| 5 | remotewin-guest copies pool buffer to slot | guest process, commands run in QEMU | render GPU |
| 6 | Frame named over TCP, synced over the export socket | guest to host remotewin to QEMU | none |
| 7 | Twin copy, only across GPUs | QEMU | render GPU, into system memory |
| 8 | remotewin attaches and commits | host remotewin | none |
| 9 | KWin composes or scans out | host kwin_wayland | display GPU |

All guest GPU work runs on one GPU, the render GPU, and all D3D11 sessions share one host immediate context (av:executor/d3d/d3d11_batch_progress.hpp, comment) [C]. Direct3D 9 has a device per guest device, on its own Vulkan device (avdoc:compositor.md) [C]. LSW's domain has no `<graphics>` and video model `none` after install (lsw:src/vm/domain_xml_builder.cpp:204-216) [C].

## 2. The systems

### 2.1 The application's API and the guest user-mode drivers

**What:** `avumd.dll` (D3D9/10/11/12), `avgl.dll`, `avvk.dll`. They are record-and-send drivers: `av:um/d3d/`, `av:icd/d3d/`, `av:icd/opengl/`, `av:icd/vulkan/`.

**Owns:** per-process transport (`KmtTransport`, av:guest/transport/kmt_transport.hpp) and each resource's record. Almost no pixels.

**Receives:** the runtime's DDI calls.

**Hands on:** commands in a ring, and at a present, a kernel present through the runtime's callback.

- D3D11 decides a buffer's model at creation: a primary is `scanout`, a shared buffer `flip`, any other `blt` (av:icd/d3d/swap_chain_buffer.cpp, assigned at av:um/d3d/d3d11_device_present.cpp:79) [C].
- `present_surface` dispatches on it (av:um/d3d/d3d11_device_present.cpp:105-130) [C].
- D3D9's present: av:um/d3d/d3d9_device_present.cpp:198-249. A keyed primary takes `show_primary` (:95-105). Anything else takes `publish_point` (:120-148), which names its point to the miniport by escape, because d3d9.dll drops the present's private data [C].

### 2.2 dxgkrnl, dxgmms and the miniport

**What:** Windows' kernel graphics, and `avgpu.sys`. Only the miniport is ours.

**Owns:** kernel allocations and their guest pages, fence delivery to the kernel, flip state, tickets.

**Receives:** presents, packets, escapes.

**Hands on:** present requests on the control queue, flips, interrupts turned into dxgkrnl notifications.

- `Adapter::present` (av:miniport/adapter/adapter.cpp:1708-1749) writes the DMA buffer and moves the pixels by `move_presented_pixels` (:1752-1820), which reads the sub-rectangles from `pDstSubRects` (:1804) [C].
- A flip moves nothing there (:1758) [C].
- `image_of` (:1823-1836) names a staged host frame as the source when there is a point [C].
- `PresentQueue` keeps up to 4 present payloads outstanding (av:miniport/present/present_queue.hpp) [C].
- A packet carrying a ticket completes only once that ticket has moved (`mark_present`, :1863; avdoc:protocol.md "Present engine") [C].
- `PresentationCaps` reports `SupportSoftwareDeviceBitmaps` (av:miniport/adapter/driver_capabilities.cpp:49, explained at .hpp:84-92). With it CDD makes a window's redirection surface a CPU-visible GDI surface (avdoc:stale-blt-frames.md, "The path taken") [C]. The file is modified in the working tree.
- GDI and staging allocations are pinned in the aperture segment so their pages are known, and mapped cached. The comment gives the historical reason: an uncached read "seventeen milliseconds at 1280x800, which held a game to every other vblank" (av:miniport/memory/allocation.cpp:91-132) [C, the number is theirs].

### 2.3 DWM and redirection surfaces

**What:** dwm.exe, closed code, on avumd.

**Owns:** its swap chain's primaries and its opened views.

**Receives:** whatever dxgkrnl hands it about windows' surfaces.

**Hands on:** a composed primary (a flip), and rendered capture buffers.

There are two swap models and GDI, and DWM reads each differently.

- **Redirected windows (GDI, D3D9 windowed, D3D10/11 blt-model):**
  - CDD makes one redirection surface per window, covering the whole window rect, invisible borders included (1298x847 for a 1280x800 client, avdoc:stale-blt-frames.md) [C].
  - DWM opens it with no key: av:um/d3d/d3d11_device_objects.cpp:258 tries `open_cross_adapter` (av:um/d3d/d3d11_device_present.cpp:715-778).
  - There, `AllocationKind::gdi` with `cpu_visible` counts as a redirection (:729). `lender_.lend` lends the pages (:754), and the host makes a linear texture over them (`CreateResource` with the blob handle and pitch, :774).
  - If the lend is refused, `open_guest_surface` (:780-826) makes a shadow and a host texture, and reads the pages at each bind (:846-894) [C].
- **Flip-model:**
  - DWM opens the buffer by key (av:um/d3d/d3d11_device_objects.cpp:263-266). `D3dShareState::open` answers the `shown` copy where one exists (av:executor/sharing/d3d_share_state.cpp:67-80) [C].
  - DWM never sees the live D3D11 flip buffer unless it opened before the copy was made, which the host logs (av:executor/shim/d3d11_host_device.cpp:555-566) [C].
  - A Direct3D 12 buffer is answered with a DXVK view over vkd3d-proton's own memory (`viewed`, `view_of`, av:executor/shim/d3d11_host_device.cpp:700-733) [C].
- **DWM's own swap chain** is `CDXGISwapChainDWMLegacy`, seen live through Present1. Separately, DisplayCore presents through `PresentMultiplaneOverlay` (avdoc:research/d3d-desktop/reverse-engineering.md:26-37,377-390) [C as notes]. Which one DWM uses per frame in this build is open (§7.3). Both paths call `show_flip`.

### 2.4 Lent pages, FrameBufferCopy, the present engine

**Lending:**
- `AllocationLender::lend` locks the allocation for its address and adopts it as class `readback` (av:um/d3d/allocation_lender.cpp:24-39) [C].
- The device allocates host memory, requires it to be write-back, copies what the RAM holds, and moves that part of the dma-buf's mapping over the run's RAM with `mremap` (`HostBlobTable::begin_lending`, av:device/memory/host_blob_table.cpp:194-236; avdoc:host-memory.md "Application memory") [C].
- The guest's page tables don't change: its pointer and GDI now reach host memory that the GPU also reads [C].
- A join happens only from another plane (`held_beyond`, av:device/memory/guest_ram_lending.cpp:117-123). A page lies under one lending, and a second lending on the same plane is refused (avdoc:host-memory.md "A page lies under one lending at a time") [C]. These files are modified in the working tree.

**Present engine** (av:device/present/present_engine.cpp): one thread, 16 slots (present_engine.hpp:30).
- `queue` runs on the main loop (:139), which maps the page lists and calls `QueuedPresent::lend` (av:device/present/queued_present.cpp:53-79). That duplicates the lending's fd, per present.
- `run` moves in order (:192-200) and publishes completion and an interrupt (:226-238) [C].

**FrameBufferCopy** (av:executor/shim/frame_buffer_copy.cpp): `vkCmdCopyImageToBuffer` into the lending's dma-buf, imported once per lending, 4 kept (`most_imports`, .hpp:38; `import_for`, :124-136). It waits for the previous copy into that lending (:199) so a late one can't land over this one [C].

**CPU fallback:** `RegionMover` (av:device/present/region_mover.cpp:38-59) is one memcpy per row. Why the GPU was not asked is logged once (present_engine.cpp:19-29): colours swapped, not lent, scattered, no descriptor, lending gone, no layout, or executor declined [C]. Direct3D 12 declines (avdoc:compositor.md) [C].

### 2.5 The virtio-gpu-av device: rings, doorbells, fences

**Owns:** one ring, reply slot and fence page per context; 64 doorbell pages; 68 virtqueues; worker threads.

**Receives:** batches and control requests.

**Hands on:** commands to the executor, fence completions to the guest.

- **Doorbell:** a 4-byte store to BAR 0, a KVM ioeventfd that wakes the worker without the main loop (avdoc:protocol.md "Doorbells") [C].
- **Worker:** spins up to 100 us before sleeping, and up to 100 ms polling while results are unpublished (av:device/threading/worker.cpp:182,201-229) [C].
- **Per batch:** `RingConsumer::consume_next` (av:device/context/ring_consumer.cpp:102-143) orders across rings with `D3dOrdering::wait_before` (:128), copies the payload, and runs it [C].
- **Completion:** a batch completes only once the GPU passes its mark (`complete_or_hold`, :145-162; av:executor/d3d/d3d11_batch_progress.hpp) [C]. Direct3D 9 sessions set no such mark in `end_batch` when they hold no D3D11 lock (av:executor/d3d/d3d_backend.cpp:225) [C for the guard; the effect on a D3D9 session's fence was not traced further].
- **Control queue commands** run on QEMU's main loop [C].

### 2.6 The executor, DXVK and vkd3d-proton

**What:** `libav_executor.so` in QEMU, driving DXVK, vkd3d-proton, OpenGL and Vulkan on host GPUs (avdoc:compositor.md).

**Owns:** host objects, `HostShares`, `SharedSurfaces`, `ExportableImages`.

**Receives:** guest commands.

**Hands on:** GPU work; dma-bufs and sync_files to the export path.

- Every D3D11 batch ends with a fence `Signal` and `Flush`, so a submission (av:executor/d3d/d3d_backend.cpp:222-242; d3d11_host_device.cpp:632-638) [C].
- A shared 2D texture is made by `ExportableImages::make_tiled` in a GPU tiling exported with its modifier, or `make_rows` if the maker asked for rows (av:executor/d3d/d3d_backend.cpp:321-337; av:executor/shim/exportable_images.cpp:133-182) [C].
- The modifier list is filtered for draw, blend, sample and copy (av:executor/shim/vulkan_device_table.cpp:200-233). It isn't filtered for scan-out [C].
- Textures not shared are DXVK's own, and their tiling is DXVK's choice [P]; DXVK's source was not read.
- The executor makes no host swap chain and no Vulkan present: a grep of `av:executor` for `Present(`, `vkQueuePresent` and `CreateSwapChain` finds no call site [C]. D3D9's device is windowless with an unused 16x16 back buffer (av:executor/shim/d3d9_host_device.cpp:14,126-141) [C].

### 2.7 WGC capture and remotewin's guest half

**What:** `remotewin-guest.exe` in the user's session.

**Owns:** one D3D11 device (rw:guest/capture/graphics_device.cpp:39), per window a `WindowCapture`, a `SurfaceRing` and, for some windows, `PictureEdges`.

**Receives:** WGC frames.

**Hands on:** a slot number and copy count over TCP.

- A mirrored window is captured per window: `MirrorSession::open` subscribes (rw:host/mirror/mirror_session.cpp:257), `GuestSession::subscribe` makes a `WindowCapture` (rw:guest/session/guest_session.cpp:272-277), and `CaptureStream::open_window` calls `CreateForWindow` (rw:guest/capture/capture_stream.cpp:202-216) [C]. Each window, popups included, is its own subscription, capture and ring [C].
- Frame pool: `CreateFreeThreaded`, BGRA8, 3 buffers (:68,:300-303), because "with two, about one composition in six found none free" (:64-67) [C]. `MinUpdateInterval` is set to 0 so every composition may capture (:73-88), since the default 16 ms gate drops frames (avdoc:frame-pacing.md "The WGC capture rule") [C].
- Buffer size has room: the picture rounded up to 128, plus 128, kept until it is 512 too big in a side (rw:guest/capture/picture_room.cpp; rwinc:guest/capture/picture_room.hpp:17-20), so a resize need not remake the pool [C]. Why: 87 sessions and 16 rings in one live drag (external/remotewin/docs/review-and-redesign.md:448) [M, theirs].
- The main loop wakes on `frames_.event` or a fixed timeout (rw:guest/session/guest_session.cpp:177-180), default 60 per second, so a 16 ms timeout (rw:guest/main.cpp:19; guest_session.cpp:100) [C].
- A frame is taken only if a slot is free (:657-662) [C]. `next_frame` drains to the newest (rw:guest/capture/capture_stream.cpp:445-462) [C].
- Slot: `CopySubresourceRegion` of the picture, then `Flush` (rw:guest/capture/surface_ring.cpp:56-60). The WGC frame is held in `sources_[slot]` until the host frees the slot (:67,:80), "to prove the copy finished" (:61-62) [C].
- `surface.margin_is_reserved()` (rw:guest/windows/window_surface.cpp:66: neither shaped nor DWM-framed) adds `PictureEdges::all_drawn` (rw:guest/capture/picture_edges.cpp:81-101). Four thin copies and a CPU read decide whether the window drew to its edges. If it did not, the frame goes as pixels (guest_session.cpp:668-675) [C].

### 2.8 The export channel

**What:** a `SOCK_SEQPACKET` socket, one thread per connection (av:device/export/export_connection.cpp:48-67; avdoc:external-export.md). LSW passes `--export-socket` (lsw:src/mirror/window_mirror.cpp:134-142) [C].

**Owns:** which surfaces a connection holds.

**Receives:** export and sync requests.

**Hands on:** a dma-buf once per slot, and a sync_file per frame.

- A sync takes the D3D11 lock, reads the copy count, and on another GPU refreshes the twin. Then `sync_file` drains DXVK's CS thread and makes an empty submission (av:executor/shim/d3d11_host_device.cpp:214-229; av:executor/shim/dxvk_vulkan_device.cpp:138-157) [C].
- Only copies count, not draws (avdoc:external-export.md §4.1; av:executor/d3d/d3d_backend.cpp:1693-1696) [C].

### 2.9 remotewin's host half

**What:** one host process per application (rw:host/).

**Owns:** one `wl_surface` per mirrored window, cached `wl_buffer`s, two syncobj timelines per surface.

**Receives:** `FrameInSlot` on the link, then sync answers.

**Hands on:** a commit to the compositor, and later `SlotFree` to the guest.

- `take_frame` (rw:host/export/shared_frame_feed.cpp:129-143) leases the slot and asks for a sync. `synced` (:295-318) hands the frame on when `copies >= n`, otherwise asks again 1 ms later (rwinc:host/export/copy_wait.hpp:27) [C].
- `MirrorSurface::show` replaces any waiting frame (rw:host/wayland/mirror_surface.cpp:285-292). `attach_shared` (:333-375) puts the sync_file at the next acquire point and a release point (per frame, a binary syncobj create, import and transfer, plus an eventfd: rw:host/wayland/sync_timeline.cpp:40-66) [C].
- `commit_attached` damages the whole buffer and asks presentation feedback (mirror_surface.cpp:402-413) [C].
- The buffer is created once per slot's memory (rw:host/wayland/dmabuf_buffers.cpp:23-34) [C].
- There is no `wl_surface.frame` (a grep of rw and rwinc finds none), and `suspended` is parsed but used nowhere (rw:host/wayland/toplevel_role.cpp:76) [C].
- `WindowMirror::show_frame` keeps the last frame, lease included, so a new surface can show it at once (rw:host/mirror/window_mirror.cpp:224-225,648-654) [C]. One slot of a window's three is therefore held between frames [C].

### 2.10 KWin and scan-out

**What:** kwin_wayland 6.8.0 on this host, closed to this note: its source was not read.

**Receives:** commits with a buffer, acquire and release points.

**Hands on:** a frame on the output, a release, presentation feedback.

- Opaque region: a plain window's `GetWindowRgn` answers no region, so its opaque region is empty and remotewin sets NULL on an ARGB8888 buffer (rw:guest/windows/window_reader.cpp:86-105,259; rw:host/link/guest_window.cpp:49-52; rw:host/wayland/mirror_surface.cpp:231-235) [C]. A monitor mirror sets none at all (rw:host/mirror/monitor_session.cpp) [C].
- Fullscreen is asked for only for a window that covers its monitor with no caption and no sizing border (rw:host/mirror/window_mirror.cpp:415-428) [C].
- Whether KWin composes, blends, or scans out in each case is [P] and open.

### 2.11 The cross-GPU twin

When the compositor's GPU isn't the one the export names, remotewin sets `kOtherGpu` on export and sync (rw:host/export/shared_frame_feed.cpp:51-60,152-157) [C]. The device makes a linear image in host-visible, not device-local memory (`SystemMemoryTwin`, av:executor/shim/system_memory_twin.cpp; avdoc:external-export.md §4.2) [C]. Each sync at a new count adds a GPU copy and a Flush (:72-80) [C]. `offers` requires the compositor to take linear ARGB8888 [C].

### 2.12 The control path

- **Link:** the guest half connects by TCP (rw:guest/session/tcp_client.cpp:33-69, `TCP_NODELAY`) over e1000e and SLIRP (lsw:embedded/domain.xml:217-220) [C]. `FrameInSlot` goes up (guest_session.cpp:703-712) and `SlotFree` comes down (rw:host/export/slot_lease.cpp) [C].
- **Vblank:** `DisplayClock` wakes at each blank and publishes a page (av:device/display/display_clock.cpp:161-204). It raises the display line each blank while dxgkrnl has vsync on, otherwise every 24th (av:miniport/display/device_vblank.cpp:112-116, .hpp:38) [C].
- **Flip:** the DPC sends `SCANOUT_IMAGE` and `RESOURCE_FLUSH` on the control queue per numbered flip (av:miniport/adapter/adapter.cpp:536,543-553; av:miniport/display/scanout_resource.cpp:120-131,268-275) [C]. The device ignores them unless the console has GL (av:device/qemu/virtio-gpu-av.c:327-335; virtio-gpu-av-host.c:587-592) [C].
- **DisplayLock:** retimes the guest's blanks from presentation feedback, for the mirror holding keyboard focus (rw:host/export/shared_frame_feed.cpp:174-185; rw:host/latency/display_lock.cpp; `kGuestPeriodNs` 5.56 ms at rwinc:host/latency/display_lock.hpp:54). All uncommitted, and being changed [C].

## 3. The buffers

"Per chain" means one per swap chain buffer.

| Buffer | Allocated by | Where it lives | Size | Format | Lifetime, count |
|---|---|---|---|---|---|
| D3D9 back buffer | DXVK via D3D9 session | render GPU, DXVK's tiling [P] | client | app's | swap chain, per chain |
| D3D9 buffer's kernel allocation | `D3d9Device::allocate` (d3d9_device_present.cpp:46-63) | guest RAM; pixels only on read-back fallback [C] | client | same | first present on |
| D3D9 `held_` | `D3d9FramePool::Frame::make` (av:executor/shim/d3d9_frame_pool.cpp:109-129) | render GPU, DXVK's | back buffer | same | pooled, reused by size and device |
| D3D9 `surface_` | same frame, `D3DPOOL_SYSTEMMEM` | host memory | back buffer | same | made with every frame, used only on read-back [C] |
| D3D11 blt buffer | DXVK | render GPU | client | app's | per chain |
| D3D11 blt `held_` | `D3d11FramePool::Frame::take` (d3d11_frame_pool.cpp:72-82) | render GPU, DXVK's | client | same | pooled |
| D3D11 flip buffer | `make_tiled` (exportable_images.cpp:159-182) | render GPU, GPU tiling with modifier | client | app's | per chain |
| `shown` copy | `made_shown` (d3d11_host_device.cpp:527-552) | render GPU, DXVK's; not exported | same as buffer | same | one per flip buffer or primary, life of the share |
| D3D12 buffer | vkd3d-proton | render GPU | client | app's | per chain; DWM's DXVK view shares its memory |
| Redirection surface | CDD, via miniport (av:miniport/memory/standard_allocation.cpp) | guest RAM pages with host `readback` memory laid under them when lent: system memory on a dGPU [C] | window rect | B8G8R8A8 or X8 [P] | life of window, one |
| DWM's texture over it | `lent_texture`, `adopt_rows` (d3d_backend.cpp:339-370; d3d11_host_device.cpp:690-695) | no pixels of its own: linear view of the lending | window rect | viewed format | per opener |
| Unlent: DWM's shadow and host texture | `open_guest_surface` | shadow in guest paged memory; texture on render GPU | window rect | same | fallback only |
| DWM primary | DWM's swap chain | render GPU; tiling and count not determined | M | desktop format | DWM's life |
| `shown` of primary | as `shown` | render GPU | M | same | per primary |
| WGC pool buffer | remotewin's device, `MISC_SHARED` kind, opened by DWM (avdoc:frame-pacing.md:113-119) | render GPU, tiled [C by that doc] | window, with room | B8G8R8A8 | 3 per capture; DWM keeps every one it opened (avdoc:shared-surfaces.md §0) |
| Slot | `SurfaceRing::remake` (surface_ring.cpp:83-126) | render GPU, GPU tiling, exported | window, with room | B8G8R8A8 | 3 per window, until outgrown |
| Edge textures | `PictureEdges::fit` | staging | Wx2 and 2xH | B8G8R8A8 | 2 sets, margin-reserved windows only |
| Twin | `SystemMemoryTwin::make` | system memory, linear | as slot | same | per slot, cross-GPU only |
| GL frame source, resolve target | GL presenter | render GPU | client | window's | context |
| GL or Vulkan chain slot | `DxgiWindowChains`, rows | render GPU, linear | client | channel order of frame | GL 2, Vulkan 1 (av:icd/common/window_chains.hpp; chained_image.hpp) |
| Chain back buffer | DXGI flip chain | as a D3D11 flip buffer | client | desktop | chain |
| KWin's output buffers | KWin | display GPU | output | not determined | KWin's |
| `HostImageExport` | d3d11_host_device exports | render GPU, linear | M | | 1, GL console only (host_image_export.hpp:30) |
| Pixel fallback: `staging_`, `scratch_`, `visible_`, message, host copies, `wl_shm` | remotewin both halves | system memory | window | BGRA | fallback only |
| Full-screen D3D9 primary | `make_on_gpu` (d3d9_flip_shares.cpp:217-236) | compositor's memory, **linear**, adopted as rows (exportable_images.cpp:94-100) | M | X8R8G8B8 or A8R8G8B8 | one per key |

## 4. The copies

Status words: **required** (the design has no other way here), **conditionally required**, **no reader found**.

### 4.0 The common tail, every path

Once DWM has a window's new pixels:

**T1. DWM renders the window into its primary.** Source: the window's texture. Destination: DWM primary. Who: dwm.exe's commands, on DXVK's D3D11 queue, render GPU. Extent: DWM's choice; damage-limited composition is [P]. PCIe: only if the source is lent system memory on a dGPU. Cost: pixels and shader. Why: Windows composes the desktop; DWM can't be told not to. Without it there is no desktop in the guest. Status: required by Windows, though no one on the host sees the result when no display is attached (§4.0, T2).

**T2. `show` of the primary.** Source: primary. Destination: its `shown` copy. Who: executor, same queue. Extent: **whole monitor**, a `CopyResource` (av:executor/shim/d3d11_host_device.cpp:514-523), whatever changed. PCIe: no. Why, from the comment: "the scanout reads what it shows long after the present, by which time the application [...] may be drawing the next one into the same buffer, which the display then caught half drawn (Subnautica)" (av:um/d3d/d3d11_device_present.cpp:192-195). Readers: `presented_texture` only (d3d11_host_device.cpp:583-592), from `export_image` and `compose_planes`. Those are reached by a GL console or an export `head` request. LSW's domain has no console, and no consumer of `head` was found in remotewin or lsw (grep). What breaks without it: a GL console or `head` reader could show a half-drawn frame. Status: **no reader found in LSW's default VM**. That this runs for DWM's buffers rests on DWM's being primaries (reverse-engineering.md:192).

**T3. DWM renders the window into the WGC pool buffer.** Source: the window's texture. Destination: a pool buffer. Who: dwm.exe, render GPU. Extent: the window, each time it is dirty [P]. For monitor capture, the whole monitor every dirty frame (avdoc:frame-pacing.md:120-121) [C]. PCIe: if lent. Why: this is what WGC is. Status: required while capture is WGC.

**T4. Edges** (margin-reserved windows only). Four 1-pixel-wide copies into staging plus a CPU read, one frame behind where it can be (rw:guest/capture/picture_edges.cpp:70-101). Why: a window with a transparent margin is trimmed, which only pixels can do; the frame then goes as pixels (guest_session.cpp:666-675). Breaks: a transparent margin would be mirrored whole. Status: conditionally required.

**T5. Pool buffer to slot.** Source: pool buffer. Destination: slot. Who: remotewin-guest's commands, DXVK's queue, render GPU. Extent: **the whole picture**, not its dirty part (rw:guest/capture/surface_ring.cpp:56-57). PCIe: no. Why (external-export.md §5.3): the host needs surfaces with ids learnt once, and a per-frame count, which only copies increment. Breaks without it: the host has no id or count for a pool buffer as the code stands. It does not buffer anything: the pool buffer stays held until the slot is freed (surface_ring.cpp:61-62,67,80) [C]. Status: conditionally required (by the id and count protocol, not by timing).

**T6. Twin** (cross-GPU only). Source: slot. Destination: twin. Who: executor at sync. Extent: whole slot. PCIe: yes (render GPU writes system memory, display GPU reads it). Why: VRAM in another GPU's tiling can't be read by the compositor's GPU (avdoc:external-export.md §4.2). Status: conditionally required.

**T7. KWin composes.** Source: slot or twin. Destination: KWin's output buffer. Who: kwin_wayland, display GPU. Extent: remotewin declares the whole buffer damaged. PCIe: if a twin. Status: not determined whether it runs (§2.10). If direct scan-out applies, it does not.

**T8. Scan-out** reads the output buffer, or the slot directly. Display engine. Not a copy.

### 4.1 GDI window

**G1. GDI draws.** Source: GDI's drawing. Destination: redirection surface pages. Who: guest CPU in the app's or win32k's context. Extent: what is drawn. PCIe: no (system memory). Why: this is GDI, with no acceleration, since CDD never accelerated GDI for this adapter (avdoc:stale-blt-frames.md "Kernel-mode command buffers"). Status: required. Then T1 to T8, DWM sampling lent memory directly.

**G1f.** If lending is refused (renderer mode, or pages already lent):
- `GuestSurfaceScan` compares rows to a shadow with streaming loads and copies the changed band (av:icd/d3d/guest_surface_scan.cpp).
- `upload` sends the band to the host texture (av:um/d3d/d3d11_device_present.cpp:881-893).
- Who: DWM's guest CPU, at each frame's first bind. Extent: the row band that differs, whole width. Status: conditionally required.
- Measured cost: 5.6 ms of DWM's render thread a frame instead of 0.65 at a 2560x1440 D3D9 window (avdoc:frame-pacing.md:125-135) [M, theirs].

### 4.2 D3D9 windowed (blt)

**D1.** The app's draw into the back buffer, in QEMU, render GPU.

**D2. `StretchRect` into `held_`.** Source: back buffer. Destination: `held_`. Who: session's worker, D3D9's DXVK device. Extent: **whole buffer**, every present (`every_present`, av:executor/d3d/d3d9_session_resources.cpp:503-504; d3d9_frame_pool.cpp:139). PCIe: no. Cost: pixels. Why: the engine moves a frame later, by point, so the frame must be fixed where the buffer is drawn: "what the application draws into it after this returns never reaches the window" (the same reason, stated for D3D11 at d3d11_device_present.cpp:229-232); "the staging copy is ordered on the writer's own queue" so nothing can hang on a present that never comes (avdoc:shared-surfaces.md §2.5). Also resolves multisampling. Breaks: a frame read later shows what the app drew next. Staging every present rather than on demand exists because WPF presents once and its windows stayed black (shared-surfaces.md, "A writer that knows a blt reads it"). Status: required under this design.

**D3. Frame into the window's memory.** Source: `held_`. Destination: lent memory at the window's client rect. Who: engine thread, `vkCmdCopyImageToBuffer` on D3D9's queue (av:executor/shim/d3d9_frame_copier.cpp; queue_work.cpp:128-162), waited for before the ticket completes. Extent: **the kernel's sub-rectangles** (`place_regions`, queued_present.cpp:137-153). PCIe: yes on a dGPU. Cost: pixels, plus a submit. Why: the frame must reach the surface DWM composes from, and only the kernel knows the client rect (stale-blt-frames.md). Breaks: the window shows nothing but its GDI fill. Status: required.

**D3c.** CPU version, for any `Fallback` (queued_present.cpp:173-189): `held_` is staged to the system-memory `surface_` and memcpy'd by row. Status: conditionally required.

Then T1 to T8, with T1 and T3 sampling lent system memory.

### 4.3 D3D11 flip-model window

**F0.** App's draw into the tiled shared buffer.

**F1. `show`.** Source: flip buffer. Destination: its `shown`. Who: session worker, DXVK's D3D11 queue. Extent: **whole buffer** (`CopyResource`). PCIe: no. Why: "nothing drawn after this present reaches the window" (d3d11_device_present.cpp:185-187), and a compositor must find the copy, never the live buffer (d3d_backend.cpp:862-863). The flip's history includes DWM reading a buffer the app drew into next (d3d11_batch_progress.hpp). A DirectComposition surface is excluded because it is "drawn between its presents as well, and a copy taken at one would hide those draws" (:189-190). Breaks: DWM could read a buffer while the app draws its next frame. Direct3D 12 runs without such a copy (§4.4), under its own ordering. Status: conditionally required.

Then T1 to T8, sampling `shown`.

### 4.4 D3D12 flip-model window

No copy on the host for a flip: `PresentResource12` orders DXVK's queue after the presenting queue by a shared fence (av:executor/d3d/d3d12_session.cpp:811-822; av:executor/shim/d3d12_presentation.hpp), and DWM samples a view of the same memory [C]. `once_blt_read`: stages nothing until a blt once asks (shared-surfaces.md §2.5) [C]. Then T1 to T8.

### 4.5 D3D11 blt-model window

**B1.** `CopySubresourceRegion` buffer to `held_`, then `Flush` (d3d11_frame_pool.cpp:83-84). Whole subresource, every present (d3d_backend.cpp:1924). Same reason as D2. Status: required under this design.

**B2.** Same as D3: `copy_into` (:151-177) on DXVK's queue, sub-rectangles only. Status: required.

Note: DXGI also opens the surface into the app's device, and `open_cross_adapter` then tries to lend it there too (stale-blt-frames.md, "The same for a Direct3D 10/11 blt-model window"). Two opens on one plane can't both hold it (§2.4). Which wins, and what the loser does in practice, is open. Then T1 to T8.

**D3D12 blt:** `D3d12FramePool` stages to a readback buffer on the queue (`CopyTextureRegion`, resolve if multisampled, av:executor/shim/d3d12_frame_pool.cpp:196,227) once a blt has read the surface. FrameBufferCopy declines, so the engine moves it by CPU (avdoc:compositor.md) [C]. The surface's first frame races; a frame at most (shared-surfaces.md §2.5) [C].

### 4.6 OpenGL window

- **GL1.** Resolve, if the format is multisampled (`glBlitNamedFramebuffer`, av:icd/opengl/back_buffer_presenter.cpp:120-122).
- **GL2.** `glCopyImageSubData` frame to chain slot, one-way, then glFinish (av:icd/opengl/chained_frames.cpp:70). A copy, so the app's scissor and masks can't touch it (shared-surfaces.md, step 5). Whole frame. Status: conditionally required.
- **GL3.** Slot to chain back buffer, by a draw, since GL's rows are bottom-up (av:um/common/dxgi_window_chains.cpp:120-121,274). Status: required while GL's rows are bottom-up.
- Then F1, T1 to T8.
- Fallback: if no chain, read back and blit to the DC (back_buffer_presenter.cpp:125-132), then G1.

### 4.7 Vulkan window

- **V1.** `vkCmdCopyImage` image to slot (av:icd/vulkan/swapchain.cpp:252; present_device.cpp:241-250,337-). Whole image, unconverted.
- **V2.** `CopyResource` slot to chain back buffer (dxgi_window_chains.cpp:122-123).
- Then F1, T1 to T8.
- A present returns once the chain has read the slot (chained_image.hpp) [C].

### 4.8 Variation: monitor capture

`MonitorSession`, `WatchMonitor`, `MonitorCapture` (rw:host/mirror/monitor_session.cpp:37-44; guest_session.cpp:208-213; capture_stream.cpp:218-232). T3 and T5 become M for any dirty frame. Windows' own paths don't change. Measured on Iris Xe: QEMU held 26 to 28% of the render engine against 9 to 11% for a window capture, and 16 to 19% once shared textures were tiled (avdoc:frame-pacing.md:106-120) [M, theirs].

### 4.9 Variation: full-screen D3D9 flip

**X1. Back buffer into the shared primary.** Whole monitor, D3D9's queue, into a linear image (av:executor/d3d/d3d9_flip_shares.cpp:88-107,217-236). Why: Direct3D 11, where DWM is, opens that memory as the primary (avdoc:compositor.md). Status: conditionally required.

Then T3, T5, T7. T1 and T2 happen only if DWM composes the frame, which is open. A D3D11 full-screen primary takes `show` instead (T2 form), with Subnautica as the reason, and DWM's read of it is its `shown` copy.

### 4.10 Add-on: cross-GPU

T6 joins any path. KWin reads system memory, linear.

### 4.11 Fallback: pixels

When sharing is off, or a margin frame: `CopyResource` to staging (capture_stream.cpp:417-418), `Map`, row memcpy (:431-434), maybe a margin trim (window_capture.cpp:99-106), TCP over e1000e and SLIRP, host decode, `last_pixels_` copy (window_mirror.cpp:633-646), `wl_shm` copy (mirror_surface.cpp:260-265). Measured limit: about 116 MB/s, 8 fps at 2560x1440 (avdoc:external-export.md, opening) [M, theirs].

### 4.12 Tally for a full window

Counting the whole window or monitor as one pass, on a same-GPU host:

| Path | Passes of W | Passes of M | Crosses PCIe (dGPU) |
|---|---|---|---|
| GDI | T1, T3, T5, T7 | T2 | T1 and T3 read |
| D3D9 blt | D2, D3, T1, T3, T5, T7 | T2 | D3 writes; T1, T3 read |
| D3D11 blt | B1, B2, T1, T3, T5, T7 | T2 | same |
| D3D11 flip | F1, T1, T3, T5, T7 | T2 | none |
| D3D12 flip | T1, T3, T5, T7 | T2 | none |
| OpenGL | GL2, GL3, F1, T1, T3, T5, T7 | T2 | none |
| Vulkan | V1, V2, F1, T1, T3, T5, T7 | T2 | none |

T1 assumes DWM samples the whole window; T7 assumes KWin composes. Both are open (§7.3).

## 5. Synchronisation

### 5.1 What waits on what

| Thing | Waits on | Waited on by | Mechanism |
|---|---|---|---|
| Batch completion | GPU reaching its mark | fence page, guest | `D3d11BatchProgress`, EventfdWake (av:executor/d3d/d3d11_batch_progress.hpp); `complete_or_hold` |
| Follow packet | batch's fence | the present behind it on the context | `BatchFollower` (av:um/d3d/batch_follower.cpp:19-40) |
| Flip's reuse | the runtime's waits on the kernel context | the device's next published batch | `KernelContextGate`, `PresentOrder::submitted` (av:um/d3d/present_order.cpp:36-46) with a 1 s bound |
| Write into a shown primary, or `show_flip` | a blank after the last flip, plus 4 ms, or 21 ms since it | the frame's write | `FlipShownWait` (av:um/d3d/flip_shown_wait.cpp:20,26,74-89), with `Sleep(1)` |
| Flip's copy on D3D9 | D3D11's previous readers (reverse timeline) | D3D11's next readers (forward timeline) | `flip_on_gpu`, `ComposeOrder` |
| D2/B1 staging | writer's queue order | engine's `copy` | `SharedSurfaces`, polling every 100 us up to 500 ms (shared_surfaces.cpp:14,124-141; present_frames.hpp:26) |
| D3 GPU copy | previous copy into that lending | ticket, then packet | `FrameBufferCopy` timeline |
| Blt's packet | ticket | dxgkrnl's completion of the present | `mark_present`, present line |
| CPU read of redirection pages | present engine's move | presents into them, held up to 20 ms | `PagesReadRecord`, `PresentWrites` (protocol.md); only unlent (`open_guest_surface`) |
| Lend or give-back swap | engine idle, vCPUs out of KVM_RUN | everything | `RunSwapGate` (host-memory.md) |
| Slot refill | slot free | `has_free_slot` | `SlotLedger`, `SlotFree` |
| Pool buffer reuse | the frame closed, on slot free | DWM's next render | `sources_[slot]`, WGC |
| Frame seen by host | `copies >= n` | commit | `CopyWait`, retried 1 ms apart up to 2000 asks |
| KWin reading the slot | acquire point (the sync_file) | its compose | `wp_linux_drm_syncobj`, `signal_with` |
| Slot return | KWin's release point, and no `last_shared_` holding the lease | `SlotFree` | eventfd, `SlotLease` destructor |

Keyed mutexes: nothing here uses them for a window's frames; `D3d11Device` distinguishes a keyed `ReleaseSync` only to avoid a read-back (avdoc:stale-blt-frames.md) [C]. avdoc:shared-surfaces.md lists them as future work [C].

A note on the blt path: dxgkrnl tells DWM of the dirty region *before* it submits the present (avdoc:stale-blt-frames.md, "What Windows orders") [C]. With lent pages, DWM reads memory directly. What orders that GPU read against D3's GPU write on another device's queue was not found. A measurement reports none torn with a present every 4 ms (avdoc:frame-pacing.md:131-135) [M, theirs]. Open (§7.3).

### 5.2 One frame of a D3D11 flip window, in order

Times aren't given because none are sourced at this level; §7.1 has end-to-end figures.

1. App draws. Commands queue in avumd.
2. `Present`. `show_flip` awaits the last flip's blank if pending, then sends `PresentPoint(0)` and flushes. Tail published, doorbell rung.
3. Worker wakes. `wait_before`. Runs batch under the D3D11 lock: draws recorded, `show` recorded. `end_batch`: signal, flush.
4. UMD: `follow` queues a packet naming the batch, then `pfnPresentCb`. `flipped` records the scan line.
5. GPU finishes. EventfdWake wakes the worker. Fence page written, interrupt. DPC completes the follow packet. dxgkrnl processes the present and DWM learns of it.
6. Reuse: the app's next batch waits behind the runtime's kernel-context wait, up to 1 s.
7. A blank reaches DWM through a display line, DPC and dxgkrnl report.
8. DWM's frame: T1, T3 (its batches, each signal and flush). Present: T2. Capture present through DWM's flip manager [P].
9. `FrameArrived`: `arrivals_.raise`. Main loop wakes. `has_free_slot`. `next_frame` drains. Maybe T4. T5, `Flush`. Worker runs it under the D3D11 lock, counted.
10. `FrameInSlot` over TCP: e1000e, SLIRP, host socket.
11. `take_frame`, sync. Connection thread: lock, count, flush, empty submit, sync_file back. T6 if crossing.
12. `attach_shared`: acquire point from the sync_file, release point. `commit_attached`.
13. KWin waits for acquire, composes or scans out at its latch. Feedback goes to `DisplayLock`.
14. Next frame replaces this buffer. KWin releases it. `Held` goes. `last_shared_` is replaced by the next frame too. `SlotLease` destructor sends `SlotFree`. Guest frees the slot and closes the WGC frame, returning the pool buffer.

### 5.3 Differences for a blt window

Replace 2 and 3 with: `PresentPoint` runs D2 or B1, flushed; `follow`; `pfnPresentCb` (for D3D9, after a `NextPointRecord` escape). dxgkrnl calls `DxgkDdiPresent`, which queues a ticket and returns. Main loop accepts it, `lend`s. Engine thread: D3, waits for GPU, publishes, raises the present line. Packet completes. Reuse is immediate (`Reuse::immediate`), so no gate.

## 6. Damage

| Stage | Is the damage known? | Is it used? |
|---|---|---|
| Application `Present` (D3D9 `pDirtyRegion`; DXGI `Present1`) | yes, app's choice | not by this driver |
| GDI into redirection | win32k knows: `GreAddBitmapD3DDirtyRgn`, `bSpDwmNotifyDirty` (stale-blt-frames.md) | Windows uses it; driver does not see it |
| `D3DDDIARG_PRESENT` (D3D9 DDI) | no rectangle (stale-blt-frames.md) | no |
| `DXGI_DDI_ARG_PRESENT1` | yes: `pDirtyRects` in av:../third_party/windows-kit/include/um/dxgiddi.h:518-519 | **no**: no use in av:um/d3d or av:icd/d3d [C] |
| D2, B1 staging | not passed | whole |
| F1 `show` | no | whole |
| `DxgkDdiPresent` | yes: `pDstSubRects`, `SubRectCnt` | **yes**: D3, D3c, regions (adapter.cpp:1804; present_blit.cpp:gather; queued_present.cpp:137-153) |
| MPO attributes | `pDirtyRects` in the record (av:miniport/ddi/dxgk_multiplane_overlay_attributes3_record.hpp:23) | no use found in overlay_plane.cpp or display_sources.cpp |
| Unlent redirection | recomputed by comparing rows | yes, as one row band of full width |
| DWM's compose, T1 | DWM knows | DWM's own [P] |
| T2 `show` of primary | no | whole monitor |
| T3 capture render | DWM knows | whole window or monitor, per avdoc:frame-pacing.md:120-121 for monitor; window not determined |
| WGC frame | `DirtyRegionMode` named in external/remotewin/docs/review-and-redesign.md:203 | not requested: no use in rw |
| T5 | no | whole picture |
| T6 | no | whole |
| `wl_surface.damage_buffer` | no | whole buffer (mirror_surface.cpp:404-405) |
| KWin | sees whole damage | not determined |
| `SCANOUT_IMAGE` flush | carries width and height (scanout_resource.cpp:120-131) | full size |

Damage survives only one hop, `DxgkDdiPresent` into the window's memory, and is lost before and after it.

## 7. Facts, measurements, questions

### 7.1 Measurements, all from existing docs

| Figure | Source |
|---|---|
| 2560x1440 on RX 9070 XT, RADV Mesa 26.2.4: sync and sync_file waited 0.10 to 0.13 ms; copy into a linear exportable image waited 0.22 to 0.24 ms; a whole-frame copy about 0.1 ms and 15 MB | avdoc:external-export.md §7 |
| Intel Xe: full-screen draw into linear 3.5x slower, from linear 6x slower than tiled (`tools/host/shared_image_bench.c`) | avdoc:frame-pacing.md:114-116 |
| Xe: monitor capture 26 to 28% of the render engine, window capture 9 to 11%; monitor 16 to 19% once tiled | avdoc:frame-pacing.md:112-119 |
| 2560x1440 D3D9 window with the caps bit, unlent: DWM's render thread 5.6 ms a frame against 0.65; `memcmp`, `memmove` | avdoc:frame-pacing.md:125-135 |
| Lending: 59 captures a second of a GDI monitor, 36 before; `d3d9_dirty_present` never more than a frame behind at a present every 4 ms, none torn | avdoc:frame-pacing.md:131-135 |
| Reading those pages: 80 MB/s through `memcmp`; 642 to 11 ms a frame for a `GetDC` | avdoc:frame-pacing.md:192-194 |
| Present to capture arrival: window 30 ms; monitor 31.7, or 48 when the unexplained state holds | avdoc:frame-pacing.md:182-190 |
| Mirror at 180 Hz, full-screen D3D9 drawing every blank: 176 frames a second; "host GPU 92% busy", QEMU 1.66 cores | avdoc:frame-pacing.md:202-204 |
| Pixels over network: about 116 MB/s, about 8 fps at 2560x1440 | avdoc:external-export.md, opening |
| A resize, before room in buffers: 87 sessions and 16 rings in a live run; after, 4 and 4 | external/remotewin/docs/review-and-redesign.md:448 |
| Unloaded host, RX 9070 XT, 6 s: fdinfo engines 5.2% of wall time, `gpu_busy_percent` mean 37.6 (min 24, max 90), SCLK about 1580 MHz. A single sample at light load, not the probe's regime, but `gpu_busy_percent` isn't a share of time | measured 2026-11-01 for this note; what the guest was doing then was not recorded [M] |

No per-pass cost of T1, T3 or T7 is known, for any window or GPU.

### 7.2 Things stated above as fact

Tagged where they appear. The ones a study will lean on most:

- T2 runs at M on every DWM present and has no reader in LSW's default VM [C, resting on the RE note that DWM's swap chain buffers are primaries].
- T5 holds its source for as long as its destination (surface_ring.cpp:61-62,67,80) [C].
- D3's destination is system memory on a dGPU, and DWM samples it linear (host-memory.md; adopt_rows) [C].
- A flip-model D3D11 buffer's every present is a whole-buffer copy (`show`); D3D12's is not [C].
- Damage reaches only `DxgkDdiPresent` (§6) [C].
- Nothing paces commits to the compositor's refresh (no `wl_surface.frame`), and `suspended` is ignored (§2.9) [C]. KWin discards the feedback of a surface it is not compositing (avdoc:frame-pacing.md:176-177) [C].
- Capture is per window for a mirrored window (§2.7) [C].

### 7.3 Open, could not be determined

1. **How many times DWM samples a window per frame.** DWM is closed; no trace of it is in the trees. Expected: once for the primary if visible, plus once per capture containing it [P]. A count of draws binding the window's view per DWM frame, in `AV_D3D_TRACE`, would settle it.
2. **DWM's present DDI in this build:** Present1 or `PresentMultiplaneOverlay`. The RE notes hold both. It decides whether `FlipShownWait` holds DWM. A guest `trace-scopes` run would settle it.
3. **Whether DWM uses planes for a window.** `planes()` reports `max_planes` when the device composes (av:miniport/display/overlay_support.cpp:57-63), and a plane would bypass T1. Not known whether DWM does while capturing.
4. **Which opener wins a D3D11 blt window's lending**, and whether DWM then reads through `open_guest_surface`. The log line "a window's redirection surface could not be lent: it is read" would show it.
5. **What orders DWM's GPU read of lent pages against D3's GPU write.** Also CPU writes by GDI against that read.
6. **KWin 6.8's rules:** direct scan-out for a surface with no opaque region, for a tiled modifier not filtered for scan-out, under explicit sync; how much it repaints below such a surface. Comparing kwin's `drm-engine-gfx` for a full-screen mirror would settle whether T7 runs.
7. **DXVK's and vkd3d-proton's internal choices:** tiling and compression of non-shared textures, how their `StretchRect` and `CopySubresourceRegion` are carried out.
8. **DWM primary's tiling, buffer count, and whether its misc flags make it `make_tiled`.**
9. **WGC:** whether a capture of an unchanged window really produces no frames (the code assumes so: guest_session.cpp:655-656); whether pool textures carry an id `surface_id_reader` could read; whether `DirtyRegionMode` is available on the guest's build.
10. **Whether T3 of a window is limited to its dirty part.** Monitor capture is whole.
11. **A D3D9 session's batch fence:** `end_batch` returns early with no D3D11 lock held (d3d_backend.cpp:225), so whether a D3D9 session has any GPU mark was not traced.
12. **`latprobe32`'s present path.** It isn't in the repo, so what avdoc:frame-pacing.md:197-204 measured could be §4.9 or §4.2.
13. **Whether T1 runs when the guest window is covered or off-screen.** A mirrored window can be covered in the guest while its capture continues.
