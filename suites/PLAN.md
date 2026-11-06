# LSW suites

One guest executable, `lsw-suites`, holds every suite. A suite is a folder under `suites/` and a CMake library
linked into that executable. The first suite is `sanity`: correctness checks for the whole stack, from the
miniport to window capture, run on every GPU in the `scripts/gpu-test` matrix, in 64-bit and 32-bit builds.
A later `benchmark` suite shares the runner but never runs in parallel.

The goal of `sanity` is that any regression in the driver stack, the transport, the memory system, an API, an
interop path, presentation or the mirrors fails a named check, on a named GPU, with an artifact that shows what
went wrong.

---

## 1. Layout and build

```
suites/
  CMakeLists.txt        standalone project lsw_suites; target lsw-suites (the executable)
  PLAN.md               this file
  core/                 target suite_core: runner, registry, workers, report, check helpers
  sanity/               target suite_sanity: the sanity checks
  benchmark/            target suite_benchmark (later)
```

- `suites/` is its own CMake project, not part of the LSW build. It is configured with the guest toolchains:
  - x64: `cmake/toolchain-mingw-w64.cmake` builds `lsw-suites64.exe`;
  - x86: `cmake/toolchain-mingw-w64-i686.cmake` builds `lsw-suites32.exe`.
  `OUTPUT_NAME` follows `CMAKE_SIZEOF_VOID_P`, so both builds can sit in one folder in the guest.
- Every suite folder is a CMake target (`suite_<name>`, an OBJECT library so nothing is dropped by the linker)
  and exports one registration function, e.g. `lsw::suites::sanity::add(Registry&)`, called from `main`.
- Shaders and kernels are compiled at build time where a compiler exists on the host (DXC/FXC bytecode from the
  accel-virt d3d tool tree, glslang for SPIR-V) and embedded as byte arrays; GLSL and OpenCL C stay as source
  strings so the guest compilers are exercised too.
- A `scripts/build-suites.sh` builds both bitnesses into `build-suites/` and `build-suites32/`, with ccache and
  the shared core limit.

## 2. The executable

### Command line

| option | meaning |
|---|---|
| `--list` | print every check id, its suite, its tags and its policy, then exit |
| `--suite NAME[,NAME]` | run these suites (default: `sanity`) |
| `--filter GLOB[,GLOB]` | only checks whose id matches; `-GLOB` excludes |
| `--tag TAG` | only checks with a tag (`d3d11`, `interop`, `exclusive`, `x86-only`, `slow`...) |
| `--jobs N` | worker processes for parallel suites (default: logical CPUs, capped at 16) |
| `--timeout S` | per-check timeout (default per check, 60 s if none) |
| `--repeat N` | run every selected check N times (flake hunting) |
| `--shuffle SEED` | randomise order within the parallel phase |
| `--json FILE` | write the result file (schema in 2.4) |
| `--artifacts DIR` | where failing checks drop images, buffers and logs (default `.\lsw-suites-artifacts`) |
| `--run-one ID` | internal: run one check in this process (used by workers) |

### Parallelism

- A suite declares a policy:
  - `parallel` (sanity): checks run in up to `--jobs` worker processes at once;
  - `serial` (benchmark): one check at a time whatever `--jobs` says, and the runner refuses to start it while
    another `lsw-suites` instance is running (a named mutex), so benchmark numbers never share the GPU.
- A check can declare `exclusive`. Exclusive checks of a parallel suite run after the parallel phase, one at a
  time, with no other worker alive. Exclusive is for anything that changes or needs quiet global state:
  device-loss injection, TDR, memory exhaustion, display mode changes, DWM restarts, frame pacing and timing
  measurements, power transitions, driver log scans.
- Every check runs in its own worker process: the executable starts itself with `--run-one ID`. A crash, a hang,
  a device loss or a leaked handle then fails one check, not the run; the parent enforces the timeout with a job
  object and kills the whole process tree.
- Output is deterministic under parallelism: lines are printed in registry order once a check finishes, not in
  completion order; the JSON keeps start time, duration, worker pid and exit cause.

### Output

The same lines the existing probes print, so `gpu_matrix.py` and `run_guest_suite.py` parse them unchanged:

```
ok sanity.d3d11.map.discard_rename 12ms
FAIL sanity.gl.pbo.pack_slice status=mismatch 41ms
note sanity.cl.mem.use_host_ptr_small copied below lent_minimum
lsw-suites64: 812 passed, 2 failed, 3 skipped
```

- Exit code: 0 all passed, 1 some check failed, 2 the runner itself failed (bad arguments, no adapter).
- `skip` is used only for a declared reason (the host GPU lacks a feature, the check is x86-only); a skip
  without a reason is a failure.

### Result file

```
{ "exe": "lsw-suites64", "bitness": 64, "adapter": {...}, "driver": {...}, "started": "...",
  "checks": [ { "id": "...", "suite": "sanity", "tags": [...], "status": "ok|fail|skip|timeout|crash",
                "reason": "...", "ms": 12, "pid": 1234, "artifacts": ["..."], "measures": {...} } ] }
```

`measures` carries numbers a check observed (frame intervals, transfer sizes, counters) so the matrix can compare
GPUs and runs, and so the benchmark suite later shares the schema.

### Check helpers (suite_core)

- `Context`: artifacts folder, logging, timeouts, bitness, adapter identity.
- Windows: create, show, move, resize, occlude, minimize a test window on a dedicated thread.
- Devices: create D3D9/9Ex, D3D11 (hardware and WARP), D3D12, GL (pixel format, attribs, share), CL, VK
  instances on the accel-virt adapter by LUID, never by index.
- Patterns: deterministic pixel and buffer patterns from a seed, with exact expected values, so most checks
  need no stored reference.
- Compare: exact, per-channel tolerance, PSNR; writes `expected.png`, `actual.png`, `diff.png` on failure.
- Readers: KMT queries (`D3DKMTQueryAdapterInfo`, `D3DKMTGetScanLine`, `D3DKMTWaitForVerticalBlankEvent`),
  the driver state through `avinspect kernel-state` when present.

## 3. gpu-test integration

- `gpu_matrix.py` gets a suite `suites`. For each GPU it pushes `lsw-suites64.exe` and `lsw-suites32.exe`, runs
  both in the console session through `run_script` with `"session": 1`, pulls the JSON and artifacts, and judges:
  a check fails the GPU if it failed in either bitness.
- `--suite-args "..."` passes options through (`--suite sanity --filter 'sanity.d3d11.*' --jobs 8`).
- Host phase. Around each run the matrix records host state and judges host invariants (section 4.N):
  executor and device state dumps before and after, the driver log (`avgpu.log`) delta, the QEMU log delta,
  avinspector stalls. A run that passes in the guest but leaks host objects or logs driver errors fails.
- `aggregate.py` reports `suites` checks like the others: "on every GPU" against "this GPU only", and 64 against
  32-bit.
- Results stay comparable over time: the matrix keeps the last green JSON per GPU and flags checks that went from
  ok to fail, and `measures` that moved past a per-check bound.

## 4. The sanity catalogue

Ids are `sanity.<area>.<group>.<check>`. Each row says what the check does and what passes. Unless marked
x86-only or x64-only, every check runs in both bitnesses. `[E]` marks exclusive checks.

Oracles, in order of preference:
1. analytic expected values (clears, patterns, arithmetic results);
2. CPU reference computed in the check (CL kernels, blends, format conversion);
3. WARP in the same guest for D3D9/10/11 (and D3D12 when WARP12 is installed), compared within tolerance;
4. a cross-API twin in the same guest (the same scene in D3D11 and Vulkan);
5. stored references, only where nothing above works.

### 4.A Environment and install

| check | does | passes when |
|---|---|---|
| env.adapters.present | enumerates adapters (DXGI, KMT, SetupAPI) | every accel-virt adapter is listed, started, problem code 0 |
| env.adapters.luid_stable | LUIDs from DXGI, KMT and VK match | one LUID per adapter in every API |
| env.adapters.roles | display vs render-only adapter | the display adapter owns the outputs, the render-only one has none |
| env.driver.versions | file versions of avgpu.sys, avumd, avumd32, avgl, avcl, avvk (both bitnesses) | all from one release; the UMD version is the stamped catalog version |
| env.driver.protocol | capset and protocol from the driver log or state | capset 2, protocol major matches the host |
| env.driver.features | capset feature bits | match what the host executor reports for this GPU |
| env.icd.opencl | OpenCL ICD registry (64 and WOW6432Node) | the avcl vendor is registered once per bitness and loads |
| env.icd.vulkan | Vulkan ICD JSON registration | avvk manifest points at the right DLL per bitness, api version 1.3 |
| env.icd.opengl | OpenGL ICD registry (OpenGLDriverName, 32 and 64) | avgl is the ICD for the adapter |
| env.requirements.test_signing | boot options | test signing on |
| env.requirements.driver_search | the three driver search values | off |
| env.requirements.display_core | DWM key and dwm.exe modules | DisableDDisplay=1 and ddisplay.dll not loaded |
| env.display.output_visible | driver state of each display source | visible, a primary and a scanout address; catches the DisplayCore off-loop |
| env.display.modes | display mode list | contains the host modes LSW set, at the host refresh |
| env.dwm.running | dwm.exe up, composition on | DWM present and composing |

### 4.B Kernel driver (miniport, through KMT and the driver state)

| check | does | passes when |
|---|---|---|
| kmd.qai.all_types | every D3DKMTQueryAdapterInfo type the KMD answers | answers are stable and consistent (caps, segment sizes, WDDM version) |
| kmd.caps.consistency | DRIVERCAPS against MPO caps against the UMD's DXGI caps | plane counts agree; today they do not (4, 1, 0), so this is a known failure |
| kmd.segments.describe | segment layout | aperture and memory segments match the host window size |
| kmd.alloc.kinds | allocations of each kind (primary, shadow, staging, gdi, surface, client) through the runtimes | created, locked, freed without driver log errors |
| kmd.alloc.lock_cpu | D3DKMTLock on lockable allocations, write and read back | data round-trips; no pages left mapped after unlock |
| kmd.alloc.large | allocations up to the segment size and beyond | big ones succeed, oversize fails with an error, not a hang |
| kmd.paging.evict_restore [E] | memory pressure to force eviction, then use | contents survive eviction and restore |
| kmd.aperture.map_unmap | many map and unmap cycles | aperture pages return to baseline (driver state) |
| kmd.sched.native_fences | native fence signal and wait across contexts | monotonic, never early |
| kmd.sched.hw_queues | user-mode submission queues | submissions retire in order, doorbells ring once each |
| kmd.vsync.event | D3DKMTWaitForVerticalBlankEvent | interval matches the mode's refresh within 10% |
| kmd.vsync.scanline | D3DKMTGetScanLine | scan line advances and wraps within the mode |
| kmd.display.visibility | driver source state through a present | the source stays visible between presents (no visible/hidden churn) |
| kmd.display.mpo3_base [E] | an MPO3 flip without layer 0 after a legacy address set | the scanout keeps the legacy primary (known defect, display_sources.cpp:120) |
| kmd.display.vsync_kinds | vsync report kind per flip kind | type 3 after an address flip, type 10 with the right PresentIds after MPO3 |
| kmd.cursor.shape | set mono, color and masked cursors | the driver accepts each, the host cursor matches (host phase) |
| kmd.escape.all | each escape the UMDs use, with malformed sizes | well-formed ones work, malformed ones are refused cleanly |
| kmd.log.clean [E] | driver log delta over the run | no failure lines (status != 0, refused, dropped lines) except declared ones |
| kmd.power.d3_d0 [E] | adapter power down and up (when Windows allows) | scanout and contexts restored |

### 4.C Transport and protocol

| check | does | passes when |
|---|---|---|
| xport.ring.wrap | submissions sized to wrap the ring many times | every result correct, ring head consistent |
| xport.ring.full | flood until the ring is full | the client waits (ring_waits counted), nothing is lost |
| xport.command.large | commands larger than the staging buffer | split into pieces, reassembled exactly |
| xport.piece.cap | uploads at 1, 4, 14, 31.9, 32, 33, 64 MiB | one piece up to 32 MiB, then 32 MiB pieces; split_transfers and split_pieces match exactly |
| xport.piece.floor | uploads while the arena cannot grow | 4 MiB pieces, never inline for large data |
| xport.inline.small | tiny transfers | inline, no blob room used |
| xport.reply.limits | replies of 1 KiB to 16 MiB | correct up to the reply cap; above it the API path splits |
| xport.order.single_context | writes then reads in one context | strict order |
| xport.order.many_contexts | 32 contexts submitting at once | each context ordered, no cross-talk |
| xport.fence.monotonic | fence values per context | never go backwards, never complete early |
| xport.teardown.inflight | destroy a context with work queued | host frees it; no leaked blob or context (host phase) |
| xport.kill.midway [E] | kill a process mid-submission | host contexts and blobs freed; other contexts unaffected |
| xport.stats.semantics | provoke splits, waits, copies, refusals | ChannelStats counters move by the exact provoked amounts (extra = split_pieces) |
| xport.stats.cadence | change one counter | a report reaches the host within the stats interval |
| xport.contexts.limit | open contexts until the limit | the limit is reached cleanly, then contexts work again after closing |

### 4.D Memory

| check | does | passes when |
|---|---|---|
| mem.window.alloc_free | host window allocations of many sizes | pages_in_use returns to baseline |
| mem.window.exhaust [E] | allocate until refusal | the API returns out-of-memory, refusals counted, recovery after freeing |
| mem.window.caching | write-back and write-combined blobs, CPU read and write speed and coherence | WC writes coherent after fence; WB reads hit cache; no torn data |
| mem.arena.grow [E] | sustained large uploads | arena grows within the pressure window, blob_room_waits stay low |
| mem.arena.shrink [E] | stop after growth | segments released within ~10 s of idle |
| mem.arena.regions | GL presenter and CL staging regions | regions allocated, reported as set-aside, freed on teardown |
| mem.slice.rename | D3D11/D3D9 DISCARD loops, GL persistent buffers | no slice reused before its fence; data never torn (pattern check every frame) |
| mem.slice.blocks | streams under growth and idle | blocks grow geometrically, spare blocks freed after the spare lifetime |
| mem.lent.cl | CL USE_HOST_PTR above and below lent_minimum | above: zero-copy, the host pointer is coherent after map; below: copied, and the check records it |
| mem.lent.vk | VK_EXT_external_memory_host imports | imported pages coherent both ways |
| mem.budget | VK memory_budget, DXGI QueryVideoMemoryInfo, CL device memory | numbers consistent across APIs and with the host window |
| mem.leak.loops | create/destroy 1000x for each resource kind per API | host blobs, guest allocations and handles return to baseline |
| mem.large.x64 (x64-only) | resources over 4 GiB total | succeed and round-trip |
| mem.large.x86 (x86-only) | allocations against the 32-bit address space | fail cleanly near the limit, never corrupt |
| mem.residency | D3D12 MakeResident/Evict | evicted resources restore with contents |
| mem.alignment | odd sizes and offsets, 1-byte to page-crossing | exact round trip |

### 4.E Per API

Each API section has the same groups: device and caps, resources (the full matrix of memory classes and usages),
transfers, pipeline and draw or dispatch, queries and sync, present, threading, errors and known gaps.

#### D3D9 and D3D9Ex

| check | does | passes when |
|---|---|---|
| d3d9.device.create | D3D9 and D3D9Ex, HAL, windowed and fullscreen-ready | created on the accel-virt adapter |
| d3d9.caps.pin | GetDeviceCaps, format and query counts | match a pinned table derived from the host |
| d3d9.pools.matrix | every pool (DEFAULT, MANAGED, SYSTEMMEM, SCRATCH) x usage (DYNAMIC, WRITEONLY, RENDERTARGET, DEPTHSTENCIL) x type (VB, IB, texture, cube, volume, surface) | create, lock, fill, read back exact |
| d3d9.lock.flags | DISCARD, NOOVERWRITE, READONLY, NO_DIRTY_UPDATE, partial rects and boxes | contents exact; DISCARD and NOOVERWRITE take slices (counters) |
| d3d9.lock.slice_reuse | NOOVERWRITE appends across frames | no data written into a slice the GPU still reads |
| d3d9.blt.all | StretchRect, UpdateSurface, UpdateTexture, ColorFill, GetRenderTargetData | exact or within filter tolerance |
| d3d9.draw.fixed_function | fixed-function states, fog, lighting, texture stages | matches WARP within tolerance |
| d3d9.draw.shaders | vs/ps 1.1 to 3.0 | matches WARP within tolerance |
| d3d9.query.all | EVENT, OCCLUSION, TIMESTAMP(DISJOINT, FREQ), VCACHE | sensible, monotonic, occlusion counts exact |
| d3d9.present.modes | Present, PresentEx, swap effects DISCARD/FLIP/COPY/FLIPEX, intervals | frames arrive in order (4.G) |
| d3d9ex.shared.surface | shared handles between D3D9Ex devices and processes | content visible to the opener; refused operations fail cleanly |
| d3d9.wpf.path | the WPF/D3DImage pattern: D3D9Ex shared render target, dirty rects, presents | matches the expected frame (the Affinity UI path) |
| d3d9.gaps | decode, video process, overlay, PresentMultiPlaneOverlay | fail with E_NOTIMPL, no crash, no host error |
| d3d9.lost.reset | device lost and Reset/ResetEx | resources recreated, rendering correct after |

#### D3D10 and D3D10.1

| check | does | passes when |
|---|---|---|
| d3d10.device.create | D3D10CreateDevice and D3D10.1 on the adapter | created (layered on the D3D11 DDI) |
| d3d10.draw.basic | clears, draws, readback | exact or WARP-equal |
| d3d10.interop.dxgi | DXGI swap chain from D3D10 | presents correctly |

#### D3D11 (immediate and deferred)

| check | does | passes when |
|---|---|---|
| d3d11.device.levels | every feature level 9_1 to 11_1 | levels the host offers create; others fail cleanly |
| d3d11.caps.pin | CheckFeatureSupport for every type the UMD answers | matches a pinned table; zeroed answers are the declared ones |
| d3d11.formats.support | CheckFormatSupport and quality levels for every DXGI format | equal to the host rows |
| d3d11.resources.matrix | usage (DEFAULT, IMMUTABLE, DYNAMIC, STAGING) x bind flags x CPU access x misc (SHARED, NT handle, keyed mutex, GDI_COMPATIBLE, TEXTURECUBE, RESOURCE_CLAMP) x dimension (buffer, 1D, 2D, 3D, arrays, mips, MSAA) | creates or fails exactly like WARP |
| d3d11.map.kinds | READ, WRITE, READ_WRITE, WRITE_DISCARD, WRITE_NO_OVERWRITE, DO_NOT_WAIT | data exact; DO_NOT_WAIT returns WASSTILLDRAWING when busy (today it never does) |
| d3d11.map.discard_rename | DISCARD every draw for 1000 draws | no torn or stale data; copied_mappings 0 when slices are offered |
| d3d11.map.watched_shadow | partial writes to a mapped DEFAULT/STAGING buffer | only written runs uploaded; untouched bytes intact |
| d3d11.update.subresource | UpdateSubresource for every dimension, partial boxes, row and depth pitches, BC formats, 14 MiB full-screen | exact; one piece up to 32 MiB |
| d3d11.copy.all | CopyResource, CopySubresourceRegion (offsets, mips, arrays, depth), CopyStructureCount, ResolveSubresource, GenerateMips | exact or within filter tolerance |
| d3d11.readback.staging | GPU write then staging map READ | exact, and waits for the GPU |
| d3d11.draw.pipeline | every shader stage including tessellation and GS stream-out, blend, depth, stencil, rasterizer states | WARP-equal |
| d3d11.compute.uav | CS with UAVs, append/consume, atomics, typed loads | CPU reference equal |
| d3d11.query.all | every query type including timestamp and pipeline statistics | values consistent; published slot path and round-trip path both correct |
| d3d11.deferred.basic | record on deferred contexts, execute on immediate | equals the immediate result |
| d3d11.deferred.map | maps inside deferred contexts, large updates over 128 KiB recording pieces | exact |
| d3d11.threading | free-threaded resource creation from 8 threads | no corruption, no deadlock |
| d3d11.tiled.gaps | tiled resource DDIs | fail cleanly (E_NOTIMPL) and caps say tiled resources unsupported |
| d3d11.shared.keyed_mutex | keyed mutex acquire/release across devices and processes | ordering held, content visible |

#### DXGI

| check | does | passes when |
|---|---|---|
| dxgi.factory.enum | EnumAdapters1/ByLuid/ByGpuPreference, outputs, modes | outputs only on the display adapter, modes match |
| dxgi.swap.models | BITBLT and FLIP models, SEQUENTIAL and DISCARD, 1 to 16 buffers | each presents correctly (4.G) |
| dxgi.swap.waitable | frame latency waitable object, SetMaximumFrameLatency 1 to 16 | the wait releases once per frame |
| dxgi.swap.stats | GetFrameStatistics, GetLastPresentCount | counts advance one per present |
| dxgi.swap.resize | ResizeBuffers during presentation, many sizes | no stale size, no leak |
| dxgi.swap.fullscreen [E] | SetFullscreenState on and off | transitions without loss; mode restored |
| dxgi.present1 | Present1 with dirty rects and scroll | content correct |
| dxgi.mpo.caps | GetMultiplaneOverlayCaps | consistent with the KMD (known failure today) |
| dxgi.memory.info | QueryVideoMemoryInfo | budget and usage plausible and moving with allocations |
| dxgi.gdi_compatible | GetDC on a GDI_COMPATIBLE back buffer (also 4.F) | GDI draws land in the presented frame |

#### D3D12

| check | does | passes when |
|---|---|---|
| d3d12.device.create | device on the adapter, feature levels, shader models | created; options match a pinned table |
| d3d12.heaps.matrix | DEFAULT, UPLOAD, READBACK, CUSTOM (each CPU page property and memory pool) x committed, placed, reserved | create, map where allowed, round trip exact |
| d3d12.heaps.placed_alias | aliased placed resources with barriers | aliasing behaves as specified |
| d3d12.upload.paths | upload heap slice vs dedicated, readback lent pages vs slice vs dedicated | data exact on each path (force each by size) |
| d3d12.copy.all | CopyBufferRegion, CopyTextureRegion (footprints, BC), CopyResource, ResolveSubresource | exact |
| d3d12.queues | direct, compute, copy queues, ExecuteCommandLists with more than 64 lists | results and order correct |
| d3d12.fences.cross_queue | signal/wait between queues and with the CPU, SetEventOnCompletion | no early completion, no deadlock |
| d3d12.pipeline.all | root signatures, descriptor heaps and tables, PSOs, pipeline library | renders WARP12-equal or analytic |
| d3d12.descriptors.dimensions | every SRV/UAV/RTV dimension | supported ones work, unsupported ones fail cleanly |
| d3d12.tiles | reserved resources, UpdateTileMappings with many ranges | correct, including more ranges than one command |
| d3d12.present | swap chain on a D3D12 queue, flip models | frames in order (4.G); today untested |
| d3d12.shared | shared heaps and resources with NT handles | OpenHeapAndResource behaviour as declared (E_NOTIMPL today) |
| d3d12.gaps | stream output, mesh/amplification, raytracing, sampler feedback | caps report them unsupported and calls fail cleanly |

#### OpenGL

| check | does | passes when |
|---|---|---|
| gl.context.attribs | every profile, version, flag, robustness and no-error attribute | the granted context matches the request |
| gl.context.share | wglShareLists, shared contexts, objects across them | shared objects visible; sliced buffers stay in the creator |
| gl.pixel_formats | every reported format | choose, set, draw, read back |
| gl.buffers.storage_matrix | BufferStorage flag combinations (MAP_READ/WRITE, PERSISTENT, COHERENT, DYNAMIC_STORAGE, CLIENT_STORAGE) and BufferData usages | data exact; persistent non-dynamic buffers are sliced; SubData on a sliced buffer gives INVALID_OPERATION |
| gl.map.ranges | MapBufferRange flags, FlushMappedBufferRange, unsynchronized, invalidate | exact; no copy for sliced buffers |
| gl.pbo.pack_unpack | pack and unpack through PBOs and slices, all formats and alignments | exact |
| gl.textures.all | all targets, formats, compressed, mip generation, sub-images | exact or within tolerance |
| gl.draw.core | core profile pipeline and state | analytic or VK-twin equal |
| gl.draw.compat | compatibility profile, display lists, immediate mode | analytic equal |
| gl.sync | FenceSync, ClientWaitSync with timeouts, glFinish, glFlush | correct ordering and timeouts honoured |
| gl.swap.interval | 0, 1, 2, -1 | intervals measured match (4.G) |
| gl.external_objects | EXT_memory_object_win32 and EXT_semaphore_win32 | imports from VK/D3D work (also 4.F) |
| gl.extensions.pin | GL_EXTENSIONS and WGL extensions | equal to the pinned list for the host |
| gl.multithread | contexts current on several threads | no cross-talk |
| gl.lost.recreate | robustness and context reset | reset reported, new context works |

#### OpenCL

| check | does | passes when |
|---|---|---|
| cl.platform.devices | platform, device info, versions, extensions | match the pinned table |
| cl.mem.flags_matrix | READ_WRITE/READ_ONLY/WRITE_ONLY x USE_HOST_PTR/ALLOC_HOST_PTR/COPY_HOST_PTR x HOST_* flags | data exact; USE_HOST_PTR coherent per spec above lent_minimum; the silent copy below it is reported |
| cl.mem.sub_buffers | sub-buffers at aligned offsets | views correct |
| cl.transfers.all | read/write/copy buffer and rect, image read/write/copy, fill, map/unmap blocking and not | exact |
| cl.images.all | 1D/2D/3D/arrays, from buffer, every supported format | exact, including 32-bit cl_image_desc decoding |
| cl.svm.coarse | coarse-grain SVM alloc, map, use | exact; fine grain reported unsupported |
| cl.kernels.compute | a battery of kernels (reductions, atomics, local memory, barriers, doubles when offered) | CPU reference equal |
| cl.programs.forms | source, IL (SPIR-V), binaries, build options, errors | build and logs work; bad source fails with a log |
| cl.events.ordering | in-order and out-of-order queues, user events, wait lists, callbacks, profiling | ordering and profiling timestamps consistent |
| cl.gaps | create_sub_devices, program release callback | fail with the declared codes |
| cl.x86.image_desc (x86-only) | image creation with cl_image_desc | correct (known bug today) |

#### Vulkan

| check | does | passes when |
|---|---|---|
| vk.instance.device | instance, physical device, 1.3, features, properties, limits | match the pinned table for the host |
| vk.memory.types_matrix | allocate every memory type, map host-visible ones, bind buffers and images | exact; host-visible types map; budget honoured |
| vk.memory.external | export/import win32 handles (memory and semaphores), external host memory | round trip exact |
| vk.transfers | copy buffer/image both ways, blit, resolve, clear, fill, update | exact |
| vk.compute | compute shaders against CPU references | exact |
| vk.graphics | render passes, dynamic rendering, pipelines | analytic or D3D11-twin equal |
| vk.sync | fences, binary and timeline semaphores, events, barriers, WaitSemaphores, QueueWaitIdle | no early signal, no deadlock |
| vk.queries | timestamp, occlusion, pipeline statistics | consistent |
| vk.wsi | surface caps, FIFO and IMMEDIATE, image counts 2 to 8, extent equal to the window, resize | presents in order (4.G); out-of-date handled |
| vk.lost | device loss | VK_ERROR_DEVICE_LOST surfaced, a new device works |
| vk.validation | run selected checks under the validation layer | no validation errors from the ICD's own behaviour |

### 4.F Interop

Every pair is checked in both directions, with content written on one side and verified on the other, with the
synchronisation the API pair requires, within one process and across processes, and across bitness where the
handle type allows it (32-bit producer, 64-bit consumer).

#### GDI

| check | does | passes when |
|---|---|---|
| gdi.dxgi.getdc | IDXGISurface1::GetDC on GDI_COMPATIBLE textures and back buffers, draw text and lines, ReleaseDC, present | GDI pixels appear where drawn, D3D content intact around them |
| gdi.d3d9.getdc | IDirect3DSurface9::GetDC on render targets and back buffers | same |
| gdi.gl.ownDC | GDI drawing into a CS_OWNDC GL window, before and after SwapBuffers | defined result: GL frame then GDI overlay as Windows composes it |
| gdi.bitblt.from_d3d | BitBlt from a window showing D3D11/D3D9/GL/VK content | captured pixels equal the presented frame |
| gdi.printwindow | PrintWindow with PW_RENDERFULLCONTENT on each API's window | equals the presented frame |
| gdi.layered | UpdateLayeredWindow and SetLayeredWindowAttributes over D3D content, per-pixel alpha | DWM composition matches the expected blend |
| gdi.dib.section | CreateDIBSection, GDI draw, upload to D3D/GL textures | exact |
| gdi.redirection | GDI-only windows next to flip-model windows | both correct in captures |
| gdi.d2d | Direct2D on a DXGI surface (D3D11 interop), DirectWrite text, mixed with GDI via GetDC | equal to WARP D2D |
| gdi.cursor.capture | screen capture including the cursor | cursor drawn where expected |

#### GL and CL

| check | does | passes when |
|---|---|---|
| clgl.buffer | clCreateFromGLBuffer, acquire, kernel writes, release, GL reads | exact |
| clgl.texture | clCreateFromGLTexture for 2D, 3D, arrays, cube faces, mip levels | exact |
| clgl.renderbuffer | clCreateFromGLRenderbuffer | exact |
| clgl.sync.event | cl_khr_gl_event and glFinish-free ordering | no stale reads |
| clgl.paths | direct and copied sharing paths (force each) | both exact |
| clgl.context.props | CL context from a GL context (CL_GL_CONTEXT_KHR, CL_WGL_HDC_KHR), clGetGLContextInfoKHR | the right device is chosen |

#### D3D and CL

| check | does | passes when |
|---|---|---|
| d3dcl.d3d11.buffer | cl_khr_d3d11_sharing buffers | exact both ways |
| d3dcl.d3d11.texture | 2D and 3D textures, subresources | exact both ways |
| d3dcl.d3d10 | cl_khr_d3d10_sharing | exact both ways |
| d3dcl.dx9.media | cl_khr_dx9_media_sharing (if offered; otherwise reported unsupported) | exact or cleanly unsupported |
| d3dcl.acquire_release | acquire/release ordering with D3D work in flight | no stale or torn data |

#### GL and D3D

| check | does | passes when |
|---|---|---|
| gld3d.nv_dx_interop2 | WGL_NV_DX_interop2: open device, register D3D11 and D3D9Ex textures and render targets, lock/unlock | exact both ways |
| gld3d.lock_order | lock/unlock ordering under load | no stale frames |

#### D3D and D3D

| check | does | passes when |
|---|---|---|
| d3d.9ex_to_11 | D3D9Ex shared surface opened in D3D11 and back | exact (the WPF composition path) |
| d3d.11_to_12 | NT-handle textures and fences between D3D11 and D3D12 | exact, fences order work |
| d3d.11on12 | D3D11On12 wrapping | renders correctly |
| d3d.cross_process | shared handles between processes (DuplicateHandle, named NT handles) | exact |
| d3d.cross_adapter | render-only adapter rendering, display adapter presenting (cross-adapter allocations) | exact |
| d3d.cross_bitness | a 32-bit producer and a 64-bit consumer of a shared texture | exact |

#### Vulkan with the others

| check | does | passes when |
|---|---|---|
| vkd3d.import_texture | import D3D11/D3D12 shared textures into Vulkan | exact |
| vkd3d.export_memory | export Vulkan memory to D3D12 | exact |
| vkd3d.semaphores | D3D12 fences as Vulkan timeline semaphores and back | ordering held |
| vkgl.memory | EXT_memory_object_win32 import of Vulkan memory into GL | exact |
| vkgl.semaphores | EXT_semaphore_win32 | ordering held |
| vkcl | CL import of Vulkan memory when an extension is offered | exact or cleanly unsupported |

#### Capture APIs (also interop)

| check | does | passes when |
|---|---|---|
| capture.wgc | Windows.Graphics.Capture of each API's window | frames equal the presented frames, in order |
| capture.duplication | Desktop Duplication | frames equal the screen, dirty rects correct |
| capture.dwm_thumbnail | DWM thumbnails of each API's window | equal within scaling tolerance |

### 4.G Presentation and frame pacing

Every presented frame carries its frame number encoded in a corner block of pixels (and a test pattern elsewhere),
so ordering, drops and duplicates can be read back from any capture.

| check | does | passes when |
|---|---|---|
| frames.order.per_api | 300 frames per present path (D3D9, D3D9Ex, DXGI blt/flip/discard, D3D12, GL, VK FIFO/IMMEDIATE) captured with WGC | numbers strictly increasing; FIFO: no drops, no duplicates |
| frames.content | the same frames | test pattern exact (no stale buffer, no swapped buffers) |
| frames.timing.vsync [E] | FIFO and interval 1 for 600 frames | median interval equals the refresh period, p99 within 1.5x |
| frames.timing.interval2 [E] | interval 2 | twice the period |
| frames.timing.immediate [E] | interval 0 / IMMEDIATE | faster than the refresh, no pacing stalls |
| frames.latency.waitable [E] | frame latency waitable object | input-to-present latency within the configured frames |
| frames.stats.consistency | DXGI frame statistics, VK present ids, GL swap counts | agree with captured frame numbers |
| frames.resize.during | resize while presenting | no wrong-size frame after the resize settles |
| frames.occlusion | minimise, occlude, restore | presents return the occluded status, resume correctly |
| frames.fullscreen [E] | exclusive fullscreen D3D9 and DXGI | frames in order, mode restored after |
| frames.multi_window | 8 windows presenting at once, different APIs | each window's sequence intact |
| frames.mirror.host (host phase) | the same sequences through the window mirror | the host side receives the same frame numbers in order |
| frames.mirror.maximized [E] | maximise, snap and restore mirrored windows | guest rect equals the maximized rect (the regression fixed in remotewin 6507de8) |
| frames.display.cycle [E] | watch the driver log over 10 s of idle | no periodic visibility churn (the DisplayCore loop) |

### 4.H Synchronisation and concurrency

| check | does | passes when |
|---|---|---|
| sync.cpu_gpu.readback_race | CPU reads right after GPU writes on every API | never stale |
| sync.timeouts | waits with short timeouts on busy work | time out correctly, then complete |
| sync.many_threads | 16 threads each with its own device or context | results exact, no deadlock |
| sync.many_processes | 16 processes using the GPU at once (the parallel runner itself is a second layer) | results exact, host contexts freed |
| sync.cross_api_fences | D3D12 fence to VK semaphore to GL semaphore chains | order kept |

### 4.I Robustness and errors

| check | does | passes when |
|---|---|---|
| err.invalid_args | invalid parameters on every creation call | the documented error, never a crash or host error |
| err.known_gaps | every E_NOTIMPL and unsupported area listed in the per-API sections | fails cleanly and caps agree |
| err.device_loss [E] | injected device loss (the gpu-test hook) | every API reports loss; new devices work; the desktop recovers |
| err.tdr [E] | a shader that runs past the TDR timeout | Windows resets the adapter, apps recover |
| err.oom.guest [E] | guest memory pressure while allocating | clean failure |
| err.process_crash [E] | crash a process holding many resources | host resources freed (host phase) |
| err.handles | create and close handles in loops | handle counts back to baseline |

### 4.J Lifecycle

| check | does | passes when |
|---|---|---|
| life.dwm_restart [E] | restart DWM with apps presenting | presents resume; mirrors keep working |
| life.display_mode [E] | change the display mode while presenting | apps resize or recreate, frames correct |
| life.agent_restart [E] | restart the guest agent | mirrors and launches recover |
| life.driver_update [E] | live driver update path (only on request; slow) | apps survive or fail cleanly as declared |
| life.sleep [E] | S3/hibernate if the VM allows | scanout and contexts restored |

### 4.K Bitness

Every check runs in both builds. Additional x86-only checks:

| check | does | passes when |
|---|---|---|
| x86.wire.layouts | each API call whose wire record carries pointers or size_t | values decoded right on the host |
| x86.address_space | arena and slice mapping near the 2/4 GiB limits (LARGEADDRESSAWARE on and off) | clean failure, no corruption |
| x86.shared.handles | 32-bit handles in sharing records | open correctly in 64-bit consumers |

### 4.L Formats

| check | does | passes when |
|---|---|---|
| fmt.support.tables | every format per API: sample, render, blend, UAV, MSAA, display | equal to the host's answers |
| fmt.roundtrip | upload and read back each supported format, including BC1-7, packed (R10G10B10A2, R11G11B10, R9G9B9E5), sRGB, depth/stencil, YUV (NV12, P010, YUY2) | exact or the format's defined precision |
| fmt.conversion | sRGB writes and reads, normalisation, typeless casts | equal to CPU reference |

### 4.M Shaders and compilation

| check | does | passes when |
|---|---|---|
| shader.dxbc | DXBC from FXC for SM 2 to 5 | compiles in the driver, runs correctly |
| shader.dxil | DXIL from DXC for SM 6.x | same |
| shader.spirv | SPIR-V for VK and GL_ARB_gl_spirv, CL IL | same |
| shader.glsl | GLSL compiled in the guest, every version | same; errors give logs |
| shader.cache | the same pipelines created twice and across processes | second creation faster; identical results |

### 4.N Host invariants (judged by gpu_matrix around every run)

| check | does | passes when |
|---|---|---|
| host.executor.alive | executor and device dumps answer | no crash, no faulted context |
| host.leaks.blobs | host blob count before and after | back to baseline (with a settle time) |
| host.leaks.contexts | device contexts and executor channels | back to baseline |
| host.window.pages | pages_in_use | back to baseline after the arena idle release |
| host.refusals | refused_no_space, refused_no_memory, refused_other | zero except in checks that provoke them |
| host.stalls | avinspector stalls over the run | none above the stall threshold |
| host.driver_log | avgpu.log delta | no failure lines outside declared ones |
| host.qemu_log | QEMU log delta | no errors (aio, device errors) |
| host.stats.agree | ChannelStats against the guest's own provoked counts | equal |

### 4.O LSW stack

| check | does | passes when |
|---|---|---|
| lsw.mirror.launch_first | first launch of a self-relaunching app (the Firefox pattern, with a test app that de-elevates through Explorer) | the mirror follows the relaunch |
| lsw.mirror.capture_each_api | a window per API mirrored | host frames match guest frames |
| lsw.status.gpu | GPU status data (submit rates, arena, splits) against provoked activity | the numbers the UI shows equal the provoked ones |

### 4.P Regression list

Every fixed bug gets a check that would have caught it. Current entries:

| check | bug |
|---|---|
| reg.cl_buffer_ref | CL buffer reference fix (c31218d2) |
| reg.d3d12_caps_enum | d3d12_capabilities enum memcpy |
| reg.split_extra_semantics | EXTRA computed as pieces minus splits |
| reg.piece_cap_32m | 4 MiB upload cap splitting full-screen uploads |
| reg.display_core_cycle | DWM DisplayCore off-loop |
| reg.mirror_maximize | maximized window shifted to the drop point |
| reg.first_launch_relaunch | first launch of a de-elevating app not mirrored |
| reg.gl_core_options | GL core contexts failing after the protocol minor gate |
| reg.slice_group_overflow | SliceGroups invariant overflow |
| reg.host_registry_overflow | HostObjectRegistry make_room overflow |

## 5. Benchmark suite (later)

- Policy `serial`; refuses to run next to any other `lsw-suites` instance.
- Reuses the sanity helpers; checks report `measures` only (no pass/fail except a sanity bound).
- Candidates: upload and readback bandwidth per path and size, submit latency, present latency and pacing
  jitter, arena growth time, shader compile time, the D3D11 sample workloads.
- The matrix compares runs and GPUs and flags regressions beyond a per-measure tolerance.

## 6. Order of work

1. Skeleton: the executable, registry, workers, policies, output, JSON, both bitnesses, `build-suites.sh`,
   the `gpu_matrix.py` suite. One real check (`env.adapters.present`) proves the path end to end.
2. 4.A environment, 4.N host invariants, 4.C transport, 4.D memory.
3. 4.E per API, in this order: D3D11 and DXGI, D3D9, OpenGL, OpenCL, Vulkan, D3D12, D3D10.
4. 4.F interop, GDI first.
5. 4.G presentation and pacing, 4.H sync.
6. 4.I robustness, 4.J lifecycle, 4.K bitness, 4.L formats, 4.M shaders, 4.O LSW, 4.P as bugs are fixed.

Existing tests stay where they are: accel-virt unit tests, fuzz targets and probes cover units and bring-up;
this suite covers behaviour as an application sees it, on real GPUs.
