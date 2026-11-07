# LSW suites

One guest executable, `lsw-suites`, holds every suite. A suite is a folder under `suites/` and a CMake library
linked into that executable. The first suite is `sanity`: contract checks for the whole stack, from the
kernel driver to window capture, run on every GPU in the `scripts/gpu-test` matrix, in 64-bit and 32-bit builds.
A later `benchmark` suite shares the runner but never runs in parallel.

The goal of `sanity` is that any regression an application could notice fails a named check, on a named GPU,
with an artifact that shows what went wrong. Checks assert contracts (section 4.0), never how we implement them,
so they stay valid through redesigns.

---

## 1. Layout and build

```
suites/
  CMakeLists.txt        standalone project lsw_suites; target lsw-suites (the executable)
  PLAN.md               this file
  core/                 target suite_core: runner, registry, workers, report, check helpers
  sanity/               target suite_sanity: the sanity checks
  selftest/             target suite_selftest: runner self-checks (serial policy)
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
- `scripts/build-suites.sh` builds both bitnesses into `build-suites/` and `build-suites32/` (overridable with
  `LSW_SUITES_BUILD` and `LSW_SUITES32_BUILD`), with Ninja, ccache and `CMAKE_BUILD_PARALLEL_LEVEL`.

## 2. The executable

### Command line

| option | meaning |
|---|---|
| `--list` | print every check id, its suite, its tags and its policy, then exit |
| `--suite NAME[,NAME]` | run these suites (default: `sanity`) |
| `--filter GLOB[,GLOB]` | only checks whose id matches; `-GLOB` excludes |
| `--exclude-file PATH` | read one check id or glob per line (blank lines ignored) and exclude those checks, in addition to `--filter`; for long held-row lists that do not fit a cmd.exe line |
| `--tag TAG` | only checks with a tag (`d3d11`, `interop`, `exclusive`, `x86-only`, `slow`...) |
| `--phase all\|main\|pacing` | `main` runs every check without the `pacing` tag, `pacing` only those; default `all` |
| `--jobs N` | worker processes for parallel suites (default: logical CPUs, capped at 16) |
| `--timeout S` | per-check timeout (default per check, 60 s if none) |
| `--repeat N` | run every selected check N times (flake hunting) |
| `--shuffle SEED` | randomise order within each phase (parallel, exclusive, pacing) |
| `--json FILE` | write the result file (schema in 2.4); the per-check log goes next to it with the extension `.jsonl` (`r64.json` gives `r64.jsonl`) |
| `--artifacts DIR` | where failing checks drop images, buffers and logs (default `.\lsw-suites-artifacts`) |
| `--run-one ID` | internal: run one check in this process (used by workers) |

### Parallelism

- A suite declares a policy:
  - `parallel` (sanity): checks run in up to `--jobs` worker processes at once;
  - `serial` (benchmark): one check at a time whatever `--jobs` says, and the runner refuses to start it while
    another `lsw-suites` instance is running, so benchmark numbers never share the GPU. Every run opens
    `%ProgramData%\lsw-suites.lock`: parallel-only runs with shared read, runs with a serial suite exclusively;
    the kernel drops the lock when a process dies. A refused run prints `refusing to start` and exits 2.
- A check can declare `exclusive`. Exclusive checks of a parallel suite run after the parallel phase, one at a
  time, with no other worker alive of the same run; the 32-bit and 64-bit runs on one machine take the named
  mutex `Global\lsw-suites-exclusive-check` around each exclusive check, so their exclusive checks never overlap.
  Windows of non-exclusive checks are parked beyond the virtual desktop and never topmost, so they cannot disturb
  another run's exclusive screen checks. Exclusive is for anything that changes or needs quiet global state:
  device-loss injection, TDR, memory exhaustion, display mode changes, DWM restarts, frame pacing and timing
  measurements, power transitions, driver log scans.
- A check tagged `pacing` is one whose verdict depends on presentation, capture, frame order or frame timing.
  The run is ordered: parallel checks, exclusive checks, serial-suite checks, then all pacing checks last. Pacing
  checks always run one at a time, exclusive or not, each under the same machine-wide mutex; `--phase main`
  and `--phase pacing` run the two halves separately (`gpu_matrix.py` runs `main` for both bitnesses, then
  `pacing` for x64 and x32 one after the other).
- Every check runs in its own worker process: the executable starts itself with `--run-one ID`, suspended, in a
  job object with kill-on-close. A crash, a hang, a device loss or a leaked handle then fails one check, not the
  run; the parent enforces the timeout and kills the whole tree. The child reports through `@@lsw status|reason|
  measure|artifact|note` lines on its stdout; no status line means `crash` (with the exception code when there
  is one). Captured output is kept in the result, capped at 4 MiB.
- Output is deterministic under parallelism: lines are printed in registry order once a check finishes, not in
  completion order; the JSON keeps start time, duration, worker pid and exit cause.

### Output

The same lines the existing probes print, so `gpu_matrix.py` and `run_guest_suite.py` parse them unchanged:

```
ok sanity.d3d11.map.no_overwrite_in_flight 12ms
FAIL sanity.gl.buf.persistent_coherent_visibility status=fail 41ms
note sanity.gl.buf.persistent_coherent_visibility word 1024 read 0x00000000, expected 0xa5a5a5a5
lsw-suites64: 652 passed, 2 failed, 8 skipped
```

- Exit code: 0 all passed, 1 some check failed, 2 the runner itself failed (bad arguments, no adapter).
- `skip` is used only for a declared reason (the host GPU lacks a feature, the check is x86-only); a skip
  without a reason is a failure.

### Result file

```
{ "exe": "lsw-suites64", "bitness": 64, "adapter": {...}, "driver": {...}, "started": "...",
  "complete": true, "summary": { "passed": 652, "failed": 2, "skipped": 8 },
  "checks": [ { "id": "...", "suite": "sanity", "tags": [...], "status": "ok|fail|skip|timeout|crash",
                "reason": "...", "ms": 12, "pid": 1234, "artifacts": ["..."], "measures": {...} } ] }
```

The file is rewritten while the run progresses (at most every few seconds and once at the end) with `complete`
false until the run finishes, so a crashed run still leaves its finished checks and counts; the per-check
`.jsonl` log stays the exact record.

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

- `gpu_matrix.py` has a suite `suites` (in the default list). For each GPU it pushes `lsw-suites64.exe` and
  `lsw-suites32.exe` to `C:\av\gputest`, runs both in the console session, pulls the text, the JSON and a zip of
  the artifacts, and judges: a check fails the GPU if it failed in either bitness; a missing JSON, a runner exit
  other than 0 or 1, or zero checks run is a failure. `gpu-test.sh build` builds both, `deploy` ships both.
- `--suite-args "..."` passes options through (`--suite sanity --filter 'sanity.d3d11.*' --jobs 8`).
- Host phase (to build). Around each run the matrix records host state and judges host invariants (section 4.24):
  executor and device state dumps before and after, the driver log (`avgpu.log`) delta, the QEMU log delta,
  avinspector stalls. A run that passes in the guest but leaks host objects or logs driver errors fails.
- `aggregate.py` reports `suites` checks like the others: "on every GPU" against "this GPU only", and 64 against
  32-bit.
- Results stay comparable over time: the matrix keeps the last green JSON per GPU and flags checks that went from
  ok to fail, and `measures` that moved past a per-check bound.

## 4. The sanity catalogue

664 checks in 26 areas, plus white-box diagnostics recorded as measures. Ids are `sanity.<area>.<group>.<name>`;
`[E]` marks exclusive checks; `(x86)` and `(x64)` mark bitness-specific ones; every other check runs in both
builds. Area counts are in the headings.

### 4.0 What a check may assert

- **Contracts, never our implementation.** A check asserts what an application is entitled to rely on: the API
  specifications (D3D9 to D3D12 and DXGI, OpenGL 4.6 and WGL extensions, OpenCL 3.0 and extensions, Vulkan 1.3),
  documented Windows, WDDM and DWM behaviour, or invariants that hold for any design: integrity, ordering,
  isolation, liveness (waits complete or time out as documented, nothing deadlocks), reclamation (everything a
  process held is released when it exits, in the guest and on the host), error contracts (documented codes, the
  device usable afterwards) and consistency (two ways of asking the same thing agree; caps agree with behaviour).
- **No internals in pass criteria.** Slices, arenas, rings, transfer pieces, lent pages, internal counters, the
  path a transfer took or our log lines never decide a check. Where an internal mechanism is the risk, a check
  stresses it from outside (sizes, pressure, concurrency, timing) and asserts the contract. Internal observations
  go to 4.27 as measures.
- **Caps first.** An optional feature is queried before use: if supported, its full contract must hold; if not,
  every way of asking must agree it is unsupported and using it must fail the documented way.
- **Oracles**, in order: analytic expected values; a CPU reference computed in the check; WARP in the same guest
  within a stated tolerance; a twin in another API; stored references only as a last resort.
- **Deterministic and quick**: seconds per check, except exclusive timing checks, which state their length.

### 4.1 Environment and installation (15)

#### Install state

| check | contract |
|---|---|
| `sanity.env.install.adapter_started` | Every display adapter listed by SetupAPI for the display class reports a started device with no problem code; the check enumerates the class and reads the device status. |
| `sanity.env.install.driver_version_consistent` | The driver version reported by SetupAPI, by DXGI (CheckInterfaceSupport / adapter desc) and by the version resource of the user-mode driver DLL agree in every field they share. |
| `sanity.env.install.umd_files_present_both_bitnesses` | Every user-mode driver DLL named by the adapter's registry entries (UserModeDriverName, UserModeDriverNameWow) exists and loads in a process of the matching bitness. |
| `sanity.env.install.signed_and_loadable` | The driver package is accepted by the OS as a signed package: loading each user-mode driver DLL and querying the adapter raise no signature or code-integrity error in the event log during the run. |
| `sanity.env.install.no_duplicate_adapters` | The number of adapters the OS reports equals the number of adapter devices in the device tree; no adapter appears twice and none is a ghost entry. |
| `sanity.env.install.requirements_hold` | Every guest requirement an application may rely on (documented display and compositor settings) reads back as required when queried through the documented registry and API paths. |

#### ICD registration

| check | contract |
|---|---|
| `sanity.env.icd.opencl_registry_x64` | The OpenCL ICD registry key for 64-bit lists a library that exists, loads, and exposes a platform through clGetPlatformIDs with a non-empty name, vendor and version. (x64) |
| `sanity.env.icd.opencl_registry_x86` | The WoW64 OpenCL ICD registry key lists a 32-bit library that exists, loads in a 32-bit process, and exposes a platform with the same name and vendor as the 64-bit one. (x86) |
| `sanity.env.icd.vulkan_manifest_both` | The Vulkan loader finds a driver manifest for each bitness whose library path exists; vkEnumerateInstanceVersion and vkEnumeratePhysicalDevices succeed with at least one device in both. |
| `sanity.env.icd.opengl_icd_both` | The OpenGL ICD registered for the adapter loads in both bitnesses: a legacy context created on a window DC reports a vendor and renderer string that are non-empty and identical across bitnesses. |
| `sanity.env.icd.loader_independent_of_cwd` | ICD discovery gives identical results when the process is started from a different working directory and under a different user token level. |

#### Display outputs

| check | contract |
|---|---|
| `sanity.env.display.output_present` | EnumDisplayDevices and DXGI EnumOutputs both report at least one attached output that is part of the desktop, and each reports the same device name set. |
| `sanity.env.display.output_active_mode` | For every attached output EnumDisplaySettings(ENUM_CURRENT_SETTINGS) returns a mode with nonzero width, height, bits-per-pixel and frequency, and that mode appears in the output's enumerated mode list. |
| `sanity.env.display.desktop_rect_consistent` | The virtual-screen metrics equal the union of the output rectangles reported by monitor enumeration, and each monitor's work area lies inside its rectangle. |
| `sanity.env.display.dpi_scaling_consistent` | The effective DPI reported per monitor by the shell scaling API matches the ratio of the logical to the physical size the same API reports, and is stable across calls. |

### 4.2 Adapter and device enumeration (14)

#### Cross-API identity

| check | contract |
|---|---|
| `sanity.enum.identity.dxgi_vs_kmt_luid` | The set of adapter LUIDs from DXGI EnumAdapters1 equals the set from D3DKMTEnumAdapters2, excluding software adapters. |
| `sanity.enum.identity.vulkan_luid_matches` | For each Vulkan physical device with LUID validity set, the LUID matches a DXGI adapter, and the device and vendor ids equal the DXGI description of that adapter. |
| `sanity.enum.identity.opencl_luid_matches` | If cl_khr_device_uuid / LUID info is exposed, the OpenCL device LUID matches a DXGI adapter; if not exposed, the check asserts the property is reported as unsupported consistently. |
| `sanity.enum.identity.gl_renderer_matches` | The GL_RENDERER string on a context bound to a window on each output names the adapter description DXGI reports for the adapter that drives it, or a documented superset of it. |
| `sanity.enum.identity.setupapi_matches` | Hardware ids from SetupAPI contain the vendor and device ids that DXGI reports for the same adapter, found through the adapter's registry key. |
| `sanity.enum.identity.names_nonempty_printable` | Adapter descriptions from every API are non-empty, null-terminated within bounds, free of control characters and not truncated differently between APIs. |

#### Consistency and stability

| check | contract |
|---|---|
| `sanity.enum.stable.repeat_calls` | One hundred repeated enumerations through each API return the same adapters in the same order with the same descriptors. |
| `sanity.enum.stable.across_processes` | Eight processes enumerating at once, and then again after one of them exits, return identical adapter lists, LUIDs and memory sizes. |
| `sanity.enum.stable.across_bitness` | The 32-bit and 64-bit enumerations agree on LUIDs, vendor ids, device ids and display outputs. |
| `sanity.enum.stable.adapter_order_matches_primary` | The adapter that drives the primary output is enumerated consistently by DXGI in both bitnesses, and the output list of each adapter is identical in both. |
| `sanity.enum.stable.create_destroy_enumeration` | Enumeration while another thread creates and destroys devices on the same adapter never returns an error or a partial list. |

#### Memory and capability reporting

| check | contract |
|---|---|
| `sanity.enum.memory.dedicated_vs_shared_consistent` | DXGI dedicated and shared memory sizes, D3DKMT segment sizes and Vulkan heap sizes describe the same adapter within a stated tolerance, and no value is zero for an adapter that is not software. |
| `sanity.enum.memory.opencl_global_size` | CL_DEVICE_GLOBAL_MEM_SIZE does not exceed the sum of the heaps Vulkan and DXGI report for the same adapter, and CL_DEVICE_MAX_MEM_ALLOC_SIZE does not exceed the global size. |
| `sanity.enum.caps.feature_level_vs_api_support` | The maximum feature level D3D11 reports is consistent with what D3D12 and Vulkan report for the same adapter (a device that supports a D3D12 feature level supports the lower D3D11 ones). |

### 4.3 Kernel-mode contracts (36)

Implementation note: query, display and escape rows call the documented D3DKMT entry points. The allocation and synchronisation rows exercise the same kernel objects through D3D11 and D3D12 (Map and Unmap for Lock and Unlock, D3D12 fences for monitored fences, CreateSharedHandle for shared allocations), because the D3DKMT creation entry points need a WDDM device the user-mode runtimes already own. The recovery rows reuse the err and life rows of the same contract, with the Basic Render adapter as the other adapter.

#### QueryAdapterInfo

| check | contract |
|---|---|
| `sanity.kmd.query.adapter_info_stable` | Every documented D3DKMTQueryAdapterInfo type that succeeds returns the same bytes on repeated calls and from separate processes, apart from fields documented as dynamic. |
| `sanity.kmd.query.size_semantics` | Querying with a buffer smaller than required fails with a documented status and writes nothing past the size given; a larger buffer succeeds and the extra bytes are untouched. |
| `sanity.kmd.query.unsupported_type_status` | Querying an out-of-range or unsupported info type returns a failure status and never crashes or changes later answers. |
| `sanity.kmd.query.segment_info_consistent` | Segment descriptors are self-consistent: sizes nonzero, commit limits not above segment size, aperture flags matching the memory heaps DXGI reports for the same adapter. |
| `sanity.kmd.query.driver_caps_self_consistent` | WDDM caps report a version at or above 2.0 and the supported scheduling, preemption and flip-model capabilities agree with the DXGI-reported capabilities (e.g. flip model present support). |
| `sanity.kmd.query.open_adapter_from_hdc_and_name` | D3DKMTOpenAdapterFromHdc, FromGdiDisplayName and FromLuid return the same adapter handle identity (same LUID) for the same output. |
| `sanity.kmd.query.close_adapter_twice` | Closing an adapter handle twice returns a failure the second time without disturbing other handles or processes. |

#### Allocation and lock semantics

| check | contract |
|---|---|
| `sanity.kmd.alloc.lock_returns_written_data` | Data written through a locked CPU-visible allocation is read back unchanged after unlock and re-lock, for sizes from one page upward. |
| `sanity.kmd.alloc.lock_twice_status` | Locking an allocation that is already locked either succeeds with a consistent pointer or fails with the documented status; unlock of an unlocked allocation fails. |
| `sanity.kmd.alloc.lock_pointer_stable_while_locked` | The pointer returned by Lock stays valid and writable for as long as the lock is held, including across other GPU work submitted on the device. |
| `sanity.kmd.alloc.discard_and_nooverwrite` | Locking with the discard flag returns a valid pointer whose previous content is unspecified but whose neighbours in other allocations are untouched. |
| `sanity.kmd.alloc.destroy_while_locked` | Destroying an allocation whose lock is outstanding either fails with a documented status or releases it cleanly; later allocations still work. |
| `sanity.kmd.alloc.nt_handle_share` | An allocation shared with an NT handle opens in a second process, shows the same contents after writes from either side, and the second process can read after the first has closed its handle. |
| `sanity.kmd.alloc.nt_handle_wrong_adapter` | Opening a shared handle on a different adapter than it was created for fails with a documented status or yields a usable object with identical content. |
| `sanity.kmd.alloc.share_requires_handle` | A shared allocation cannot be opened by guessing the numeric handle value; only duplicated or inherited handles succeed. |

#### Display source and scanout

| check | contract |
|---|---|
| `sanity.kmd.display.vblank_wait_cadence` | D3DKMTWaitForVerticalBlankEvent returns at a mean period within a stated tolerance of the current mode's refresh period over one second, with no wait longer than five periods. [E] |
| `sanity.kmd.display.scanline_monotonic` | Successive D3DKMTGetScanLine samples inside one frame are non-decreasing until the vblank flag is seen, and are within the active height plus blanking. [E] |
| `sanity.kmd.display.vblank_vs_dwm_timing` | The refresh rate seen through DwmGetCompositionTimingInfo matches the vblank wait cadence within tolerance. [E] |
| `sanity.kmd.display.source_visible_while_composed` | While the desktop is composed, D3DKMTCheckVidPnExclusiveOwnership and the display source state report the source visible, and a screenshot of the desktop is not uniformly black. |
| `sanity.kmd.display.mode_set_restore` | Changing the display mode via ChangeDisplaySettingsEx to every enumerated mode, and back, succeeds, reports the new size through GetSystemMetrics, and restores the original mode and desktop layout exactly. [E] |
| `sanity.kmd.display.mode_list_all_settable` | Every mode enumerated for an output, up to a bounded sample, is accepted by CDS_TEST, and a mode not in the list is rejected with DISP_CHANGE_BADMODE. [E] |
| `sanity.kmd.display.cursor_shapes` | Monochrome, colour and alpha-blended cursor shapes of 32, 48 and 64 pixels created by CreateCursor and set by SetCursor are accepted, and GetCursorInfo reflects the cursor shown. |
| `sanity.kmd.display.cursor_position_roundtrip` | SetCursorPos followed by GetCursorPos returns the position set, including negative and off-screen coordinates clamped to the desktop. |
| `sanity.kmd.display.gamma_ramp_roundtrip` | A gamma ramp that the output reports as settable reads back within one quantisation step; a non-monotonic ramp is rejected the documented way. [E] |
| `sanity.kmd.display.output_duplication_frame` | IDXGIOutputDuplication delivers a frame whose pixels equal a known pattern drawn in a topmost window, within tolerance, when the window is on the duplicated output. [E] |

#### Escapes and validation

| check | contract |
|---|---|
| `sanity.kmd.escape.malformed_rejected` | D3DKMTEscape with null, oversized, undersized and garbage private data returns a failure status without crashing the process, the system, or another process. |
| `sanity.kmd.escape.unknown_command_status` | An escape with an unrecognised command value fails with a documented status and has no side effect on subsequent rendering. |

#### Synchronisation objects

| check | contract |
|---|---|
| `sanity.kmd.sync.monitored_fence_monotonic` | A monitored fence's observed value never decreases across any sequence of signals, waits and queries from multiple threads. |
| `sanity.kmd.sync.wait_releases_on_signal` | A wait on a fence value is released after the GPU or CPU signals that value, within a bounded time, and not before the value is reached. |
| `sanity.kmd.sync.wait_timeout` | A wait with a finite timeout on an unsignalled fence returns the documented timeout result within the requested time plus a stated slack. |
| `sanity.kmd.sync.shared_fence_across_processes` | A fence shared by NT handle signalled in one process releases a wait in another, and values observed on both sides agree. |
| `sanity.kmd.sync.cpu_signal_visible_to_gpu` | A value signalled by the CPU on a fence releases a GPU wait queued earlier, and work after the wait observes the memory written before the signal. |

#### Recovery and power

| check | contract |
|---|---|
| `sanity.kmd.recovery.tdr_restores_service` | After a GPU hang long enough to trigger timeout detection and recovery, the device is reported removed or reset with a documented reason, and a new device on the same adapter then renders and computes correctly. [E] |
| `sanity.kmd.recovery.tdr_other_adapter_unaffected` | A recovery on one adapter leaves a device on the other adapter running work correctly throughout. [E] |
| `sanity.kmd.recovery.power_transition_contents` | After the adapter goes through a documented power transition and resumes, resources created before it contain the same data, or the device is reported lost with a documented reason. [E] |
| `sanity.kmd.recovery.query_status_after_reset` | D3DKMTQueryStatistics-free status calls (adapter present, removed reason) report coherent values after a recovery and do not hang. [E] |

### 4.4 Data integrity (23)

All checks fill with a position-dependent pattern computed on the CPU and verify every byte of the readback. Sizes cover 1, 2, 3, 4, 7, 15, 16, 17, 255, 256, 4095, 4096, 4097, 65535, and each of 3.9, 4, 4.1, 15.9, 16, 16.1, 31.9, 32, 32.1, 63.9, 64, 64.1, 255.9, 256, 256.1 MiB where the API allows it.

#### Boundary sizes and offsets

| check | contract |
|---|---|
| `sanity.data.size.buffer_roundtrip_small` | Buffers from 1 byte to 1 MiB (every size up to 600, then selected sizes) written by the CPU through a staging path arrive intact on the GPU and return intact, in D3D11, D3D12, Vulkan and OpenCL. |
| `sanity.data.size.buffer_roundtrip_boundaries` | Buffers of the boundary sizes around 4, 16, 32, 64 and 256 MiB, each exactly at, one byte below and one byte above, roundtrip intact. |
| `sanity.data.size.buffer_roundtrip_hundreds_mib` | Buffers of 300 to 700 MiB (as far as the API reports available) roundtrip intact, or the allocation fails cleanly. [E] |
| `sanity.data.size.texture_upload_sizes` | Textures from 1x1 to 16384 wide or tall (as far as caps allow) in 1-, 4-, 8- and 16-byte formats upload and read back intact for odd widths and heights. |
| `sanity.data.size.texture_full_screen_upload` | A full-screen 2560x1440 RGBA texture uploaded through UpdateSubresource, Map-write, and staging copy reads back intact. |
| `sanity.data.size.readback_every_size` | Readbacks (GPU to CPU) of every size in the same set complete in full: the destination has no stale or unwritten bytes. |

#### Alignment and offset

| check | contract |
|---|---|
| `sanity.data.align.offset_every_byte` | A write of 4 KiB starting at every byte offset from 0 to 255 into a larger buffer lands at exactly that offset and leaves neighbouring bytes untouched. |
| `sanity.data.align.unaligned_copy_regions` | Buffer-to-buffer copies with unaligned source and destination offsets and unaligned lengths move exactly the requested bytes. |
| `sanity.data.align.mapped_pointer_alignment` | The pointer returned by map meets the alignment the API documents for it (e.g. Vulkan minMemoryMapAlignment, OpenCL data type alignment), and writes at its end do not spill. |
| `sanity.data.align.row_pitch_variants` | Texture uploads using row pitches larger than the tightly packed pitch, and depth pitches likewise, produce correct images; padding bytes do not leak into the image. |
| `sanity.data.align.sub_region_updates` | Updating a sub-box of a texture leaves pixels outside the box unchanged, for boxes touching each edge. |

#### Operation sequences

| check | contract |
|---|---|
| `sanity.data.seq.back_to_back_10000` | Ten thousand consecutive small uploads, each to a distinct region, followed by one readback show every region with its own pattern. |
| `sanity.data.seq.interleaved_small_large` | Alternating 16-byte and 100-MiB uploads and readbacks, in random order from a fixed seed, return every byte correctly. |
| `sanity.data.seq.overwrite_ordering` | Two writes to the same region in program order leave the second one's data; two interleaved with a read between show the first one at the read. |
| `sanity.data.seq.read_after_write_same_context` | Within a single context, a readback submitted after a write always sees the write, for every API and size combination above. |
| `sanity.data.seq.many_contexts` | Sixty-four contexts or queues on one device each write and verify their own region concurrently with no cross-contamination. |
| `sanity.data.seq.many_devices_one_process` | Sixteen devices created in one process each roundtrip a distinct pattern, and the data of one never appears in another. |
| `sanity.data.seq.mixed_api_chain` | A shared Texture2D written through D3D11 (NT handle with keyed mutex, NT handle, then legacy shared flags) is read back with the original bytes through every link the runtimes report as supported: D3D12 OpenSharedHandle, Vulkan import (VK_KHR_external_memory_win32) and OpenCL cl_khr_d3d11_sharing. Each unsupported link is reported unsupported consistently. |

#### Load and pressure

| check | contract |
|---|---|
| `sanity.data.load.parallel_processes` | Eight processes each running a full roundtrip stress at once all verify every byte. |
| `sanity.data.load.parallel_with_render_load` | Roundtrips run while another process renders continuously and a third computes continuously; data stays intact. |
| `sanity.data.load.memory_pressure_host` | Roundtrips of the boundary sizes complete correctly while the host is under memory pressure created from outside the guest. [E] |
| `sanity.data.load.memory_pressure_guest` | Roundtrips complete correctly while the guest has committed most of its RAM from a separate process. [E] |
| `sanity.data.load.sustained_30s` | A thirty-second loop of mixed sizes finds no mismatch, and the achieved throughput in the last ten seconds is at least half of that in the first ten. [E] |

### 4.5 Memory semantics and reclamation (25)

#### Allocation limits

| check | contract |
|---|---|
| `sanity.mem.limit.allocate_up_to_reported` | Buffers allocated in chunks until the API's reported available budget is reached succeed, and each chunk can be filled and verified. [E] |
| `sanity.mem.limit.fail_beyond_reported` | A single OpenCL buffer above CL_DEVICE_MAX_MEM_ALLOC_SIZE fails with CL_INVALID_BUFFER_SIZE; a D3D12 or Vulkan request beyond the memory the API reports either fails with E_OUTOFMEMORY or VK_ERROR_OUT_OF_*_MEMORY or succeeds and its first and last pages work; nothing crashes and the device stays usable. [E] |
| `sanity.mem.limit.single_allocation_max` | An allocation of exactly the reported maximum succeeds and works in Vulkan and OpenCL; one byte more fails with CL_INVALID_BUFFER_SIZE in OpenCL, and in Vulkan either fails with an out-of-memory code or succeeds and works. D3D12 reports no maximum, so it is not part of this row. |
| `sanity.mem.limit.zero_size` | Zero-sized allocations return the documented error or a valid empty object in each API and never a corrupt handle. |
| `sanity.mem.limit.after_failure_usable` | After a failure the API contract defines (OpenCL above the maximum allocation, an invalid-argument creation on each API, an out-of-memory refusal where one occurs) the device stays usable: a small allocation, a copy and a render still produce correct output. |

#### Reuse and budgets

| check | contract |
|---|---|
| `sanity.mem.reuse.freed_becomes_usable` | After freeing everything allocated, the same total can be allocated again. |
| `sanity.mem.reuse.fragmented_pattern` | Allocating alternating sizes, freeing every other one and then allocating a block of their total succeeds unless the API documents a fragmentation limit. |
| `sanity.mem.budget.dxgi_moves_with_alloc` | IDXGIAdapter3::QueryVideoMemoryInfo CurrentUsage rises by at least the allocated size after a committed allocation is touched, and falls after release. |
| `sanity.mem.budget.vulkan_budget_moves` | If VK_EXT_memory_budget is exposed, heap usage rises with allocations and falls after free, and usage never exceeds budget while within limits. |
| `sanity.mem.budget.reservation_honoured` | SetVideoMemoryReservation up to the reported available reservation is accepted and reflected in the budget; above it fails the documented way. |
| `sanity.mem.budget.opencl_free_memory` | If CL_AMD / cl_ext free memory queries are exposed, the value falls by at least the allocated size. |

#### No leaks

Each loop creates and destroys 5000 objects of the kind with its largest practical size, then asserts the system returns to its baseline: process private bytes, handle count, GPU memory usage reported by the API and (in the host phase) the host's counts all return within a stated tolerance.

| check | contract |
|---|---|
| `sanity.mem.leak.buffers_all_apis` | Buffer create/destroy loops in D3D9, D3D11, D3D12, GL, Vulkan and OpenCL return to baseline. |
| `sanity.mem.leak.textures_all_apis` | Texture and render-target create/destroy loops in every API return to baseline. |
| `sanity.mem.leak.views_and_samplers` | View, sampler, descriptor heap, image view and sampler loops return to baseline. |
| `sanity.mem.leak.shaders_pipelines` | Shader and pipeline create/destroy loops (including pipeline caches) return to baseline. |
| `sanity.mem.leak.contexts_devices` | Device, context, queue and swap chain create/destroy loops in every API return to baseline and the process can still create a new one. |
| `sanity.mem.leak.sync_objects` | Fence, event, query, semaphore and OpenCL event loops return to baseline. |
| `sanity.mem.leak.shared_resources` | Creating shared resources and opening them in a second process, then closing in both, returns both processes and the system to baseline. |
| `sanity.mem.leak.process_exit_releases` | A child process that allocates in every API and exits without freeing leaves the system at its baseline once the exit is observed. |

#### Eviction and mapping

| check | contract |
|---|---|
| `sanity.mem.evict.contents_survive_pressure` | Resources filled with patterns keep their content after other allocations push total use above the budget, forcing eviction and residency changes. [E] |
| `sanity.mem.evict.make_resident_semantics` | In D3D12 MakeResident and Evict on resources keep their data after being made resident again, and failure to make resident fails cleanly. [E] |
| `sanity.mem.map.pointer_valid_until_unmap` | A persistent mapping (Vulkan, D3D12 upload heap, GL persistent buffer) stays valid and coherent across GPU work until unmapped, regardless of pressure from other allocations. |
| `sanity.mem.map.unmap_flushes` | Data written through a non-coherent mapping becomes visible to the GPU after flush and unmap, as the API defines, and reads after invalidate see GPU writes. |
| `sanity.mem.map.exit_with_live_mappings` | A process that exits with live mappings, locks and in-flight work causes no error in other processes or in the system, and its resources are released. |
| `sanity.mem.map.large_map_many` | Mapping 64 buffers of 64 MiB at once either succeeds with working pointers or fails cleanly. [E] |

### 4.6 Isolation (15)

| check | contract |
|---|---|
| `sanity.iso.mem.fresh_alloc_zero_or_unspecified` | Memory from a fresh allocation does not contain a pattern another process wrote and freed before, for buffers of sizes from 4 KiB to 256 MiB in D3D11, D3D12, Vulkan and OpenCL and for a 64 MiB D3D11 texture. |
| `sanity.iso.mem.fresh_vram_pattern` | A process fills as much device memory as the API reports with a pattern and exits; a second process allocating everything it can finds no byte of the pattern; the amount is capped at 4 GiB. [E] |
| `sanity.iso.mem.fresh_host_visible` | Host-visible and upload-heap allocations of a second process contain none of the first process's freed data. |
| `sanity.iso.handle.no_access_without_handle` | A process cannot reach another's shared resource without a handle or name granted to it; attempted opens by name guesses fail. |
| `sanity.iso.handle.closed_handle_dead` | After the owner closes and the last handle is gone, opening the shared object fails; no later object reuses its contents. |
| `sanity.iso.fault.crash_leaves_others` | A process killed while it has GPU work in flight does not alter the results of eight other processes running verified workloads. |
| `sanity.iso.fault.device_loss_leaves_others` | Removal or reset of one process's device leaves the other processes' devices working correctly. [E] |
| `sanity.iso.fault.oom_leaves_others` | A process exhausting all device memory does not cause failures in a second process holding a small allocation, and that process's data is intact. [E] |
| `sanity.iso.fault.hang_leaves_others` | A long-running shader in one process delays another's simple work by no more than the documented timeout bound and does not corrupt it. [E] |
| `sanity.iso.fault.invalid_commands_contained` | A process submitting invalid commands (out-of-range indices and offsets far beyond the buffer, issued through compute dispatches) is the only one affected. |
| `sanity.iso.stall.heavy_load_other_finishes` | A process with a saturating compute loop does not prevent another process from finishing a 1 MiB roundtrip within a stated multiple of its idle time. [E] |
| `sanity.iso.stall.huge_upload_other_progress` | While one process uploads 512 MiB repeatedly, another's 64 KiB roundtrips each complete within a bounded time. [E] |
| `sanity.iso.write.out_of_bounds_contained` | Out-of-bounds writes in one process do not alter any other process's buffers, checked by sentinel buffers. |
| `sanity.iso.share.read_only_enforced` | A resource opened read-only through a shared handle cannot be written by the opener. |
| `sanity.iso.session.service_session_separate` | A process in session 0 and one in the user session see the same adapters but not each other's unshared resources; the session 0 process is started through a SYSTEM scheduled task. [E] |

### 4.7 Synchronisation and concurrency (19)

| check | contract |
|---|---|
| `sanity.sync.order.readback_after_signal` | A CPU read after a fence wait always sees the GPU writes preceding the signal, over 10000 iterations in D3D11, D3D12, Vulkan and OpenCL. |
| `sanity.sync.order.flush_makes_visible` | Flush/finish (clFinish, ID3D11DeviceContext::Flush plus query, a D3D12 queue drained to idle, vkDeviceWaitIdle) guarantee completion of preceding work as the API defines. |
| `sanity.sync.order.queue_order_in_order` | Commands in one in-order queue execute and complete in submission order: a chain of dependent increments yields the exact count. |
| `sanity.sync.order.out_of_order_events` | OpenCL out-of-order queue plus event dependencies yield the dependency-correct result. |
| `sanity.sync.wait.timeout_honoured` | vkWaitForFences, clGetEventInfo polls (clWaitForEvents has no timeout), GetData polls and D3D12 event waits return within the requested timeout plus a stated slack when the work never completes. [E] |
| `sanity.sync.wait.zero_timeout_poll` | A zero timeout returns immediately with the not-ready result while work is outstanding and ready after it ends. |
| `sanity.sync.wait.signal_before_wait` | A fence already signalled when waited on returns immediately, for every API with fences. |
| `sanity.sync.thread.many_threads_one_device` | Thirty-two threads each submitting and waiting on independent work on one device all complete with correct results and no deadlock. |
| `sanity.sync.thread.multi_queue_vulkan` | Work on separate Vulkan queues with semaphore dependencies runs in dependency order with correct results. |
| `sanity.sync.thread.d3d12_multi_queue` | Direct, compute and copy queues linked by fences produce the dependency-correct result. |
| `sanity.sync.thread.immediate_context_serialised` | Using D3D11 deferred contexts built on many threads and executed on the immediate context yields commands in the execution order. |
| `sanity.sync.process.many_processes` | Sixteen processes each running a verified workload finish with correct results within a bounded time. |
| `sanity.sync.process.shared_fence_ping_pong` | Two processes alternately incrementing a shared fence reach the exact count with no lost or doubled step. |
| `sanity.sync.cross.d3d11_d3d12_fence` | A shared fence chain between D3D11 and D3D12 devices orders writes and reads across both. |
| `sanity.sync.cross.vulkan_d3d12_semaphore` | If external semaphore interop is reported, a Vulkan-to-D3D12 semaphore chain orders work correctly; otherwise the capability is reported absent and use fails the documented way. |
| `sanity.sync.cross.gl_d3d11_interop` | WGL_NV_DX_interop2, when exposed, shows D3D11 writes in GL after unlock and the reverse. |
| `sanity.sync.deadlock.contention_no_deadlock` | Threads taking resources in opposite orders across queues, with timeouts, complete with no deadlock for thirty seconds of wall time. |
| `sanity.sync.fairness.low_rate_not_starved` | A context submitting one tiny job every 10 ms next to a saturating context sees each job complete within a bounded multiple of its idle latency. [E] |
| `sanity.sync.fairness.equal_load_share` | Two contexts with equal saturating loads each achieve at least a third of the combined throughput. [E] |

### 4.8 Direct3D 9 and 9Ex (40)

#### Device creation and caps

| check | contract |
|---|---|
| `sanity.d3d9.caps.create_matrix` | CreateDevice succeeds for HAL with HARDWARE, MIXED and SOFTWARE vertex processing (where GetDeviceCaps reports the matching support), with and without MULTITHREADED and FPU_PRESERVE, and the device renders a clear afterwards. |
| `sanity.d3d9.caps.caps_vs_behaviour` | Every boolean cap the check can exercise (texture caps POW2/NONPOW2CONDITIONAL, CUBEMAP, VOLUMEMAP, MAXTEXTURESIZE, MaxSimultaneousTextures, NumSimultaneousRTs, MaxStreams, MaxVertexIndex, MaxPrimitiveCount, VS/PS versions) agrees with behaviour: a resource or draw within the cap works, one beyond it fails with D3DERR_INVALIDCALL or D3DERR_NOTAVAILABLE (MaxPrimitiveCount is exercised only within the cap). |
| `sanity.d3d9.caps.check_device_format_consistent` | CheckDeviceFormat, CheckDeviceMultiSampleType, CheckDepthStencilMatch and CheckDeviceFormatConversion answers agree with whether CreateTexture/CreateRenderTarget/CreateDepthStencilSurface of that format, usage and multisample type succeed, for a table of formats and usages. |
| `sanity.d3d9.caps.ex_interface` | Direct3DCreate9Ex and CreateDeviceEx succeed or the whole Ex entry set is absent consistently; a 9Ex device exposes the same caps as a 9 device on the same adapter apart from the documented Ex bits. |
| `sanity.d3d9.caps.adapter_identity` | GetAdapterIdentifier, GetAdapterCount, GetAdapterDisplayMode and GetAdapterLUID are mutually consistent, and the LUID equals the one DXGI reports for the same adapter. |

#### Resources

| check | contract |
|---|---|
| `sanity.d3d9.res.pool_usage_matrix` | For every type (VB, IB, 2D texture, cube, volume, surface, RT, DS) the combinations of pool (DEFAULT, MANAGED, SYSTEMMEM, SCRATCH) and usage (DYNAMIC, RENDERTARGET, DEPTHSTENCIL, WRITEONLY, AUTOGENMIPMAP) accepted or rejected equal the documented rules (D3D9 has no WARP; 9Ex rejects MANAGED); accepted ones report matching GetDesc. |
| `sanity.d3d9.res.mip_levels_and_sizes` | Texture creation with Levels 0 yields the full chain down to 1x1, non-power-of-two sizes follow floor(size/2) per level, and GetLevelDesc and LockRect pitch are consistent for every level. |
| `sanity.d3d9.res.volume_and_cube` | Volume and cube textures keep faces and slices independent: writing a distinct pattern per face/slice and reading back returns each pattern with no bleed, honouring RowPitch and SlicePitch. |
| `sanity.d3d9.res.managed_roundtrip` | Data written to a MANAGED texture is preserved across a draw, an eviction pressure loop that allocates many DEFAULT resources, and a device Reset; reading back through a render still gives the original data. |
| `sanity.d3d9.res.priority_and_lod` | SetPriority/GetPriority and SetLOD/GetLOD on managed resources return the values set (LOD clamped to the level count) and LOD selects the expected top mip when sampled. |
| `sanity.d3d9.res.autogenmipmap` | A texture with D3DUSAGE_AUTOGENMIPMAP regenerates lower levels from level 0 after a write or render; GenerateMipSubLevels matches a box-filter CPU reference within one unit. |
| `sanity.d3d9.res.exhaustion` | [E]: creating DEFAULT-pool resources until failure returns D3DERR_OUTOFVIDEOMEMORY or E_OUTOFMEMORY (never crashes or hangs), the device stays usable, and freeing them allows creation again. |

#### Locking

| check | contract |
|---|---|
| `sanity.d3d9.lock.discard_fresh` | Lock with DISCARD on a dynamic buffer or texture lets the app write all contents; an earlier draw that read the old contents still renders the old data. |
| `sanity.d3d9.lock.nooverwrite_inflight` | Appending with NOOVERWRITE to a dynamic VB while earlier draws from the same buffer are queued never changes the data those draws consume: all frames of a ring-style pattern match the WARP output. |
| `sanity.d3d9.lock.readonly_and_partial` | READONLY locks return the contents last written (including GPU renders to the surface), partial rect/box locks address exactly the requested region with the returned pitch, and out-of-range rects fail with INVALIDCALL. |
| `sanity.d3d9.lock.dirty_regions` | AddDirtyRect on a managed texture makes only the declared region be re-uploaded; a write outside the dirty rect with NO_DIRTY_UPDATE locks is not required to appear, a write inside it must appear in the next draw. |
| `sanity.d3d9.lock.errors` | Double Lock without Unlock, Unlock without Lock, locking a non-lockable (default-pool RT without LOCKABLE, depth-stencil) surface, and DISCARD on a non-dynamic buffer (success or INVALIDCALL) return the documented codes and leave the device usable. |

#### Blt operations

| check | contract |
|---|---|
| `sanity.d3d9.blt.stretchrect` | StretchRect between RTs/textures with rect, point and linear filtering matches the WARP result within a small tolerance for scaled, flipped (negative extents rejected) and format-converting copies; invalid format/pool combos fail with INVALIDCALL. |
| `sanity.d3d9.blt.updatesurface_texture` | UpdateSurface (SYSTEMMEM to DEFAULT, subrect and point) and UpdateTexture (dirty subregions, all mip levels) deliver exact bytes, verified by render-and-readback. |
| `sanity.d3d9.blt.colorfill_and_readback` | ColorFill on an offscreen plain surface and RT writes the exact colour in every supported format, and GetRenderTargetData returns exactly what the GPU last drew, including for a surface drawn immediately before the call. |
| `sanity.d3d9.blt.frontbuffer` | GetFrontBufferData on a windowed device returns a surface of the screen size whose pixels under a topmost solid-colour window of known content equal that colour; it fails cleanly for a wrong-size or wrong-format destination. |

#### Rendering

| check | contract |
|---|---|
| `sanity.d3d9.render.fixed_function` | A set of fixed-function scenes (lighting with each light type, fog modes, texture stage ops and TCI, alpha test, stencil ops, user clip planes) renders within tolerance of WARP. |
| `sanity.d3d9.render.shaders` | VS/PS 2.0/3.0 shaders covering loops, dynamic branching, texkill, dsx/dsy, MRT and vFace render within tolerance of WARP and the CPU reference. |
| `sanity.d3d9.render.states_and_blend` | Each render state group (blend ops and factors, separate alpha blend, sRGB write, depth bias, scissor, colour write mask, multisample mask) produces the CPU-reference result on a small grid of pixels. |
| `sanity.d3d9.render.multisample` | Multisampled RT with StretchRect resolve of a known edge pattern yields coverage-weighted values consistent with the reported quality level and equal to WARP within tolerance. |
| `sanity.d3d9.render.draw_variants` | DrawPrimitive, DrawIndexedPrimitive, DrawPrimitiveUP, DrawIndexedPrimitiveUP, instancing via SetStreamSourceFreq, and 16/32-bit indices with BaseVertexIndex all produce identical pixels for the same geometry. |

#### Queries

| check | contract |
|---|---|
| `sanity.d3d9.query.occlusion` | An occlusion query returns exactly the number of passing samples of a known quad (analytic area), zero for a fully depth-failed quad, and GetData polling with GETDATA_FLUSH completes in bounded time. |
| `sanity.d3d9.query.event_timestamp` | An EVENT query signals true only after all prior GPU work has completed (a prior render is readable at that point); TIMESTAMP and TIMESTAMPFREQ give a non-decreasing timestamp with a non-zero frequency, and unsupported query types return D3DERR_NOTAVAILABLE consistently with CreateQuery(NULL). |

#### Present

| check | contract |
|---|---|
| `sanity.d3d9.present.swap_effects` | DISCARD, FLIP, COPY and (9Ex) FLIPEX swap effects with 1..3 back buffers present a known per-frame colour, and the window shows the last presented colour; back buffer contents after Present match the swap-effect rules (COPY preserves). |
| `sanity.d3d9.present.intervals` | Present with INTERVAL_ONE paces to the display refresh (N presents take at least N-1 refresh periods) and INTERVAL_IMMEDIATE does not pace; measured with a tolerance, deterministic ordering only. [E] |
| `sanity.d3d9.present.presentex_flags` | PresentEx with DONOTWAIT returns D3DERR_WASSTILLDRAWING when the previous present is still pending and succeeds on retry, with DONOTFLIP, FORCEIMMEDIATE and LINEAR_CONTENT accepted or rejected per the documentation; GetPresentStats/GetLastPresentCount are monotonic. |
| `sanity.d3d9.present.src_dst_rects` | Present with source and destination rectangles and dirty regions (windowed COPY) shows exactly the requested region scaled as specified. |

#### Device lost and reset

| check | contract |
|---|---|
| `sanity.d3d9.lost.reset_semantics` | After Reset (change of size, format, multisample) default-pool resources must be released first or Reset fails with INVALIDCALL; after a successful Reset managed resources retain content, default ones are fresh, and rendering works. |
| `sanity.d3d9.lost.ex_never_lost` | A 9Ex device never returns D3DERR_DEVICELOST/NOTRESET from Present or TestCooperativeLevel across a desktop mode switch or secure desktop (Ctrl+Alt+Del style) transition; CheckDeviceState returns S_OK or the documented PRESENT_* codes. [E] |

#### Sharing and interop patterns

| check | contract |
|---|---|
| `sanity.d3d9.share.cross_device` | A shared DEFAULT-pool texture created with pSharedHandle on device A is opened on device B (same process) and B sees what A rendered after A's EVENT query completes. |
| `sanity.d3d9.share.cross_process` | The same shared texture opened in a second process (the legacy shared handle value passed through the environment) observes every frame the producer writes with no tearing for a sequence of 200 known frames, using query ordering. |
| `sanity.d3d9.share.d3dimage_pattern` | The WPF/D3DImage pattern (9Ex device, shared RT with LOCKABLE flag off, render, Flush, then consumer reads through D3D11 OpenSharedResource) yields correct pixels each frame with the producer rendering the next frame while the consumer reads the previous. |

#### Failure behaviour

| check | contract |
|---|---|
| `sanity.d3d9.fail.unsupported_features` | Features the caps report absent (formats, texture sizes, shader models, multi-element textures) fail at creation or draw with the documented D3DERR_* and the device renders correctly afterwards. |
| `sanity.d3d9.fail.bad_parameters` | invalid sizes and invalid enums return INVALIDCALL (or the documented code) without crash, and the debug runtime reports no unexpected corruption; RT/DS size mismatch and viewports beyond the render target are measured only. |
| `sanity.d3d9.fail.lifetime` | Releasing a device with resources still referenced by the app, destroying a resource that is the current RT or bound texture, and releasing in any order leaves no leak and no crash; process exit with a live device reclaims everything (second run allocates the same amount). |

### 4.9 Direct3D 10 and 10.1 (10)

#### Creation, rendering and feature levels

| check | contract |
|---|---|
| `sanity.d3d10.create.device_flags` | D3D10CreateDevice and D3D10CreateDevice1 succeed on the hardware adapter with DEBUG, SINGLETHREADED and BGRA_SUPPORT flags where valid, and fail with the documented HRESULT for a bad driver type or adapter. |
| `sanity.d3d10.fl.levels_101` | D3D10CreateDevice1 requested at each of 10_0, 10_1 returns the requested level or fails; the level obtained matches features usable (multisampled depth shader resources at 10_1 only) and the 10.0 level rejects them. |
| `sanity.d3d10.res.format_support` | CheckFormatSupport for a table of formats is consistent with creation of textures with the corresponding bind flags, and CheckMultisampleQualityLevels agrees with creating a multisampled RT. |
| `sanity.d3d10.render.pipeline_warp` | A scene with VS, GS, PS, depth/stencil, blend, and instancing renders within tolerance of D3D10 WARP (D3D10 and D3D10.1 devices; the row fails when the WARP oracle is unavailable). |
| `sanity.d3d10.render.streamout` | Stream-output of a known vertex set returns exact positions, and the SO_STATISTICS query counts primitives written and needed correctly including an overflowing buffer. |
| `sanity.d3d10.map.contracts` | Map READ after a GPU write returns the written data, WRITE_DISCARD returns fresh memory, WRITE_NO_OVERWRITE leaves queued draws intact, and DO_NOT_WAIT either succeeds or returns DXGI_ERROR_WAS_STILL_DRAWING and later succeeds. |
| `sanity.d3d10.dxgi.swapchain` | A D3D10 device creates a swap chain, presents, resizes buffers and reads back the back buffer through a staging copy with the expected colour. |
| `sanity.d3d10.dxgi.interop_d3d11` | A D3D10 resource shared through DXGI handle opens in D3D11 on the same adapter and the data written by one is visible to the other after synchronisation. |
| `sanity.d3d10.query.predicate` | Occlusion, OcclusionPredicate, PipelineStatistics and Timestamp/Disjoint queries give correct values on known workloads and SetPredication hides draws exactly when the predicate is true. |
| `sanity.d3d10.state.clearstate_and_stale` | ClearState and ID3D10Device::Flush leave pipeline state unbound as documented (draw with nothing bound produces no output without a crash), and the device still clears and reads back a fresh target after ClearState; whether a released bound object stays reachable is a measure. |

### 4.10 Direct3D 11 (65)

#### Feature levels and caps consistency

| check | contract |
|---|---|
| `sanity.d3d11.fl.requested_levels` | D3D11CreateDevice with each single feature level list from 9_1 to 11_1 (and 12_x lists) returns exactly that level when supported or E_INVALIDARG/DXGI_ERROR_UNSUPPORTED when not; the returned level is the highest of any list where it is supported. |
| `sanity.d3d11.fl.checkfeaturesupport_options` | Every CheckFeatureSupport struct (D3D11_OPTIONS through OPTIONS5, ARCHITECTURE_INFO, DOUBLES, SHADER_MIN_PRECISION, MARSHALLING) returns S_OK or the documented error and fields are mutually consistent with the feature level (e.g. FL11 implies compute and typed-UAV loads rules). |
| `sanity.d3d11.fl.format_support_vs_creation` | For every DXGI_FORMAT, each bit of CheckFormatSupport/CheckFormatSupport2 (texture1D/2D/3D/cube, render target, blend, MSAA render, UAV typed load/store, mip generation, display, shader sample/gather/comparison) corresponds to successful creation and correct use of the matching resource/view, and each absent bit corresponds to documented failure. |
| `sanity.d3d11.fl.msaa_quality_vs_use` | CheckMultisampleQualityLevels for each format and count 1..16 matches whether a texture with those SampleDesc values can be created; every reported quality level below the count renders and resolves a known pattern. |
| `sanity.d3d11.fl.limits_at_level` | Per-feature-level documented limits (max texture dimension, array size, render targets, UAV slots, constant buffer size, viewports) are honoured by creating or binding at the limit; the runtime does not promise refusal beyond the limit, so beyond-limit results (success, E_INVALIDARG or E_OUTOFMEMORY) are measures. |

#### Resource creation matrix

| check | contract |
|---|---|
| `sanity.d3d11.res.usage_bind_cpu_matrix` | Across usage (DEFAULT, IMMUTABLE, DYNAMIC, STAGING) x bind flags x CPU access x misc flags x dimension (buffer, 1D, 2D, 3D, cube, arrays) the accepted/rejected set matches WARP exactly. |
| `sanity.d3d11.res.immutable_initial` | IMMUTABLE resources require initial data and thereafter return exactly that data when read via a view in shader and via a copy; creation with missing or short initial data fails. |
| `sanity.d3d11.res.views_matrix` | SRV, RTV, DSV and UAV creation over typed, typeless, array, MSAA, mip-sliced and buffer resources (raw, structured, append, counter) matches WARP's accept/reject, and views of sub-ranges read/write exactly that sub-range. |
| `sanity.d3d11.res.sizes_extremes` | Resources at the maximum documented dimensions, 1x1, odd sizes, and zero-sized requests behave per spec (success, E_INVALIDARG, or E_OUTOFMEMORY), leaving the device usable. |
| `sanity.d3d11.res.exhaustion_and_reclaim` | Allocating default-usage resources until failure (a failure before the cap is E_OUTOFMEMORY; no crash or hang), the device still renders, and after releasing and Flush the same allocations succeed again. |
| `sanity.d3d11.res.exit_reclaim` | A child process that allocates a large amount of GPU memory and exits (cleanly or killed) releases it all: QueryVideoMemoryInfo current usage returns to its baseline within a timeout. |

#### Map contracts

| check | contract |
|---|---|
| `sanity.d3d11.map.read_last_write` | Map(READ) on a staging copy of a resource returns the GPU's most recent writes by draw, compute, copy, clear and resolve, with data identical to a CPU reference. |
| `sanity.d3d11.map.discard_old_draws` | After Map(WRITE_DISCARD) and rewrite, draws issued before the discard render the old contents and draws after render the new, for buffers (VB, CB, IB) and dynamic textures. |
| `sanity.d3d11.map.nooverwrite_inflight` | Appending to a dynamic VB/IB with WRITE_NO_OVERWRITE while earlier draws from it are still queued never alters the data those draws read; 1000 append-draw cycles match WARP. |
| `sanity.d3d11.map.do_not_wait` | Map with DO_NOT_WAIT on a staging resource the GPU is writing returns DXGI_ERROR_WAS_STILL_DRAWING only while busy, never blocks, and succeeds on a later poll; on an idle resource it succeeds immediately. |
| `sanity.d3d11.map.pitches` | Map of 2D, array and 3D resources returns RowPitch >= row bytes (and DepthPitch >= slice bytes) and data written using those pitches is read back correctly through a copy, including BC formats where pitch is per block row. |
| `sanity.d3d11.map.errors` | Double Map, Unmap without Map, Map READ on DEFAULT or WRITE on IMMUTABLE, Map of a MS texture, and mismatched map type vs CPU access return the documented errors and later mapping works. |
| `sanity.d3d11.map.persistent_across_frames` | A staging texture held mapped across other draws and presents (where the API allows it) keeps its contents stable and Unmap flushes writes back correctly. |

#### UpdateSubresource and copies

| check | contract |
|---|---|
| `sanity.d3d11.update.boxes_pitches` | UpdateSubresource with a box, custom RowPitch/DepthPitch, null pitches for buffers, on all dimensions writes exactly the box and leaves surroundings untouched. |
| `sanity.d3d11.update.bc_and_formats` | UpdateSubresource for BC1-7 and packed formats with block-aligned and edge-block boxes follows the documented alignment and rejects unaligned boxes. |
| `sanity.d3d11.update.copy_flags` | UpdateSubresource1 with COPY_NO_OVERWRITE and COPY_DISCARD on constant buffers and dynamic buffers: draws queued earlier keep their data, later draws see the update, matching WARP. |
| `sanity.d3d11.update.deferred_ctx` | UpdateSubresource on a deferred context recorded and executed later applies at execution time in command order. |
| `sanity.d3d11.copy.variants` | CopyResource and CopySubresourceRegion between all pairs of usage (DEFAULT, STAGING), same/different mip, array slice, 3D box, partially overlapped source and destination (rejected for overlap) produce exact data or the documented failure. |
| `sanity.d3d11.copy.typeless_cross_format` | Copying between typeless and typed variants, and between BC and uncompressed formats of equal element size (e.g. BC1 to R32G32_UINT block-wise), preserves bits exactly; incompatible pairs fail. |
| `sanity.d3d11.copy.structure_copy` | CopyStructureCount returns the exact count of an append buffer after known appends and consumes; CopyResource of buffers with structured stride preserves layout. |
| `sanity.d3d11.copy.resolve` | ResolveSubresource from MSAA to single-sample for UNORM, SRGB, FLOAT formats yields correctly averaged values for a known sample pattern within the format's precision, and equals WARP. |
| `sanity.d3d11.copy.generatemips` | GenerateMips on an SRV with RTV capability produces levels within a small tolerance of a CPU box filter and equal to WARP; on a format without mip-generation support fails per CheckFormatSupport. |
| `sanity.d3d11.copy.clearview_discard` | ClearView with rect lists sets exactly the rects (rest untouched); DiscardResource/DiscardView leave the resource usable (contents undefined, but not corrupting other resources) and a subsequent clear or draw fully defines it. |

#### Rendering pipeline

| check | contract |
|---|---|
| `sanity.d3d11.render.basic_stages` | VS/PS pipelines with each primitive topology, indexed and instanced, DrawAuto and indirect draws render pixel-equal (within tolerance) to WARP. |
| `sanity.d3d11.render.states` | Blend (incl. dual-source, logic ops, independent blend), depth, stencil, rasterizer (fill, cull, bias, scissor, conservative per caps, forced sample count) and sampler (all filters, addressing, comparison, anisotropy, border) match WARP over a grid of settings. |
| `sanity.d3d11.render.msaa` | Rendering to 2x/4x/8x MSAA targets with SV_Coverage, SV_SampleIndex, sample-frequency shading, alpha-to-coverage and sample mask matches WARP after resolve. |
| `sanity.d3d11.render.tessellation` | Hull/domain shaders with integer, fractional_odd, fractional_even and pow2 partitioning on tri/quad/isoline domains produce the analytic geometry: one-pixel isolines on the predicted rows, and tri and quad tile interiors covered. |
| `sanity.d3d11.render.geometry_streamout` | Geometry shader amplification and SO to multiple buffers (with rasterization off) return exact vertex data, and the SO overflow query reports correctly. |
| `sanity.d3d11.render.format_sweep` | A clear and a blended draw into every RTV-capable format reads back the expected values at that format's precision; sRGB targets apply linear-to-sRGB on write and sampling reverses it. |
| `sanity.d3d11.render.depth_stencil_formats` | D16, D24S8, D32, D32S8 with reversed-Z, sampling depth via R-typeless SRVs, and stencil read via X-typeless SRVs give values equal to WARP or the analytic expected depth. |
| `sanity.d3d11.render.multi_rt_viewports` | Eight render targets with different formats, 16 viewports and scissors selected by SV_ViewportArrayIndex, and RT-array index selection route output to the correct slices. |
| `sanity.d3d11.render.resource_hazards` | Binding a resource as RTV and SRV at once unbinds the SRV (documented), and a draw sampling the previous pass's output sees complete data with no stale reads across 100 pass chains. |

#### Compute

| check | contract |
|---|---|
| `sanity.d3d11.compute.uav_basic` | Dispatch and DispatchIndirect over raw, structured and typed UAVs write exactly the expected values in a CPU-referenced pattern across thread groups of various sizes and thread-group-shared memory. |
| `sanity.d3d11.compute.atomics` | InterlockedAdd/Min/Max/And/Or/Xor/Exchange/CompareExchange from 64k threads on one address give the exact analytic total; on typed UAVs only when CheckFeatureSupport permits. |
| `sanity.d3d11.compute.append_consume` | Append buffer appended to by N threads then consumed by N threads sees every element exactly once; counter value equals analytic count; UAV initial counts are honoured. |
| `sanity.d3d11.compute.typed_uav_load` | Typed UAV loads for formats flagged with UAV_TYPED_LOAD return what was stored; for unflagged formats the shader compile or creation fails consistently with the caps. |
| `sanity.d3d11.compute.barriers_sharing` | GroupMemoryBarrierWithGroupSync reduction, prefix-sum and histogram kernels match the CPU result at sizes that are not multiples of group size. |
| `sanity.d3d11.compute.cs_on_downlevel` | Compute shader 4.x on FL10 devices is available iff the option reports it and then gives correct results; otherwise creating CS fails per documentation. |

#### Queries and predication

| check | contract |
|---|---|
| `sanity.d3d11.query.occlusion_counts` | OCCLUSION returns the exact sample count for a known quad, 0 when hidden, and OCCLUSION_PREDICATE true/false accordingly; GetData with DONOTFLUSH eventually completes after Flush. |
| `sanity.d3d11.query.timestamp_disjoint` | TIMESTAMP_DISJOINT frequency is non-zero, timestamps in one non-disjoint interval are non-decreasing, and a long dispatch measures longer than a trivial one. |
| `sanity.d3d11.query.pipeline_stats` | PIPELINE_STATISTICS counts (IA vertices/primitives, VS/GS/PS invocations, CS invocations, C primitives) match analytic counts for known draws. |
| `sanity.d3d11.query.event_and_predication` | EVENT queries signal after prior work completes; SetPredication with an occlusion predicate and PREDICATION_OP_EQUAL_FALSE hides exactly the draws, copies and clears the spec says it affects. |

#### Deferred contexts and multithreading

| check | contract |
|---|---|
| `sanity.d3d11.deferred.equivalence` | A scene recorded on deferred contexts and executed with ExecuteCommandList produces pixels identical to the same calls on the immediate context. |
| `sanity.d3d11.deferred.maps` | Map WRITE_DISCARD and WRITE_NO_OVERWRITE on a deferred context (buffers; READ unsupported fails) apply in command order on execution, and constant buffer updates in lists don't alias each other. |
| `sanity.d3d11.deferred.list_reuse` | One command list executed many times (including twice in a row and after the immediate context changed state) gives repeatable results; RestoreDeferredContextState true/false behaves as documented. |
| `sanity.d3d11.deferred.clearstate` | ClearState after FinishCommandList resets bound state; a new recording does not inherit state from an earlier one; command lists outlive the deferred context. |
| `sanity.d3d11.mt.free_threaded` | Creating textures, buffers, shaders and views from 8 threads concurrently on one device all succeed, with each resource usable by the immediate context afterwards. |
| `sanity.d3d11.mt.multithread_protect` | With ID3D11Multithread protection enabled, the immediate context used from 4 threads concurrently with Enter/Leave renders without corruption, GetMultithreadProtected reports the setting, and SetMultithreadProtected returns the previous state. |
| `sanity.d3d11.mt.two_devices` | Two devices on the same adapter in separate threads render independently; destroying one mid-work doesn't affect the other. |

#### Shared resources

| check | contract |
|---|---|
| `sanity.d3d11.share.legacy_handle` | A MISC_SHARED texture's GetSharedHandle opens in a second device via OpenSharedResource and shows the producer's rendering after a flush and event query. |
| `sanity.d3d11.share.nt_handle` | MISC_SHARED_NTHANDLE with CreateSharedHandle (with name and access mask) opens through OpenSharedResource1/ByName in another device and process; closing producer handle keeps the consumer's resource valid. |
| `sanity.d3d11.share.keyed_mutex` | IDXGIKeyedMutex acquire/release orders access: consumer acquiring key 1 only after producer released with key 1 sees all writes; a timeout acquire returns WAIT_TIMEOUT while held; cross-process 100-frame ping-pong has no torn frames. |
| `sanity.d3d11.share.fence` | ID3D11Fence shared across devices (11.4) signalled on one device and waited on another orders the work; unsupported caps give consistent failure. |

#### Tiled, 11.1-11.4 interfaces

| check | contract |
|---|---|
| `sanity.d3d11.tiled.caps_driven` | If TiledResources tier is reported, a tiled texture and buffer support UpdateTileMappings, CopyTiles, null-tile reads (zero) per tier, and GetResourceTiling is self-consistent; otherwise creation with MISC_TILED fails with the documented error. |
| `sanity.d3d11.ifaces.d3d11_1` | ID3D11Device1 / DeviceContext1 features (logic ops, ClearView, DiscardView, SwapDeviceContextState, 1.1 constant buffer offsets, subresource copy flags) work when the FL and options report them. |
| `sanity.d3d11.ifaces.d3d11_2_3_4` | Device2-5 features (ID3D11Device3 RW rasterizer, 3D texture Texture2D1, Map-default-with-flags, ID3D11Device5 fences, shared fence) work when the options report them and QueryInterface fails cleanly otherwise. |
| `sanity.d3d11.ifaces.on12` | D3D11On12 wrapping a D3D12 queue: a wrapped resource written by 11 and read by 12 after ReleaseWrappedResources and fence has the expected data. |

#### Errors and limits

| check | contract |
|---|---|
| `sanity.d3d11.err.bad_parameters` | Invalid enums, null descriptors, size overflow and wrong-binding calls return E_INVALIDARG (or the documented value) with the infoqueue (when present) reporting the same; no crash and the device keeps working. |
| `sanity.d3d11.err.device_removed` | After a forced removal (TDR style or RemoveDevice via the documented test hooks) GetDeviceRemovedReason returns a documented DXGI_ERROR_* and calls fail cleanly; creating a new device afterwards works. [E] |
| `sanity.d3d11.err.stress_destroy` | Create/destroy loops of devices, contexts, resources and states for 2000 iterations leave memory use (process and video) flat within tolerance. |

### 4.11 DXGI (26)

#### Factory, adapters, outputs

| check | contract |
|---|---|
| `sanity.dxgi.enum.adapters_consistent` | EnumAdapters, EnumAdapters1, EnumAdapterByLuid and EnumAdapterByGpuPreference (HIGH_PERFORMANCE/MINIMUM_POWER/UNSPECIFIED) return the same adapter set with matching LUID, vendor/device ids, and descriptions; the end of enumeration returns DXGI_ERROR_NOT_FOUND. |
| `sanity.dxgi.enum.factory_lifecycle` | IsCurrent flips to false after adapters change or new factory; CreateDXGIFactory2 with and without the debug flag yields working factories; MakeWindowAssociation flags are retained. |
| `sanity.dxgi.enum.adapter_desc_matches_apis` | DXGI_ADAPTER_DESC memory fields are non-zero and agree in order of magnitude with QueryVideoMemoryInfo budget, and the LUID matches D3D9/D3D11/D3D12/OpenGL/Vulkan queries for the same device. |
| `sanity.dxgi.enum.output_modes` | EnumOutputs yields at least one output with a non-empty desktop rect, GetDisplayModeList for each format is sorted-unique and contains the current mode, GetDisplaySurfaceData returns pixels of the desktop size, and FindClosestMatchingMode returns a listed mode. |
| `sanity.dxgi.enum.output_desc_vs_win32` | DXGI_OUTPUT_DESC DesktopCoordinates and Rotation match EnumDisplayMonitors/GetMonitorInfo, and GetDesc1 colour space and luminance fields agree with the advanced colour state of the monitor. |

#### Swap chains and Present

| check | contract |
|---|---|
| `sanity.dxgi.swap.model_matrix` | For BITBLT/FLIP_SEQUENTIAL/FLIP_DISCARD x buffer counts 1-8 x formats (R8G8B8A8, B8G8R8A8, sRGB, R10G10B10A2, R16G16B16A16_FLOAT) creation succeeds or fails per the documented rules and the presented colour is visible on screen. |
| `sanity.dxgi.swap.buffer_contents` | After Present, a one-buffer SEQUENTIAL chain keeps the back buffer contents and DISCARD does not require it; FLIP models rotate buffers such that GetCurrentBackBufferIndex advances per Present and each frame's unique colour is displayed in order. |
| `sanity.dxgi.swap.present1` | Present1 with dirty rects and scroll rect/offset updates exactly the declared region visually, with unchanged regions retained, and invalid rects fail with E_INVALIDARG. |
| `sanity.dxgi.swap.allow_tearing` | ALLOW_TEARING creation and Present(0, PRESENT_ALLOW_TEARING) work iff CheckFeatureSupport(PRESENT_ALLOW_TEARING) reports true; otherwise creation with the flag fails; both consistent. |
| `sanity.dxgi.swap.do_not_wait` | Present with DXGI_PRESENT_DO_NOT_WAIT returns DXGI_ERROR_WAS_STILL_DRAWING while the queue is full and succeeds when it drains; TEST flag does not present. |
| `sanity.dxgi.swap.waitable_latency` | A waitable swap chain's object signals no more than MaximumFrameLatency frames ahead; SetMaximumFrameLatency / GetMaximumFrameLatency round-trip; waits complete in bounded time while frames are presented. |
| `sanity.dxgi.swap.frame_statistics` | GetLastPresentCount increments by one per Present and GetFrameStatistics PresentCount, SyncRefreshCount are monotonic non-decreasing, with refresh-count deltas close to elapsed time times refresh rate. |
| `sanity.dxgi.swap.resize_buffers` | ResizeBuffers with new size, format, buffer count (all references released first, else DXGI_ERROR_INVALID_CALL) yields correct dimensions and a working chain; ResizeTarget moves the window mode as documented. |
| `sanity.dxgi.swap.fullscreen_transition` | SetFullscreenState true then false round-trips, GetFullscreenState reports it, the output mode matches the request, and the window is restored to its prior rect. [E] |
| `sanity.dxgi.swap.hdr_colorspace` | Float16 and R10G10B10A2 swap chains accept SetColorSpace1 only for spaces CheckColorSpaceSupport reports, and rendering with the accepted space reads back the written values; unsupported spaces fail with E_INVALIDARG. |
| `sanity.dxgi.swap.getdc` | A GDI_COMPATIBLE buffer yields a usable HDC via GetDC whose GDI drawing appears after ReleaseDC and Present; GetDC on a non-compatible chain fails with the documented error. |
| `sanity.dxgi.swap.background_alpha_composition` | Composition swap chains (CreateSwapChainForComposition / premultiplied alpha) create when DirectComposition is available, and rendering a half-transparent pixel over a known backdrop composites to the analytic value. |
| `sanity.dxgi.swap.multiple_and_orphan` | Several swap chains on one window/device, a swap chain whose window is destroyed, and a Present from another thread each yield documented results (or DXGI_ERROR_INVALID_CALL) without hangs. |

#### Memory, power and overlays

| check | contract |
|---|---|
| `sanity.dxgi.mem.video_memory_info` | QueryVideoMemoryInfo CurrentUsage rises by at least the requested size when a large resource is created and rendered and falls when released; Budget is non-zero and AvailableForReservation is consistent; SetVideoMemoryReservation within bounds is accepted. |
| `sanity.dxgi.mem.offer_reclaim` | OfferResources then ReclaimResources returns DXGI_RECLAIM_RESOURCE_RESULT_OK with content intact or DISCARDED (content then undefined), never an error for valid resources, and a re-use after reclaim renders. |
| `sanity.dxgi.mem.trim_and_priority` | IDXGIDevice3::Trim, SetGPUThreadPriority(-7..7, out of range fails) and GetGPUThreadPriority round-trip and rendering still works. |
| `sanity.dxgi.overlay.caps_consistent` | CheckOverlaySupport, CheckOverlayColorSpaceSupport, SupportsOverlays and Output6 flags agree with each other and with the MPO plane count the Win32 display configuration (D3DKMT/DXGI plane caps) reports, and a swap chain with the supported overlay format gets a plane when requested. |
| `sanity.dxgi.overlay.mpo_present` | A video-format swap chain on a supported overlay path displays correctly through DWM with all frames in order; an unsupported combination falls back to composition rather than failing. |
| `sanity.dxgi.debug.no_messages` | A full run of valid DXGI/D3D calls under the debug layer (IDXGIInfoQueue) produces no ERROR or CORRUPTION messages; known-invalid calls produce the expected message categories. |
| `sanity.dxgi.device.removal_events` | RegisterDeviceRemovedEvent style notification fires on device removal and GetDeviceRemovedReason is documented; for a healthy device the event does not fire during a 5 s render loop. |
| `sanity.dxgi.present.duplication_visible` | IDXGIOutputDuplication AcquireNextFrame returns frames whose pixels match a topmost known-colour window and ReleaseFrame releases; access lost errors are documented and recoverable. |

### 4.12 Direct3D 12 (45)

#### Device and options

| check | contract |
|---|---|
| `sanity.d3d12.dev.create_levels` | D3D12CreateDevice at each feature level (11_0 to 12_2) returns S_OK iff the level is supported according to CheckFeatureSupport(FEATURE_LEVELS) and the adapter's LUID equals the DXGI one. |
| `sanity.d3d12.dev.options_consistent` | Every OPTIONS struct the build's headers define (OPTIONS through OPTIONS18), ARCHITECTURE1, GPU_VIRTUAL_ADDRESS_SUPPORT, SHADER_MODEL and ROOT_SIGNATURE answers are mutually consistent (tiers in range, shader-model prerequisites of wave, 16-bit, raytracing, mesh and feedback features, FL12_1 and FL12_2 required tiers), an unknown feature or a wrong structure size fails, and any feature a check exercises works when reported. |
| `sanity.d3d12.dev.format_support` | CheckFeatureSupport(FORMAT_SUPPORT / FORMAT_INFO / MULTISAMPLE_QUALITY_LEVELS) per DXGI_FORMAT agree with resource and view creation and rendering, and with the D3D11 answers for the same format. |
| `sanity.d3d12.dev.node_and_uma` | GetNodeCount, Architecture (UMA, CacheCoherentUMA) and heap property queries are consistent with each other and with GetCustomHeapProperties. |

#### Heaps and resources

| check | contract |
|---|---|
| `sanity.d3d12.heap.default_upload_readback` | Upload through UPLOAD heap, copy to DEFAULT, copy to READBACK, read: round trip equals input for sizes from 1 byte to 128 MiB and odd alignments. |
| `sanity.d3d12.heap.custom_properties` | CUSTOM heaps accept each CPUPageProperty/MemoryPoolPreference pair permitted by the architecture and reject others; the UMA/cache-coherent flags decide validity; accepted heaps map and round-trip data. |
| `sanity.d3d12.heap.placed_alignment` | Placed resources at heap offsets follow GetResourceAllocationInfo alignment and size (64 KiB, 4 MiB for MSAA by default), resources placed back to back and at the heap end keep their own data; misaligned or oversize placements are invalid usage and are not submitted. |
| `sanity.d3d12.heap.aliasing` | Two placed resources aliasing the same heap range, used with an aliasing barrier between, behave per spec: render to one, discard, then fully write the other and read it correctly. |
| `sanity.d3d12.heap.reserved_tiles` | Reserved resources with UpdateTileMappings for many ranges (including re-mapping, null mappings, shared tiles) read back the mapped data, null-mapped tiles read zero per tier, and CopyTileMappings preserves them. |
| `sanity.d3d12.heap.committed_flags` | Committed resources with each HEAP_FLAG (ALLOW_ONLY_*, DENY_*, SHARED, CREATE_NOT_ZEROED) and resource flag creation matches the documented combination matrix; newly created non-NOT_ZEROED resources read as zero. |
| `sanity.d3d12.heap.zeroed_new` | A freshly created committed resource, and a resource placed in a newly created heap, never exposes stale data from other resources or earlier processes (reads zero or application-defined per spec). |
| `sanity.d3d12.heap.exhaustion` | [E]: allocation beyond available memory fails with E_OUTOFMEMORY, the device is not removed, and later allocations succeed after release. |

#### Map semantics and copies

| check | contract |
|---|---|
| `sanity.d3d12.map.persistent` | A mapping of an UPLOAD or READBACK resource remains valid and its pointer stable for the heap's lifetime, across many ExecuteCommandLists and multiple Map/Unmap pairs. |
| `sanity.d3d12.map.upload_visible` | CPU writes to an UPLOAD buffer before ExecuteCommandLists are visible to GPU reads in that submission; writes after a fence wait and before the next submit are visible to the next one. |
| `sanity.d3d12.map.readback_after_fence` | READBACK data is visible and correct after the fence for the copy is signalled; mapped ranges with read range empty or partial behave per spec. |
| `sanity.d3d12.copy.footprints` | GetCopyableFootprints for textures (all formats incl. BC, array, mip, 3D) reports offsets, RowPitch aligned to 256 and total size such that CopyTextureRegion with those footprints round-trips data exactly. |
| `sanity.d3d12.copy.region_variants` | CopyBufferRegion, CopyTextureRegion with boxes, CopyResource, ResolveSubresource and ResolveSubresourceRegion deliver exact data in all valid state/format combinations and typeless-cast cases. |

#### Queues, fences, barriers

| check | contract |
|---|---|
| `sanity.d3d12.queue.types` | Direct, compute and copy queues each execute lists they are allowed to (copy lists on all three, compute lists on direct and compute), report their type in GetDesc, and a high priority queue works when the priority is reported as supported; unsupported commands on a list type are invalid usage and are not recorded. |
| `sanity.d3d12.queue.many_lists` | One ExecuteCommandLists with 1, 64 and 1000 lists runs them in order, and bundles executed from direct lists produce results equal to inline commands. |
| `sanity.d3d12.fence.cross_queue` | Signal on a copy queue and Wait on a direct queue orders their work; results of the copy are visible to the draw; also compute-to-direct and direct-to-copy. |
| `sanity.d3d12.fence.cpu_semantics` | The queue waits on a fence the CPU signals 3 s after the submit, longer than the kernel's 2 s ring lockup timeout: the work queued behind the wait does not complete before the signal and completes after it, a bounded wait times out meanwhile, and a second process rendering and reading back meanwhile completes every cycle in a normal time. SetEventOnCompletion fires only once the value is reached (immediately if already), GetCompletedValue is monotonic, CPU Signal from the application raises it, signalling a lower value leaves the completed value or sets it, a queue wait on a CPU-signalled fence continues once it is signalled, and 10000 fence waits complete without deadlock. |
| `sanity.d3d12.fence.shared` | A shared fence (NT handle) signalled in process A completes a wait in process B and vice versa, where reported by caps; handle closing leaves the other side valid. |
| `sanity.d3d12.barrier.transition_uav` | Transition and UAV barriers between write-then-read passes make all writes visible (compute writes then pixel shader reads); 1000 chained passes give the CPU-referenced result. |
| `sanity.d3d12.barrier.enhanced` | If EnhancedBarriersSupported, texture and buffer barriers with syncs/access/layouts transform data correctly and layouts across queues work; otherwise CreateCommandList7 / Barrier fails consistently with the caps. |
| `sanity.d3d12.barrier.misuse_tolerance` | COMMON promotion and decay for buffers and simultaneous-access textures without barriers, split begin/end barriers, per-subresource transitions and a null aliasing barrier behave as documented, not as errors. |

#### Descriptors, root signatures, pipelines

| check | contract |
|---|---|
| `sanity.d3d12.desc.heaps_and_tables` | CBV/SRV/UAV and sampler descriptor heaps (shader-visible and not), CopyDescriptors and bindless tables of large size give correct resource access; heap sizes up to the tier's limit work. |
| `sanity.d3d12.root.signatures` | Root signature 1.0 and 1.1 serialize/deserialize round trip and convert between versions; root constants, root descriptors, tables, static samplers, descriptor/data volatility all produce the analytic result in a test shader. |
| `sanity.d3d12.pso.graphics_compute` | Graphics PSOs (all state fields, MSAA, depth, blend with logic op) and compute PSOs render or compute the analytic or CPU reference result. |
| `sanity.d3d12.pso.library_cache` | A pipeline library stores and reloads PSOs; a loaded PSO produces the same output; a corrupted or driver-mismatched blob yields D3D12_ERROR_DRIVER_VERSION_MISMATCH / ADAPTER_NOT_FOUND / INVALID_ARG not a crash; cached PSO blobs in desc work or are ignored. |
| `sanity.d3d12.render.draw_variants` | DrawInstanced, DrawIndexedInstanced, ExecuteIndirect (draw, indexed draw, dispatch, constant, constant buffer, vertex and index buffer arguments, with count buffers and offsets) and indirect argument buffers match the analytic image. |
| `sanity.d3d12.render.msaa_resolve` | MSAA render with programmable sample positions (if tier reports) and resolve equals WARP. |

#### Queries, timing, present

| check | contract |
|---|---|
| `sanity.d3d12.query.occlusion_pipeline` | Occlusion (binary and counting) and pipeline-statistics queries resolve to analytic counts through ResolveQueryData. |
| `sanity.d3d12.query.timestamp_calibration` | GetTimestampFrequency is non-zero, GetClockCalibration pairs GPU and CPU (QPC) timestamps whose conversions agree within a few ms with a CPU stopwatch around a known-length wait, and timestamps in a list are non-decreasing. [E] |
| `sanity.d3d12.present.dxgi_queue` | A swap chain created on a D3D12 direct queue (FLIP_DISCARD, 2-4 buffers) presents distinct frames in order, back buffers need PRESENT state, and ResizeBuffers after idle works (frames are read from the screen). [E] |

#### Sharing, residency, optional features, removal

| check | contract |
|---|---|
| `sanity.d3d12.share.heap_resource_nt` | Shared heaps and committed resources with CreateSharedHandle open in a second device and process (OpenSharedHandle) and show the writer's data after fence ordering, when shared tier is reported; D3D11 and D3D12 opening the same texture agree. |
| `sanity.d3d12.share.keyed_cross_api` | A D3D11 texture shared with NT handle written by D3D11 under a keyed mutex is readable by D3D12 and vice versa, with the D3D11 side ordered by the mutex keys and the D3D12 side by CPU waits (D3D12 has no keyed mutex). |
| `sanity.d3d12.resid.make_evict` | Evict then MakeResident (and EnqueueMakeResident with a fence) of heaps and committed resources preserves contents on UMA and non-UMA per spec; MakeResident beyond the budget fails with E_OUTOFMEMORY rather than removing the device. [E] |
| `sanity.d3d12.resid.budget_pressure` | [E]: allocating beyond the video memory budget while evicting retained resources keeps all data intact and QueryVideoMemoryInfo within Budget at the end. |
| `sanity.d3d12.opt.raytracing` | If RaytracingTier is reported, a BLAS and a TLAS for a known scene build with sizes inside the prebuild info and a compacted size not above the current size; tracing rays (exact hit and miss results against an analytic CPU intersection) needs DXIL and runs only when a DXIL compiler is available to the build, otherwise the row reports it. |
| `sanity.d3d12.opt.mesh_shaders` | MeshShaderTier is consistent with shader model 6.5; the amplification+mesh pipeline render (analytic result) needs DXIL and runs only when a DXIL compiler is available to the build, otherwise the row skips with that reason. |
| `sanity.d3d12.opt.vrs_sampler_feedback` | VRS tiers (per-draw, image, combiners) shade rates are respected on known inputs (fine pattern at 1x1, uniform 2x2 blocks when coarse); a sampler feedback map is created when the tier is reported and rejected when it is not; recording the sampled mips needs DXIL and is not run. |
| `sanity.d3d12.opt.work_graphs_dxr11` | Optional features expressible in SM 5.1 when reported give results equal to a CPU or analytic reference (typed UAV loads of the additional formats, rasterizer ordered views accumulating in primitive order, conservative rasterization coverage), and unreported ones fail at PSO creation; wave ops, 16-bit and 64-bit ops and work graphs need DXIL and are not run. |
| `sanity.d3d12.removal.reasons` | After a device is removed (by documented test hook or a deliberate fault), GetDeviceRemovedReason returns a documented DXGI_ERROR_*, further submissions fail without hang, and a new device can be created on the same adapter. [E] |
| `sanity.d3d12.stress.lifetime_exit` | Create/destroy loops of devices, queues, lists, heaps and fences with outstanding GPU work, and process exit with queued work, leave video memory usage at baseline (monitored from a second process). |
| `sanity.d3d12.stress.concurrent_queues` | Four threads each submitting to its own queue with shared resources under fence ordering for 5 s produce the expected sums with no device removal. |

### 4.13 OpenGL (51)

#### Pixel formats and context creation

| check | contract |
|---|---|
| `sanity.gl.pixelformat.enumerate_choosable` | Every format index up to DescribePixelFormat's count describes consistently (bits, flags) and SetPixelFormat on a fresh window succeeds for each one that has PFD_SUPPORT_OPENGL and PFD_DRAW_TO_WINDOW. |
| `sanity.gl.pixelformat.every_format_renders` | For each choosable format a context clears to a known colour, draws a triangle and SwapBuffers/glReadPixels returns the analytic result within the format's channel precision. |
| `sanity.gl.pixelformat.choose_matches_request` | ChoosePixelFormat for requested colour/depth/stencil/double-buffer returns a format whose described bits are at least the request, and wglChoosePixelFormatARB results satisfy every exact/minimum attribute passed. |
| `sanity.gl.pixelformat.arb_attribs_agree_with_describe` | wglGetPixelFormatAttribivARB values for colour, depth, stencil, double-buffer and swap-method agree with DescribePixelFormat for every ICD (non-generic) format. |
| `sanity.gl.pixelformat.set_twice_fails` | SetPixelFormat on a window that already has a format fails, and the first format stays in effect. |
| `sanity.gl.pixelformat.srgb_and_float` | If WGL_ARB_framebuffer_sRGB / WGL_ARB_pixel_format_float are advertised, formats with those attributes exist and render with the correct encoding (sRGB blend result, float values beyond 1.0 kept); if not advertised, no format reports them. |
| `sanity.gl.context.legacy_create` | wglCreateContext on a valid-format DC returns a context that is current-able, reports GL_VERSION >= 1.1 and works after wglMakeCurrent/unmake cycles. |
| `sanity.gl.context.arb_version_profile` | wglCreateContextAttribsARB for each version 3.2 to 4.6 core and compatibility either grants a context whose GL_MAJOR/MINOR_VERSION is >= requested with matching profile mask, or fails with the documented ERROR_INVALID_VERSION_ARB / ERROR_INVALID_PROFILE_ARB. |
| `sanity.gl.context.flags_granted_or_error` | Debug, forward-compatible and robust-access flags requested are reflected in GL_CONTEXT_FLAGS when the context is created; forward-compatible contexts reject removed state such as wide lines with INVALID_VALUE; glBegin is a measure. |
| `sanity.gl.context.robustness_reset` | With ARB_create_context_robustness, glGetGraphicsResetStatus on a healthy context returns NO_ERROR and the GL_RESET_NOTIFICATION_STRATEGY query matches the requested strategy. |
| `sanity.gl.context.no_error` | If KHR_no_error is advertised a no-error context creates and renders valid work correctly; if not advertised, requesting the flag fails with the documented error. |
| `sanity.gl.context.share_lists` | Objects (texture, buffer, shader, program) created in one context are usable in a context joined with wglShareLists or the share argument, and deleting in one leaves the other's use valid until it unbinds. |
| `sanity.gl.context.non_shared_isolation` | Objects in unshared contexts are not visible to each other: binding a name from another group yields no content / GL_INVALID_OPERATION as specified; container objects (VAOs, FBOs) are not shared even in a group. |
| `sanity.gl.context.share_across_threads` | A texture written by one thread's context and made visible via glFinish/fence is read correctly by a shared context on a second thread. |
| `sanity.gl.context.make_current_rules` | A context current on one thread cannot be made current on another (ERROR_BUSY / failure documented), and becomes available after release; releasing with wglMakeCurrent(NULL,NULL) leaves GL calls as no-ops. |
| `sanity.gl.context.many_contexts` | Creating and destroying a few hundred contexts and windows in a loop in one process leaves a final context fully functional. |

#### Buffers

| check | contract |
|---|---|
| `sanity.gl.buffer.storage_flag_matrix` | For every valid combination of MAP_READ/WRITE, PERSISTENT, COHERENT, DYNAMIC_STORAGE, CLIENT_STORAGE, glBufferStorage succeeds and invalid combinations (COHERENT without PERSISTENT) give INVALID_VALUE. |
| `sanity.gl.buffer.persistent_coherent_gpu_write` | A coherent persistent mapping sees a GPU write (compute/transform feedback/copy) after the documented fence sync, without any flush or barrier call. |
| `sanity.gl.buffer.persistent_noncoherent_flush` | A non-coherent persistent mapping sees host writes on the GPU after FlushMappedBufferRange (or MemoryBarrier with CLIENT_MAPPED_BUFFER_BARRIER_BIT) plus the required sync, and sees GPU writes after MemoryBarrier and fence. |
| `sanity.gl.buffer.dynamic_storage_subdata` | BufferSubData succeeds only with DYNAMIC_STORAGE_BIT (else INVALID_OPERATION) and its content is exactly what was written, for offsets/sizes across the buffer including unaligned and zero-size. |
| `sanity.gl.buffer.map_range_flags` | MapBufferRange with INVALIDATE_RANGE/INVALIDATE_BUFFER keeps content outside the invalidated range, UNSYNCHRONIZED with fencing yields correct data, and FLUSH_EXPLICIT publishes only flushed ranges after UnmapBuffer. |
| `sanity.gl.buffer.map_errors` | Mapping beyond size, with no read/write bit, with both INVALIDATE and READ, or mapping twice, produces the documented errors; UnmapBuffer returns GL_TRUE on intact data. |
| `sanity.gl.buffer.copy_and_clear` | CopyBufferSubData (incl. overlapping same-buffer rejected, adjacent allowed) and ClearBufferSubData produce the CPU-reference bytes for many sizes and offsets including large (hundreds of MB if memory limits report it). |
| `sanity.gl.buffer.pbo_pack_unpack` | glReadPixels into a PBO and glTexSubImage2D from a PBO equal their client-memory equivalents in the pixel-group bytes for all format/type pairs, with non-default PACK/UNPACK row length, alignment and skip; bytes outside the pixel groups stay untouched, and the pixels match the uploaded image analytically. |

#### Textures, framebuffers and draws

| check | contract |
|---|---|
| `sanity.gl.texture.all_targets_roundtrip` | 1D, 2D, 3D, cube, arrays, rectangle, buffer and multisample targets each accept upload and return identical data via GetTexImage or a sampling shader, where supported. |
| `sanity.gl.texture.compressed_formats` | Every advertised compressed internal format round-trips: CompressedTexImage then GetCompressedTexImage returns identical blocks, and sampling matches a CPU decoder within the format's tolerance. |
| `sanity.gl.texture.generate_mipmap` | GenerateMipmap for each target yields level counts and (for box-filterable formats) values within tolerance of a CPU box filter. |
| `sanity.gl.texture.views` | TextureView over mip/layer subranges and compatible formats samples the same texels as the parent, and parent writes are visible through the view. |
| `sanity.gl.texture.immutable_storage` | After TexStorage* the format and size cannot change (INVALID_OPERATION on TexImage), SubImage works at every level, and level count follows the formula. |
| `sanity.gl.texture.max_size_limits` | Textures at GL_MAX_TEXTURE_SIZE and GL_MAX_3D/ARRAY limits are creatable and correct at the corners; one past the limit fails with INVALID_VALUE and the proxy query reports failure. |
| `sanity.gl.fbo.completeness_matrix` | glCheckFramebufferStatus returns COMPLETE for every attachment format combination the spec requires and the documented INCOMPLETE_* code otherwise (with GL 4.1 or GL_ARB_ES2_compatibility the draw/read buffer rules are gone and COMPLETE is expected); renders to every complete configuration are correct. |
| `sanity.gl.fbo.blit` | BlitFramebuffer between FBOs and default framebuffer scales, flips, converts format and resolves multisample with the same pixels as a CPU reference (nearest exact, linear within 1 LSB). |
| `sanity.gl.draw.core_vs_compat_equal` | The same scene drawn through a core context (VAO/VBO) and through compatibility immediate/vertex-array paths produces equal pixels, and equals an analytic or Vulkan-twin render of the same geometry. |
| `sanity.gl.draw.instanced_indirect` | DrawArraysInstanced, DrawElementsBaseVertex, MultiDrawIndirect and glDrawTransformFeedback render identical results to a CPU-expanded plain draw. |
| `sanity.gl.draw.large_batches` | Tens of thousands of draws and a vertex buffer of several million vertices render the exact expected coverage and finish without timeouts. |

#### Sync, queries, swap, threads, extensions, errors

| check | contract |
|---|---|
| `sanity.gl.sync.fence_ordering` | A fence inserted after a draw is signalled only once the draw's results are readable; ClientWaitSync with FLUSH_COMMANDS_BIT returns ALREADY_SIGNALED/CONDITION_SATISFIED, never TIMEOUT_EXPIRED, for a bounded workload. |
| `sanity.gl.sync.timeout_semantics` | ClientWaitSync with timeout 0 on an unsignalled fence returns TIMEOUT_EXPIRED immediately, and a timeout of N ns waits at least close to N before expiring; WaitSync does not block the CPU. |
| `sanity.gl.sync.finish_flush` | glFinish returns only when all prior commands completed (readback of a long shader is final without further sync); glFlush returns promptly and work eventually completes without further commands. |
| `sanity.gl.query.timer_samples_primitives` | TIME_ELAPSED is non-negative and nondecreasing with workload size, SAMPLES_PASSED matches the analytic covered-pixel count, PRIMITIVES_GENERATED and TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN match the issued primitives. |
| `sanity.gl.query.availability_rules` | Query results become available eventually, QUERY_RESULT_AVAILABLE is false-then-true only monotonically, and nested/active query misuse gives the documented INVALID_OPERATION. |
| `sanity.gl.swap.interval_semantics` | wglSwapIntervalEXT(n) returns TRUE for 0, 1, 2 and for -1 only if WGL_EXT_swap_control_tear is advertised, wglGetSwapIntervalEXT returns the set value, interval 1 presents no faster than 0.9 refresh periods and interval 2 no faster than 1.8; interval 0 is a measure. |
| `sanity.gl.swap.swapbuffers_content` | SwapBuffers presents the back buffer exactly (frame number encoded in pixels), and with PFD_SWAP_EXCHANGE/COPY semantics the back buffer content after swap follows the format's reported swap method. |
| `sanity.gl.swap.resize_and_dc_lifetime` | Resizing a GL window between swaps keeps the framebuffer matching the client size (glViewport query, readback size) and rendering stays correct; releasing and re-getting the DC does not lose the context. |
| `sanity.gl.thread.concurrent_contexts` | Several threads each with their own context drawing to their own FBOs for seconds produce each their own correct image with no cross-talk. |
| `sanity.gl.thread.producer_consumer` | A producer thread uploading textures/buffers and a consumer sampling them via share group and fences never observes torn or stale data after the fence is signalled. |
| `sanity.gl.memobj.external_memory_caps` | If GL_EXT_memory_object(_win32) is advertised, the device LUID matches the adapter, and importing Vulkan-exported OPAQUE_WIN32 memory succeeds with a GL_DEVICE_UUID_EXT equal to the Vulkan deviceUUID; importing a Direct3D 11 NT shared texture handle (GL_HANDLE_TYPE_D3D11_IMAGE_EXT) is a measure (the semaphore hand-off ordering is not exercised); if unadvertised, entry points are absent or error. |
| `sanity.gl.ext.string_matches_entry_points` | Every extension listed in GL_EXTENSIONS and WGL extension strings that has a known entry point list has all of them resolve through wglGetProcAddress, and the glIs* entry points answer glIs*(0) with GL_FALSE and no error. |
| `sanity.gl.ext.unadvertised_not_supported` | Features gated by extensions not advertised (e.g. KHR_debug, ARB_compute_shader on older versions, tessellation) are not usable: the corresponding calls give errors or the context version lacks them, consistently with GL_VERSION. |
| `sanity.gl.ext.version_implies_core_functions` | For the reported GL_VERSION, all core functions of that version (and below) resolve and the corresponding GL_MAX_* minimum limits from the spec hold. |
| `sanity.gl.error.getError_contracts` | Classic invalid calls (bad enum, bad value, bad operation, stack over/underflow, out of range framebuffer op) set exactly the specified error, GetError clears it, and the context renders correctly afterwards. |
| `sanity.gl.error.debug_output` | In a debug context with KHR_debug a deliberate error delivers a callback with matching type/severity synchronously when DEBUG_OUTPUT_SYNCHRONOUS is enabled, and clean work delivers no error-type messages. |

### 4.14 OpenCL (40)

#### Platform, device and queries

| check | contract |
|---|---|
| `sanity.cl.platform.query_consistent` | clGetPlatformIDs/Info report profile, version string format "OpenCL x.y ...", extensions and ICD suffix consistent with the device versions (platform version >= each device version). |
| `sanity.cl.device.query_all_params` | Every clGetDeviceInfo parameter valid for the reported version returns a value of the documented size, with spec minimum limits satisfied for the device type. |
| `sanity.cl.device.extensions_match_entry_points` | Each advertised extension's functions resolve via clGetExtensionFunctionAddressForPlatform and unadvertised ones (cl_khr_fp64, cl_khr_gl_sharing etc.) are unusable (build fails or entry absent). |
| `sanity.cl.device.limits_honoured` | A kernel using the maximum work-group size, max local memory, max constant buffer, and max work-item dimensions reported works, and one over the limit gives CL_INVALID_WORK_GROUP_SIZE / CL_OUT_OF_RESOURCES as documented. |
| `sanity.cl.device.both_adapters` | Each OpenCL device (one per adapter) runs the same vector-add and reduction and matches the CPU reference, concurrently from two threads. |
| `sanity.cl.device.create_sub_devices_error` | clCreateSubDevices on a device without partitioning returns CL_INVALID_VALUE or CL_DEVICE_PARTITION_FAILED per spec (and success with valid subdevices if partition properties are reported). |

#### Memory objects and transfers

| check | contract |
|---|---|
| `sanity.cl.mem.use_host_ptr_coherence` | With CL_MEM_USE_HOST_PTR, the host pointer contains the device's results after a blocking map or an unmap-then-finish, and host writes between map and unmap are visible to kernels afterwards. |
| `sanity.cl.mem.use_host_ptr_blocking_ops` | After blocking clEnqueueReadBuffer/WriteBuffer on a USE_HOST_PTR buffer using other memory, that data is correct and the host pointer is coherent at the next map as the spec defines. |
| `sanity.cl.mem.alloc_host_ptr` | ALLOC_HOST_PTR buffers map to usable host memory, writes through the mapping are seen by kernels after unmap, and kernel writes visible after map. |
| `sanity.cl.mem.copy_host_ptr` | COPY_HOST_PTR initialises the buffer with the host data at creation time and later host changes to the source do not affect it. |
| `sanity.cl.mem.access_flags_honoured` | READ_ONLY, WRITE_ONLY, HOST_NO_ACCESS, HOST_READ_ONLY, HOST_WRITE_ONLY: invalid host operations fail with CL_INVALID_OPERATION on map and valid ones succeed; invalid flag combinations fail at creation with CL_INVALID_VALUE. |
| `sanity.cl.mem.sub_buffer_alignment` | clCreateSubBuffer requires origin aligned to CL_DEVICE_MEM_BASE_ADDR_ALIGN (misaligned gives CL_MISALIGNED_SUB_BUFFER_OFFSET), aligned sub-buffers alias the parent: writes via either are visible to the other. |
| `sanity.cl.transfer.read_write_blocking_nonblocking` | Read/Write buffer in blocking and non-blocking forms (with events) produce exact data across sizes from 1 byte to hundreds of MB; non-blocking data is valid only after the event completes. |
| `sanity.cl.transfer.rect_and_fill_copy` | ReadBufferRect/WriteBufferRect/CopyBufferRect with arbitrary pitches/origins, FillBuffer with all pattern sizes, and CopyBuffer equal the CPU model; overlapping copies fail with CL_MEM_COPY_OVERLAP. |
| `sanity.cl.transfer.map_unmap_ranges` | MapBuffer with READ, WRITE, WRITE_INVALIDATE_REGION and sub-ranges gives pointers whose content and write-back obey the flags; multiple simultaneous non-overlapping maps work. |
| `sanity.cl.transfer.migrate_mem_objects` | clEnqueueMigrateMemObjects (including CONTENT_UNDEFINED) leaves buffer contents intact except when undefined is requested, and kernels afterwards read correct data. |
| `sanity.cl.image.formats_supported` | For each image type and the full list from clGetSupportedImageFormats, create, write, read back (and read in a kernel) round-trips values within format precision; formats required by the spec for the profile are present. |
| `sanity.cl.image.transfers_rect_fill` | Read/Write/Copy/Fill image for 1D, 1D array, 2D, 2D array, 3D with row/slice pitches and sub-regions match the CPU model; out-of-bounds region gives CL_INVALID_VALUE. |
| `sanity.cl.image.from_buffer` | An image created from a buffer (1D buffer, 2D from buffer per 2.0/3.0 rules) sees buffer writes and vice versa, honouring pitch alignment; invalid pitch fails as documented. |
| `sanity.cl.svm.coarse_grain` | clSVMAlloc coarse-grain memory is correct after SVMMap/SVMUnmap, kernels receive it via SetKernelArgSVMPointer, and MemFill/Memcpy produce exact data; free after finish is clean. |
| `sanity.cl.svm.fine_grain_caps` | If fine-grain buffer (or system) SVM is reported, host and kernel concurrent access with atomics (if reported) is coherent; otherwise clSVMAlloc with those flags returns NULL and capability bits are consistently clear. |

#### Programs, kernels, events, queues

| check | contract |
|---|---|
| `sanity.cl.program.build_source_options` | Source builds with -D, -I, -cl-std=CLx.y (up to the supported version), -cl-fast-relaxed-math and clGetProgramBuildInfo reports SUCCESS; unsupported -cl-std fails with CL_INVALID_BUILD_OPTIONS. |
| `sanity.cl.program.build_errors_logs` | Invalid source returns CL_BUILD_PROGRAM_FAILURE with a non-empty build log naming the line; kernels cannot be created from a failed program (CL_INVALID_PROGRAM_EXECUTABLE). |
| `sanity.cl.program.binary_roundtrip` | clGetProgramInfo binaries reload through clCreateProgramWithBinary, report binary status success and the kernels give identical results to the source build; a corrupted binary fails with CL_INVALID_BINARY. |
| `sanity.cl.program.il_spirv_caps` | If CL_DEVICE_IL_VERSION reports SPIR-V, a valid module builds via clCreateProgramWithIL and runs correctly; if empty, CreateProgramWithIL fails with CL_INVALID_OPERATION. |
| `sanity.cl.program.compile_link` | clCompileProgram with headers plus clLinkProgram of two units yields working kernels; unresolved symbols fail with CL_LINK_PROGRAM_FAILURE and a log. |
| `sanity.cl.kernel.atomics_vs_cpu` | 32-bit (and 64-bit if reported) atomic add/min/max/cmpxchg/xchg from thousands of work-items on global and local memory give exact CPU-computed totals. |
| `sanity.cl.kernel.local_memory_barrier` | Work-group reductions/scans/transposes using local memory and barrier(CLK_LOCAL_MEM_FENCE) match the CPU reference for several group sizes including non-power-of-two. |
| `sanity.cl.kernel.arithmetic_vs_reference` | Integer, float builtins (within the spec ULP tolerances), vector types and vload/vstore on all widths equal the CPU reference; division by zero and conversions follow the C99/OpenCL rules. |
| `sanity.cl.kernel.doubles_caps` | If cl_khr_fp64 / CL_DEVICE_DOUBLE_FP_CONFIG is reported, double kernels match the CPU within spec ULPs; otherwise building a kernel using double fails. |
| `sanity.cl.kernel.images_in_kernels` | read_image{f,i,ui}/write_image in kernels with each sampler combination (normalized, address mode, filter) return values matching the analytic sampling model. |
| `sanity.cl.kernel.args_and_ndrange` | SetKernelArg errors (CL_INVALID_ARG_SIZE, CL_INVALID_ARG_INDEX, unset args CL_INVALID_KERNEL_ARGS), global offset, non-uniform work-groups (3.0) and 3D ranges cover each work-item exactly once. |
| `sanity.cl.kernel.large_workloads` | Kernels over very large global sizes (tens of millions of items) and long running (seconds) complete without the device being lost, and results are exact. |
| `sanity.cl.event.in_order_ordering` | In an in-order queue, commands complete in enqueue order: a chain of dependent kernels and copies gives the sequential result with no explicit events. |
| `sanity.cl.event.out_of_order_deps` | In an out-of-order queue (if supported) commands ordered only via wait lists and barriers/markers produce the correct dependency result, and independent commands may run in any order but all complete. |
| `sanity.cl.event.user_events_callbacks` | A command waiting on a user event does not run until clSetUserEventStatus; a negative status propagates failure to dependents; callbacks fire exactly once for each requested status. |
| `sanity.cl.event.profiling_monotonic` | With PROFILING_ENABLE, QUEUED <= SUBMIT <= START <= END for each event, a dependent command's START >= its dependency's END, and durations scale with workload size. |
| `sanity.cl.queue.multiple_queues_concurrent` | Several queues on one device and on two devices running dependent work linked by events produce the correct final data under concurrency from multiple threads. |
| `sanity.cl.queue.finish_flush_release` | clFinish returns only after all work completes; releasing queues/events/memory with commands still in flight neither crashes nor corrupts other work. |
| `sanity.cl.error.codes_documented` | Representative invalid calls (null context, bad handle, size 0 buffer, too-large allocation, bad flags) return the spec's error codes and the context remains usable. |

### 4.15 Vulkan (41)

#### Instance, device and memory

| check | contract |
|---|---|
| `sanity.vk.instance.version_layers_extensions` | vkEnumerateInstanceVersion >= 1.1, instance creation at the reported apiVersion succeeds, requesting an unsupported extension/layer returns VK_ERROR_EXTENSION/LAYER_NOT_PRESENT, and each advertised instance function resolves. |
| `sanity.vk.device.props_features_consistent` | Properties2/Features2 chains agree with the basic structures (limits, UUIDs, LUID, Vulkan11/12 structs), the device apiVersion has major version 1, and each feature reported true can be enabled at vkCreateDevice while each false feature enabled fails with VK_ERROR_FEATURE_NOT_PRESENT; an unsupported device extension fails with VK_ERROR_EXTENSION_NOT_PRESENT. |
| `sanity.vk.device.limits_minimums_honoured` | All reported limits meet the spec minimums, and work at the stated limit (max push constants, descriptors, workgroup size, image dimension, viewport) executes correctly. |
| `sanity.vk.device.queue_families` | Queue family flags, counts and timestampValidBits are consistent; a queue of each family accepts only commands matching its flags with the right synchronisation and submits work to completion. |
| `sanity.vk.device.extensions_match_functions` | Every advertised device extension's functions resolve through vkGetDeviceProcAddr and its feature structs chain without error. |
| `sanity.vk.memory.every_type_allocates` | Each memory type allocates (1 MiB and a large chunk of up to 128 MiB, 1/4 of the heap and the budget at most), binds to a buffer and image of compatible memoryTypeBits, and frees; requirement size and alignment are valid. |
| `sanity.vk.memory.host_visible_map` | HOST_VISIBLE types map aligned to minMemoryMapAlignment, written data is readable back through a copy to a device buffer and readback, contents survive unmap and map, and a partial map writes the right bytes. |
| `sanity.vk.memory.host_coherent_no_flush` | In HOST_COHERENT memory, host writes are seen by a submitted GPU copy and GPU writes by the host after fence wait, with no flush/invalidate calls. |
| `sanity.vk.memory.noncoherent_flush_invalidate` | In non-coherent HOST_VISIBLE memory, flushing mapped ranges (aligned to nonCoherentAtomSize) publishes host writes to the device and invalidating makes device writes visible. |
| `sanity.vk.memory.device_local_and_budget` | DEVICE_LOCAL allocations work; with VK_EXT_memory_budget the reported heapUsage rises on allocation and falls after free, never exceeds heapBudget with successful allocations tracked. [E] |
| `sanity.vk.memory.allocation_limits` | [E]: allocating maxMemoryAllocationCount (capped at 4096) small blocks succeeds and frees; repeated allocation of up to heap size chunks either succeeds or fails only with VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_OUT_OF_HOST_MEMORY or VK_ERROR_TOO_MANY_OBJECTS; the device works after freeing. |
| `sanity.vk.memory.dedicated_and_aliasing` | Dedicated allocation requirements are consistent, and aliased buffers on one allocation with proper barriers see each other's data. |

#### Buffers, images, transfers

| check | contract |
|---|---|
| `sanity.vk.buffer.all_usages` | A buffer with every legal usage-flag set creates, binds, and each usage (transfer, uniform, storage, texel, index, vertex, indirect) works; size and alignment requirements are honoured. |
| `sanity.vk.image.formats_features_usage` | For each format with reported format features, an image with the matching tiling/usage creates and the feature (sample, storage, attachment, blit, filter) works; not-reported combinations fail in vkGetPhysicalDeviceImageFormatProperties with FORMAT_NOT_SUPPORTED. |
| `sanity.vk.image.types_layers_mips` | 1D, 2D, 3D, cube, array, multisample images with all mip levels create and each subresource is individually writable and readable. |
| `sanity.vk.image.sparse_caps` | If sparseBinding/sparseResidency features are reported, binding and unbinding pages and residency queries work as specified; otherwise creating sparse resources fails. |
| `sanity.vk.transfer.copy_all_forms` | vkCmdCopyBuffer, CopyImage, buffer<->image copies with non-trivial offsets, rowLength and imageHeight equal the CPU model across formats and aspects. |
| `sanity.vk.transfer.blit_resolve` | BlitImage (nearest exact, linear within 1 LSB, flips, scaling) and ResolveImage match the CPU reference. |
| `sanity.vk.transfer.clear_fill_update` | CmdClearColorImage, ClearDepthStencil, FillBuffer and UpdateBuffer (up to 65536 bytes, 4-byte aligned) leave exactly the specified bytes and no more. |

#### Compute, graphics, synchronisation

| check | contract |
|---|---|
| `sanity.vk.compute.vs_cpu` | Compute shaders (SSBO/UBO/push constant/shared memory/atomic/subgroup per caps) produce results equal to the CPU for several dispatch sizes including indirect and zero-size. |
| `sanity.vk.graphics.vs_twin` | A render pass (and dynamic rendering) scene with depth, blending, MSAA, BGRA targets and two colour attachments equals the analytic CPU result, with pixel-centred rectangles of exact coverage. |
| `sanity.vk.graphics.pipeline_features` | Tessellation, geometry shaders, line/point sizes, wide lines, depth clamp, secondary command buffers and pipeline cache reuse render correctly when the feature is reported. |
| `sanity.vk.descriptor.pools_templates_samplers` | Descriptor pools with and without FREE_DESCRIPTOR_SET allocate up to maxSets and fail further allocation with VK_ERROR_OUT_OF_POOL_MEMORY or FRAGMENTED_POOL, reset and free make sets available again, update templates, dynamic uniform offsets and immutable samplers produce the CPU result. |
| `sanity.vk.cmd.buffers_reset_reuse` | Command buffers from pools with and without per-buffer reset re-record and re-submit correctly (ONE_TIME_SUBMIT, SIMULTANEOUS_USE, vkResetCommandBuffer, vkResetCommandPool) and secondary buffers execute from primaries. |
| `sanity.vk.sync.fences` | The GPU waits on an event the host sets 3 s after the submit, longer than the kernel's 2 s ring lockup timeout: the gated work does not run before the set and runs after it, and a second process rendering and reading back meanwhile completes every cycle in a normal time. A fence signals only after the submitted work completes (readback after wait is final), WaitForFences with timeout 0 and short timeout return VK_TIMEOUT correctly and ResetFences works. |
| `sanity.vk.sync.binary_semaphores` | Cross-queue and cross-submit binary semaphore dependencies order work correctly, and each signal is waited exactly once. |
| `sanity.vk.sync.timeline_semaphores` | Timeline semaphore values advance monotonically, host waits/signals and device waits on future values interleave correctly, and values stay within maxTimelineSemaphoreValueDifference. |
| `sanity.vk.sync.events_barriers` | Events and pipeline barriers (execution plus memory dependencies, layout transitions) order writes before reads across all stage pairs; the same program without the barrier is not asserted. |
| `sanity.vk.sync.memory_model` | With vulkanMemoryModel features reported, release/acquire shared-memory message passing across workgroups on atomic variables never observes stale data across many iterations. |
| `sanity.vk.sync.submit2_and_wait_idle` | The GPU waits on an event the host sets 3 s after the submit, longer than the kernel's 2 s ring lockup timeout: the gated work does not run before the set and runs after it, and a second process rendering and reading back meanwhile completes every cycle in a normal time. vkQueueSubmit2 with stage-specific semaphores and vkDeviceWaitIdle/vkQueueWaitIdle return only after all work completes. |
| `sanity.vk.query.timestamps` | The GPU waits on an event the host sets 3 s after the submit, longer than the kernel's 2 s ring lockup timeout: the gated work does not run before the set and runs after it, and a second process rendering and reading back meanwhile completes every cycle in a normal time. Timestamp queries are monotonic along each queue (timestampValidBits masked) with timestampPeriod conversion, availability and VK_QUERY_RESULT_WAIT_BIT semantics hold, with VK_EXT_calibrated_timestamps the device/QPC pair relates sanely, and occlusion and (when the feature is on) pipeline statistic queries match analytic counts. |
| `sanity.vk.device.independent_devices` | Two devices on one adapter work side by side, the second keeps working after the first is destroyed, and a third device created afterwards works (device loss is not provoked: forcing it needs invalid usage or unbounded shaders). |
| `sanity.vk.validation.reference_workload` | A reference workload (buffers, images, compute, graphics, swapchain) run under the Khronos validation layer produces zero errors and zero warnings. |

#### WSI

| check | contract |
|---|---|
| `sanity.vk.wsi.surface_caps_consistent` | Surface capabilities, formats and present modes are consistent (FIFO present, extent in range and equal to the client area, two-call VK_INCOMPLETE) and every reported (format, mode, image count) creates a swapchain granting at least the requested images. [E] |
| `sanity.vk.wsi.fifo_every_image_in_order` | In FIFO mode N sequential frames with their numbers encoded in pixels are all presented, in order (verified by screen reads). [E] |
| `sanity.vk.wsi.mailbox_immediate_caps` | MAILBOX and IMMEDIATE are used only if reported; mailbox never shows an older frame after a newer one, and immediate is not slower than FIFO (rates recorded). [E] |
| `sanity.vk.wsi.out_of_date_suboptimal` | [E] After resizing the window, acquire or present returns VK_ERROR_OUT_OF_DATE_KHR or SUBOPTIMAL_KHR per spec, and a recreated swapchain at the new extent presents correctly. |
| `sanity.vk.wsi.acquire_timeouts` | AcquireNextImage with timeout 0 and finite timeouts return NOT_READY/TIMEOUT when no image is free, and with the images presented they become available. |
| `sanity.vk.wsi.minimized_and_destroyed` | [E] Presenting to a minimised window and destroying the swapchain/surface/window in various orders never hangs or loses the device. |

#### External

| check | contract |
|---|---|
| `sanity.vk.external.memory_caps_consistent` | vkGetPhysicalDeviceExternalBufferProperties/ImageFormatProperties for each handle type (OPAQUE_WIN32, KMT, D3D11_TEXTURE, D3D12_RESOURCE...) agree with actual export/import success. |
| `sanity.vk.external.semaphore_fence_caps` | External semaphore and fence handle types reported as exportable/importable can actually be exported, imported and used to order work; unreported ones fail at creation. |

### 4.16 Formats (15)

#### Per-format contracts

| check | contract |
|---|---|
| `sanity.fmt.support_bits_match_use` | For each format in D3D9, D3D11 and D3D12 tables, support for sample, render target, blend, UAV, MSAA and display reported by the API equals what actually works and what fails when a bit is absent. |
| `sanity.fmt.cross_api_equal` | For the formats supported by both D3D11 and D3D12, the support bits and the resulting pixel values from the same upload-render-readback test are identical between the two APIs. |
| `sanity.fmt.roundtrip_uncompressed` | Upload and readback of every uncompressed colour format (8/16/32 bits, UNORM/SNORM/UINT/SINT/FLOAT) returns the exact bytes for copies and values correct at the format's precision for render and sample. |
| `sanity.fmt.packed_formats` | Packed formats (R10G10B10A2, R11G11B10_FLOAT, R9G9B9E5, B5G6R5, B5G5R5A1, B4G4R4A4) decode and encode to the analytic values when sampled and rendered, with correct handling of denormals/infinity for floats. |
| `sanity.fmt.bc_blocks` | BC1-BC5 textures uploaded with valid generated blocks sample to values equal to a CPU decoder within the spec's tolerance (endpoints exact), BC6H and BC7 blocks round-trip bit-exactly, and CopyResource of blocks and partial block-aligned updates are bit-exact in D3D11, D3D12, Vulkan and OpenGL. |
| `sanity.fmt.bc_dimensions` | BC textures with 4-aligned top levels and mip chains down to 1x1 have the documented footprint sizes and sample correctly; on D3D10/11 a top level that is not a multiple of 4 in width or height fails with E_INVALIDARG as on WARP, on D3D12 only when UnalignedBlockTexturesSupported is FALSE; creation with other invalid sizes fails per spec. |
| `sanity.fmt.srgb_conversion` | Writing linear values to SRGB targets and sampling them back follow the sRGB EOTF within one 8-bit step for all 256 codes, equal across D3D9 (SRGBWRITE), D3D11 and D3D12; blending happens in linear space. |
| `sanity.fmt.typeless_casts` | A typeless resource viewed as UNORM, SNORM, UINT, SINT, FLOAT and SRGB (where allowed) reinterprets the same bits as per the spec in D3D11 and D3D12, and casts outside the format family fail in D3D11 view creation. |
| `sanity.fmt.depth_stencil` | Depth and stencil formats clear, test and sample to the analytic values (D16 quantisation, D24 and D32 precision, stencil bits) in D3D11, Vulkan and OpenGL, and reading depth via typeless SRV equals the written depth within the format's resolution. |
| `sanity.fmt.yuv_nv12_p010` | If NV12 / P010 / YUY2 / AYUV are reported supported, a known frame uploaded plane by plane samples through luma/chroma SRVs to values equal to a CPU YUV decode, in D3D11 and D3D12, for even sizes that are not multiples of 16 (YUY2 and AYUV in D3D11); odd sizes either work or fail with E_INVALIDARG; otherwise creation fails consistently. |
| `sanity.fmt.yuv_video_processing` | Where video processor formats are reported, converting NV12 to RGB with the video processor produces a result within a small tolerance of the CPU BT.601/709 conversion (stream colour space). |
| `sanity.fmt.float_special_values` | FLOAT formats preserve NaN, Inf, denormal and negative zero through copy and sample paths per IEEE rules (rendering may flush denormals per spec), and comparisons against the CPU values are bit-exact for copies. |
| `sanity.fmt.snorm_and_clamp` | SNORM formats clamp -1 for the minimum code and write/readback symmetrical values; UNORM render clamps out-of-range shader outputs to [0,1]; UINT/SINT clamp per format on write. |
| `sanity.fmt.mip_and_array_footprints` | For each format the subresource sizes, pitches and total size from API queries (GetCopyableFootprints / Map RowPitch / GetLevelDesc) are consistent with the format's bits per element and block size at every mip. |
| `sanity.fmt.display_formats` | Formats the output reports for display (GetDisplayModeList, swap-chain support bit) can be created as a swap chain and present a known colour, matching the reported scan-out support; R10G10B10_XR_BIAS_A2_UNORM, which is not a valid flip-model buffer format, is tried as a blt-model chain and reported as a measure. [E] |

### 4.17 Shaders and compilation (12)

| check | contract |
|---|---|
| `sanity.shader.dxbc.sm2_sm3` | SM2.0/2.x/3.0 vertex and pixel shaders from D3DCompile run on D3D9 and equal WARP/D3D9Ex-reference pixels. |
| `sanity.shader.dxbc.sm4_sm5` | SM4.0/4.1/5.0/5.1 vertex, geometry, hull/domain, pixel and compute shaders run on D3D10/11 and equal WARP within tolerance (graphics) or the analytic result (compute, tessellation, UAV atomics on SM5 only since cs_4_x has none, resource arrays); SM5.1 dynamic resource arrays run on D3D12. |
| `sanity.shader.dxil.sm6` | DXIL SM6.0 to 6.x (per reported max shader model) compute and graphics shaders, wave intrinsics per caps, run on D3D12 and equal a CPU reference; unsupported models are refused by CheckFeatureSupport; the run skips with a reason when dxcompiler.dll or dxil.dll cannot be loaded. |
| `sanity.shader.spirv.valid_runs` | Valid SPIR-V compute and graphics modules (with specialization constants) create and run in Vulkan and match the CPU reference. |
| `sanity.shader.spirv.invalid_fails` | Invalid SPIR-V is invalid usage with undefined behaviour and reaches the host compiler; whether creation failed and whether a message appeared are measures only, and the device still computes correctly afterwards. |
| `sanity.shader.glsl.versions` | GLSL #version 110 through 460 shaders (those within the context's reported SHADING_LANGUAGE_VERSION) compile and give the analytic output. |
| `sanity.shader.glsl.invalid_diagnostics` | Invalid GLSL fails with COMPILE_STATUS false and a non-empty info log; programs that fail to link report LINK_STATUS false and glUseProgram gives INVALID_OPERATION. |
| `sanity.shader.dxbc.invalid_diagnostics` | Invalid HLSL produces diagnostics from D3DCompile, and garbage bytecode passed to CreateVertexShader/PixelShader fails with E_INVALIDARG and does not lose the device. |
| `sanity.shader.cl.c_and_il` | CL C kernels at each supported -cl-std and IL kernels (when reported) compile and run correctly; build errors give a log. |
| `sanity.shader.determinism.created_twice` | The same shader/pipeline created twice in one process gives bit-identical output for the same input, in each API. |
| `sanity.shader.determinism.two_processes` | Two processes concurrently compiling and running the same shaders (D3D, GL, VK, CL) both get identical outputs, and each is unaffected by the other's invalid shaders. |
| `sanity.shader.cache.reuse_correct` | A pipeline cache or shader cache blob saved and reloaded (Vulkan pipeline cache, GL program binary, D3D12 cached PSO) yields the same output; an incompatible blob is rejected and the application falls back to compiling. |

### 4.18 Interop (54)

#### GDI and window composition

| check | contract |
|---|---|
| `sanity.interop.gdi.dxgi_getdc_roundtrip` | IDXGISurface1::GetDC on a GDI-compatible back buffer lets GDI draw (lines, text, FillRect, alpha-less), and after ReleaseDC and Present the frame shows the GDI content and the D3D content around it intact. [E] |
| `sanity.interop.gdi.dxgi_getdc_discard_flag` | GetDC with Discard=TRUE does not require preserving prior contents, FALSE preserves them; both return pixels matching the WARP result. |
| `sanity.interop.gdi.dxgi_getdc_lifetime` | GetDC twice without release fails, ReleaseDC without GetDC fails with documented error, and the device and surface stay usable. |
| `sanity.interop.gdi.d3d9_surface_getdc` | IDirect3DSurface9::GetDC on a lockable/GDI-compatible surface lets GDI draw, and D3D9 sees GDI pixels after ReleaseDC while untouched pixels are unchanged. |
| `sanity.interop.gdi.d3d9ex_flip_gdi` | GDI drawn via GetDC onto a D3D9Ex flip-model back buffer shows in the presented frame in order with D3D rendering. [E] |
| `sanity.interop.gdi.gl_window_owndc` | In a CS_OWNDC window with a GL format, GDI calls on the same DC and SwapBuffers produce the documented behaviour: SwapBuffers shows GL content in double-buffered windows, GDI drawing in a single-buffered window is visible, and the GL content is unchanged by unrelated GDI calls. [E] |
| `sanity.interop.gdi.gl_mixed_swap` | Alternating GDI TextOut and GL render+SwapBuffers 100 times: the final frame equals the last GL frame (double buffered) and no GDI call crashes or hangs. [E] |
| `sanity.interop.gdi.bitblt_each_api` | BitBlt from the desktop device context over a window showing D3D9, D3D11, D3D12, GL and Vulkan content returns exactly the presented frame's pixels; BitBlt from the window's own device context does too for bitblt-model windows (D3D9, D3D11 sequential, GL double buffered) and is a measure for flip-model windows (D3D12, Vulkan), whose window DC content is not specified. [E] |
| `sanity.interop.gdi.printwindow_full_content` | PrintWindow(PW_RENDERFULLCONTENT) of windows showing each API's content (D3D11 also in flip model) returns client pixels equal to the frame presented last. [E] |
| `sanity.interop.gdi.layered_per_pixel_alpha` | UpdateLayeredWindow with a per-pixel-alpha DIB over a 3D window composes to the analytic source-over result in a capture. [E] |
| `sanity.interop.gdi.layered_over_3d_update` | A layered window over a continuously updating 3D window: every capture is a consistent (frame i, overlay j) pair with exact blend, uniform across the whole image (no tearing inside either surface), and i and j never go back in time; the newest pair is reached after each update. [E] |
| `sanity.interop.gdi.dib_section_upload` | A DIB section (pattern) uploaded as a texture to D3D9, D3D11, D3D12, GL and Vulkan and rendered 1:1 (shader load/fetch; Vulkan by format-converting blit) reads back the same R, G, B, A values through the format codecs (BGRA to RGBA conversions correct). |
| `sanity.interop.gdi.dib_after_gpu_writes` | GPU results on a GDI-compatible D3D11 texture and a lockable D3D9 render target are copied by GetDC and BitBlt into a DIB section and equal the frame after GdiFlush; the reverse (BitBlt of a DIB pattern into the surface DC) is read back identically by D3D11. Patterns change every frame. |
| `sanity.interop.gdi.d2d_gdi_interop_target` | Direct2D on a DXGI surface mixed with GDI through ID2D1GdiInteropRenderTarget::GetDC/ReleaseDC yields pixels equal to the WARP render of the same sequence within 1 LSB. |
| `sanity.interop.gdi.dwrite_text_on_surface` | DirectWrite text drawn on a DXGI surface render target with GDI-compatible rendering mode matches the WARP rendering and the GDI text metrics. |
| `sanity.interop.gdi.text_over_flip_window` | GDI text on the window DC of a flip-model swapchain window never changes pixels outside the text and the next present is exact; a layered window carrying GDI text over the window composes to the analytic frame plus text. [E] |
| `sanity.interop.gdi.redirection_mixed_windows` | With several overlapping windows (GDI-only, 3D, layered), DWM composition equals the analytic z-ordered composite in a capture, including after moving one. [E] |
| `sanity.interop.gdi.cursor_in_captures` | Desktop captures without the cursor leave the window content exact under the cursor; captures with the cursor (GetCursorInfo plus DrawIconEx at the position minus the hotspot) equal the content with the cursor drawn at GetCursorPos with its hotspot, for a GDI window and a 3D window. [E] |
| `sanity.interop.gdi.gdi_after_device_lost` | After a D3D device is destroyed or reset, GDI on the same window still works and the window is repainted fully on the next WM_PAINT. [E] |
| `sanity.interop.gdi.multi_thread_gdi_3d` | One thread drawing GDI on a window while another presents a 3D swapchain to a different window both remain correct and responsive for seconds. [E] |

#### OpenGL and OpenCL

| check | contract |
|---|---|
| `sanity.interop.glcl.context_device_match` | clGetGLContextInfoKHR with CL_CURRENT_DEVICE_FOR_GL_CONTEXT_KHR returns a device within clGetPlatformIDs's devices, and CL_DEVICES_FOR_GL_CONTEXT_KHR contains it; a context created from the GL context works. |
| `sanity.interop.glcl.buffer_sharing` | A GL buffer written by GL is read by a CL kernel after clEnqueueAcquireGLObjects (with glFinish), CL writes become visible to GL after ReleaseGLObjects (and clFinish), and contents equal the CPU reference. |
| `sanity.interop.glcl.texture_targets_mips_layers` | clCreateFromGLTexture for 2D, 3D, array, cube faces, rectangle and mip levels > 0 returns images whose pixels match the GL texture, and CL writes visible in GL. |
| `sanity.interop.glcl.renderbuffer_sharing` | A GL renderbuffer rendered by GL is read by CL; CL writes to it are visible in glReadPixels after release. |
| `sanity.interop.glcl.gl_event_sync` | With cl_khr_gl_event, a CL event from a GL fence (clCreateEventFromGLsyncKHR) orders CL work after GL rendering without glFinish; the reverse via cl_khr_gl_event/ GL sync object works. |
| `sanity.interop.glcl.acquire_release_errors` | The documented error codes of cl_khr_gl_sharing hold: clCreateFromGLBuffer, clCreateFromGLTexture and clCreateFromGLRenderbuffer reject unknown GL names (CL_INVALID_GL_OBJECT), invalid flags, targets and contexts; clEnqueueAcquire/ReleaseGLObjects accept zero objects with a null list (CL_SUCCESS), reject a zero count with a list or a count with a null list (CL_INVALID_VALUE) and objects not created from GL (CL_INVALID_GL_OBJECT); clGetGLObjectInfo agrees with the shared object. Using an object outside acquire and release is invalid usage and is never exercised. |
| `sanity.interop.glcl.caps_unsupported` | If cl_khr_gl_sharing is not advertised, clCreateFromGLBuffer fails with the documented code and the extension string and entry points agree. |

#### D3D and OpenCL

| check | contract |
|---|---|
| `sanity.interop.d3dcl.d3d11_sharing` | With cl_khr_d3d11_sharing, buffers and 2D/3D textures from D3D11 are read and written by CL across AcquireD3D11Objects/Release with matching data; the device from clGetDeviceIDsFromD3D11KHR matches the D3D device's adapter. |
| `sanity.interop.d3dcl.d3d10_sharing` | Same contract for cl_khr_d3d10_sharing. |
| `sanity.interop.d3dcl.dx9_media_sharing` | With cl_khr_dx9_media_sharing, D3D9Ex surfaces (including planes of NV12 where reported) are readable and writable by CL across acquire/release. |
| `sanity.interop.d3dcl.acquire_order_caps` | For each of cl_khr_d3d10_sharing, cl_khr_d3d11_sharing and cl_khr_dx9_media_sharing that is advertised, the extension's own error codes hold: a second acquire gives the ALREADY_ACQUIRED code, a second release the NOT_ACQUIRED code, and acquiring an object not created from D3D10 or D3D11 CL_INVALID_MEM_OBJECT; for one that is not advertised, contexts with its device property and its creation functions fail. Using an object outside acquire and release is invalid usage and is never exercised. |

#### OpenGL and D3D

| check | contract |
|---|---|
| `sanity.interop.gld3d.nv_dx_interop_d3d11` | WGL_NV_DX_interop2: wglDXOpenDeviceNV, RegisterObject of a D3D11 texture as GL texture/renderbuffer, Lock, GL render, Unlock, then D3D sampling gives the GL result; D3D writes visible in GL after Lock. |
| `sanity.interop.gld3d.nv_dx_interop_d3d9ex` | The same with D3D9Ex surfaces and textures. |
| `sanity.interop.gld3d.lock_ordering` | Lock/Unlock pairs across 60 frames preserve ordering (GL reads exactly the frame D3D wrote before the lock, D3D reads exactly the frame GL rendered before the unlock, never a stale frame); locking or unlocking a handle that was never registered fails and an invalid object type is refused. Use outside Lock and unregistering locked objects are invalid usage and are never exercised. |
| `sanity.interop.gld3d.formats_and_targets` | Each format/target combination reported usable by interop (RGBA8, sRGB, float, depth, MSAA) renders identically to its pure-GL twin. |
| `sanity.interop.gld3d.present_via_d3d` | GL content rendered into a D3D11 swapchain back buffer via interop and presented by D3D shows the exact frame in capture. [E] |
| `sanity.interop.gld3d.caps_unsupported` | If the extension isn't advertised, wglDXOpenDeviceNV is absent or fails and all queries are consistent. |

#### D3D and D3D

| check | contract |
|---|---|
| `sanity.interop.d3d.d3d9ex_shared_to_d3d11` | A D3D9Ex shared render target (shared handle) rendered in D3D9Ex is opened in D3D11 (OpenSharedResource) with the same pixels after Flush/event query. |
| `sanity.interop.d3d.d3d11_nt_handle_d3d12` | D3D11 texture created with SHARED_NTHANDLE is opened by D3D12 (OpenSharedHandle) and sampled correctly; D3D12 writes visible in D3D11 with a shared fence ordering. |
| `sanity.interop.d3d.shared_fence_ordering` | A fence shared between two D3D11 devices (ID3D11Fence) and between D3D11 and D3D12 (ID3D12Fence opened in D3D11) orders a 1000-step ping-pong over a shared texture: each side reads exactly the other's data of that step, never an older value, and the opened fence's completed value has reached each awaited value. |
| `sanity.interop.d3d.keyed_mutex` | IDXGIKeyedMutex acquire/release between two devices (and processes) orders producer and consumer exactly; wrong key times out with WAIT_TIMEOUT. |
| `sanity.interop.d3d.d3d11on12` | D3D11On12 wrapped D3D12 resources render in D3D11 and appear in the D3D12 swapchain present with correct pixels, with ReleaseWrappedResources/ flush ordering. [E] |
| `sanity.interop.d3d.cross_adapter_render_only` | Data written on the render adapter into a D3D12 cross-adapter shared buffer (shared cross-adapter fence) is copied by the display adapter into its swap chain back buffer and presents exactly; skipped when fewer than two adapters or no cross-adapter heap exists. [E] |
| `sanity.interop.d3d.cross_process_sharing` | A resource created in one process opens in another via named/duplicated NT handle, and data and fence ordering across processes are correct; the producer exit leaves the consumer valid. |
| `sanity.interop.d3d.cross_bitness_sharing` | An x86 process and an x64 process ping-pong a shared texture and a shared fence both ways with intact pixels (duplicated handles and names), and a texture created by the x64 process is valid in the x86 process after the x64 process exited. (x86) |
| `sanity.interop.d3d.handle_lifetime` | Closing the creator's handle/device while the other holds the resource keeps the data valid until the last reference; closing all returns memory (re-creating the same count succeeds repeatedly). |
| `sanity.interop.d3d.invalid_open_errors` | Opening an invalid, closed or wrong-type handle fails with the documented HRESULT and the device remains usable. |

#### Vulkan with the others

| check | contract |
|---|---|
| `sanity.interop.vk.import_d3d11_texture` | Vulkan imports a D3D11 shared NT texture (D3D11_TEXTURE handle type, preserved initial layout), reads it by image copy, and the pixels equal what D3D11 wrote every frame, ordered by a shared fence imported as a timeline semaphore when the semaphore import is reported, otherwise by an event query. |
| `sanity.interop.vk.import_d3d12_resource` | Vulkan imports a D3D12 shared buffer (D3D12_RESOURCE) and a D3D12 fence as a timeline semaphore; the two ping-pong 8 rounds with exact data in both directions. |
| `sanity.interop.vk.export_to_d3d12` | Vulkan-allocated exportable buffer memory (D3D12_RESOURCE, else D3D12_HEAP) opens in D3D12 with the Vulkan-written bytes, and D3D12 writes are read back by Vulkan. |
| `sanity.interop.vk.export_to_gl` | Vulkan exported OPAQUE_WIN32 buffer memory and binary semaphores imported into GL (EXT_memory_object_win32, EXT_semaphore_win32) show the Vulkan bytes in GL after the semaphore wait, and GL writes (copied into the imported storage, then signalled) are read back by Vulkan. |
| `sanity.interop.vk.opencl_external_memory_caps` | If cl_khr_external_memory is advertised for a handle type, Vulkan memory imports into CL and kernel results are visible to Vulkan; otherwise not claimed. |
| `sanity.interop.vk.kmt_handles_caps` | KMT handle types work when reported (OPAQUE_WIN32_KMT buffer memory and a legacy shared D3D11 texture as D3D11_TEXTURE_KMT, both read in a second process) and when not reported the queries agree (zero features, VK_ERROR_FORMAT_NOT_SUPPORTED); creating objects with an unreported handle type is invalid usage and is not exercised. |
| `sanity.interop.vk.cross_process_present_chain` | A Vulkan process renders frames into a shared D3D11 texture, a D3D11 process composes them into its swap chain, and the capture shows each frame exactly and in order, with events ordering the two processes after the GPU work of each side completed. [E] |

### 4.19 Capture (12)

| check | contract |
|---|---|
| `sanity.capture.wgc.each_api_equals_presented` | Windows.Graphics.Capture of a window showing D3D9, D3D11, D3D12, GL and Vulkan content returns frames whose pixels equal the presented frame number's content. |
| `sanity.capture.wgc.frames_in_order` | Consecutive captured frames of a presenting window have nondecreasing frame numbers, with no frame number shown after a higher one. |
| `sanity.capture.dxgi.desktop_duplication_content` | IDXGIOutputDuplication frames of a window region equal the presented content, and AcquireNextFrame times out with DXGI_ERROR_WAIT_TIMEOUT when nothing changes. |
| `sanity.capture.dxgi.dirty_rects_cover_changes` | For each frame, the union of dirty and move rects covers every pixel that differs from the previous frame; no uncovered pixel changes. |
| `sanity.capture.dxgi.pointer_info` | Pointer position and shape reported by the duplication match GetCursorPos and the cursor shape set by the application. |
| `sanity.capture.dwm.thumbnail_each_api` | DWM thumbnails of a window for each API show its content (read back via capture of the destination) equal to the source window downscaled analytically. |
| `sanity.capture.dwm.thumbnail_updates` | A thumbnail of a continuously updating window shows frames in order and stops updating when the source is minimised or destroyed as documented. |
| `sanity.capture.wgc.resize_and_close` | A WGC session survives source window resize (FrameArrived with new ContentSize, pool recreate) and ends cleanly with Closed on window destruction. |
| `sanity.capture.wgc.cadence_matches_present` | A window presents at interval 1 for 3 s on each of D3D11 flip, D3D12 flip, GL, Vulkan FIFO and D3D9Ex FLIPEX; the WGC FrameArrived SystemRelativeTime intervals have a p50 within 10 percent of the refresh period (DwmGetCompositionTimingInfo) and a p99 under 2.5 periods, and at most 1 percent of the presented frames (after 8 warm-up frames) are never captured; duplicates (one frame number captured twice) are a measure. Raw samples are written as CSV. [E] (about 20 s) |
| `sanity.capture.dxgi.duplication_cadence` | The same through Desktop Duplication on each of the five APIs: LastPresentTime deltas, divided by AccumulatedFrames, have a p50 within 10 percent of the refresh period and a p99 under 2.5 periods; the AccumulatedFrames after the first decoded frame sum to the number of presents between the first and last decoded frame within max(2, 2 percent); the LastPresentTime span equals the present QPC span within 3 percent plus 2 periods; no frame has a LastPresentTime more than one period before its Present call. [E] (about 20 s) |
| `sanity.capture.latency_present_to_capture` | Per API, over 3 s at interval 1, the time from the QPC taken after Present to the capture reporting that frame (WGC SystemRelativeTime and duplication LastPresentTime, both against QPC); p50 and p99 are measures and the p99 is under 4 refresh periods plus 20 ms. [E] (about 20 s) |
| `sanity.capture.steady_no_drift` | 10 s of WGC capture of a D3D11 flip window at interval 1: the captured frame number trails the number of Presents started by 0 to 6 frames at every sample, and the linear-fit slope of that lag over time is within 0.1 frames per second. [E] (about 12 s) |

### 4.20 Presentation and frame pacing (38)

Each frame encodes its number in pixels so captures can read ordering, drops and duplicates.

#### Present paths and ordering

| check | contract |
|---|---|
| `sanity.present.path.d3d9_blt` | D3D9 windowed Present shows every frame in order with exact content when presenting once per vsync-throttled step: stepwise (each frame visible after its Present and not before), then a 40-frame burst at interval 1 in which no frame is missing where the capture sampled faster than 0.75 refresh periods. [E] |
| `sanity.present.path.d3d9ex_flip` | D3D9Ex FLIPEX present with present statistics shows frames in order with exact content (stepwise and 40-frame burst) and nondecreasing PresentCount, PresentRefreshCount and SyncRefreshCount; GetLastPresentCount is at least the presents issued and not below the displayed count. [E] |
| `sanity.present.path.dxgi_blt_sequential` | DXGI bitblt-model SEQUENTIAL swapchain (one buffer) shows every presented frame once and in order (interval 1; stepwise and 40-frame burst). [E] |
| `sanity.present.path.dxgi_blt_discard` | DXGI bitblt DISCARD never shows a lower frame number after a higher one in a 48-frame burst at interval 0 and a 16-frame run at interval 1, never shows torn or unrecognised images, and the last frame is shown. [E] |
| `sanity.present.path.dxgi_flip_sequential` | Flip-model SEQUENTIAL with interval 1 shows every frame, in order, exactly once (stepwise and 40-frame burst; frame statistics nondecreasing). [E] |
| `sanity.present.path.dxgi_flip_discard` | Flip-model DISCARD never shows an older frame after a newer one in a 48-frame burst at interval 0 and a 16-frame run at interval 1, and the last presented frame is eventually shown. [E] |
| `sanity.present.path.d3d12_flip` | D3D12 flip swapchain with three buffers (fence-protected reuse, no GPU wait between presents) shows every frame in order at interval 1. [E] |
| `sanity.present.path.gl_swapbuffers` | GL SwapBuffers with interval 1 (WGL_EXT_swap_control when advertised) shows every frame once in order. [E] |
| `sanity.present.path.vk_fifo` | Vulkan FIFO presents every image exactly once in order. [E] |
| `sanity.present.path.vk_immediate_mailbox` | Vulkan IMMEDIATE and MAILBOX (each only when the surface reports it; skip when neither) never show an older frame after a newer one in a 48-frame burst and the last frame is shown. [E] |
| `sanity.present.content.exact_pixels` | In every path above (11 path ids, three window sizes including odd ones), the displayed frame's pixels equal what the application rendered over the full surface (quadrant frames), and equal an uploaded gradient plus checker pattern on every path that has a pixel upload. [E] |
| `sanity.present.content.sizes_and_formats` | Presenting RGBA8, BGRA8, sRGB (blt chain and sRGB view over a BGRA flip chain), R10G10B10A2 and FP16 swapchains at odd 131x77 (exact within 0, 1, 1, 1, 3 per channel respectively), 1x1 and 3840x2160 (stretched, quadrant centres) shows correct pixels; CheckFormatSupport DISPLAY and RENDER_TARGET agree with swap chain creation. [E] |
| `sanity.present.content.scaling_modes` | DXGI_SCALING stretch/none/aspect-ratio when the window size differs from the buffer (64x64 buffer in a 128x96 client) shows the documented geometry: stretch covers the client, none shows the buffer 1:1 at the top-left, aspect-ratio keeps the square centred; none on a bitblt chain and aspect-ratio may be rejected with a documented invalid-argument code. [E] |
| `sanity.present.content.alpha_modes` | Window swapchains with IGNORE alpha show the colour channels unchanged (premultiplied and straight are measured: creation result and colour); composition swapchains (DirectComposition, over a known opaque background visual) composite premultiplied and ignore as source-over within 2 per channel, straight when the runtime creates it. The layered-window cases are covered by sanity.interop.gdi.layered_per_pixel_alpha. [E] |

#### Timing

| check | contract |
|---|---|
| `sanity.present.timing.vsync_interval_equals_refresh` | Interval-1 present timestamps (QPC around Present or present statistics) average the refresh period within 5 percent over 5 seconds, jitter bounded. [E] |
| `sanity.present.timing.interval_two` | Interval 2 produces a frame period about twice the refresh period (within 10 percent) and every frame still shown once. [E] |
| `sanity.present.timing.immediate_faster_than_refresh` | Interval 0 / IMMEDIATE / allow-tearing runs faster than refresh rate for a trivial scene while ordering holds. [E] |
| `sanity.present.timing.frame_latency_limits` | SetMaximumFrameLatency(n) bounds queued frames: the waitable object never allows more than n frames ahead of the display, and n=1 gives lower latency than larger n. [E] |
| `sanity.present.timing.present_stats_vs_capture` | GetFrameStatistics / DwmGetCompositionTimingInfo present counts and refresh counts agree with the number of frames and intervals observed in a capture. [E] |
| `sanity.present.timing.high_refresh_rates` | At each refresh rate offered by the display modes (up to the maximum), vsync-paced presenting achieves that rate within bound and no frames are lost. [E] |
| `sanity.present.timing.stutter_per_path` | For each of the 11 present paths, 3 s at interval 1 (interval 0 for Vulkan IMMEDIATE, which has no interval 1), the displayed intervals (DXGI/D3D9Ex frame statistics: PresentRefreshCount deltas times the DWM refresh period when at least 60 percent of the frames yield a new PresentCount, otherwise WGC capture timestamps): at most 0.5 percent of the intervals span 2 refresh periods or more (an interval of 1.5 periods or more counts), the longest is under 3 periods and the p99 frame time under 1.5 periods. The full histogram (0.25-period bins) and the per-frame timing are written as CSV. [E] (about 40 s) |
| `sanity.present.timing.pacing_under_gpu_load` | The stutter_per_path bounds hold for D3D11 flip and Vulkan FIFO while two other processes run bounded heavy GPU work (1080p clears and copies; multi-API 4 and 16 MiB round trips) for the whole measurement. [E] (about 20 s) |
| `sanity.present.timing.pacing_with_parallel_presenters` | Up to four processes on different APIs (D3D11 flip, D3D12 flip, Vulkan FIFO, GL; D3D9Ex when fewer are enabled) each present at interval 1 for 3 s and each meets the stutter_per_path bounds; DwmGetCompositionTimingInfo cFramesMissed rises by at most 0.5 percent of the cRefresh rise over the run. [E] (about 15 s) |
| `sanity.present.latency.present_to_display` | Per paced path, over 3 s at interval 1, the time from the Present call to the frame being displayed (frame statistics: SyncQPCTime of the frame's PresentCount, moved by PresentRefreshCount minus SyncRefreshCount periods; otherwise the SystemRelativeTime of the first WGC capture showing the frame); p50 and p99 are measures and the p99 is at most (frame latency 3 + 1) periods plus 1 period. [E] (about 35 s) |

#### Window state and concurrency

| check | contract |
|---|---|
| `sanity.present.window.resize_during_presentation` | Resizing the window repeatedly while presenting (ResizeBuffers / swapchain recreate) never loses a frame number order, never shows garbage, and device stays usable. |
| `sanity.present.window.occlusion` | When fully covered by another window, Present returns DXGI_STATUS_OCCLUDED (flip) or succeeds as documented, the device stays alive, and the content is correct when uncovered. |
| `sanity.present.window.minimize_restore` | Minimising and restoring while presenting returns documented statuses, does not hang, and after restore the latest frame is displayed. |
| `sanity.present.window.fullscreen_exclusive` | SetFullscreenState(TRUE) and back work, presenting in fullscreen shows exact content at the display mode, and the desktop is restored with window position. [E] |
| `sanity.present.window.borderless_fullscreen_flip` | A borderless window covering the monitor with flip presentation shows exact frames and may use independent flip; leaving restores normal composition. [E] |
| `sanity.present.window.many_windows` | 16 windows each presenting its own frame sequence at once all show their own frames in order without cross-talk. |
| `sanity.present.window.many_processes` | Several processes with different APIs presenting simultaneously each show their own ordered sequences. |
| `sanity.present.window.multi_monitor` | With more than one output, a window moved across outputs keeps presenting correct frames in order. [E] |
| `sanity.present.window.destroy_while_presenting` | Destroying the window or device during Present from another thread does not hang and releases cleanly; a new window then presents correctly. |
| `sanity.present.mirror.host_receives_sequence` | The mirror on the host receives the same frame number sequence as presented in the guest (in order, none from the future), the latest frame is eventually delivered, and the host-phase capture equals the guest content. |
| `sanity.present.mirror.host_cadence` | The guest presents a numbered sequence (D3D11 flip, interval 1, 3 s) and records for each frame number its QPC and wall-clock UTC in ms (measure `frames` and mirror_frames.json); the guest row asserts only that the sequence was presented and its last frame shown. The matrix host phase (scripts/gpu-test/host_phase.py, same id) matches the frame list to the remotewin host's per-frame arrival events (RWIN_TIMING_DIR feed-*.csv, event received) and asserts a p50 within 10 percent of the period, a p99 under 2.5 periods and at most 1 percent of the frames lost. [E] (about 6 s) |
| `sanity.present.display.output_stays_active` | While frames are composed continuously, the display output reports active (EnumDisplayDevices ATTACHED, DWM composition on, no monitor off events) for the whole run. [E] |
| `sanity.present.display.idle_then_present` | After seconds of no changes, a single new present still reaches the capture/mirror within a bound. |
| `sanity.present.latency.input_to_photon_monotonic` | Each frame presented after a SendInput shows in a capture no earlier than the input and the captured sequence has no gaps larger than a bound while typing/moving. |

### 4.21 Errors and robustness (27)

#### Invalid arguments

| check | contract |
|---|---|
| `sanity.err.arg.d3d11_invalid_params` | D3D11 calls with null pointers, out-of-range values and invalid descriptors return E_INVALIDARG (or the documented code) and leave the device usable. |
| `sanity.err.arg.d3d12_invalid_params` | D3D12 creation calls with invalid descriptors return E_INVALIDARG or DXGI_ERROR_UNSUPPORTED and do not crash; the debug layer is not needed. |
| `sanity.err.arg.d3d9_invalid_params` | D3D9 calls with invalid parameters return D3DERR_INVALIDCALL and the device stays usable. |
| `sanity.err.arg.vulkan_invalid_handles_null` | Vulkan calls that are valid with null handles succeed as documented (vkDestroy* of null, vkFreeMemory null) and valid-usage-compliant error returns are the documented codes. |
| `sanity.err.arg.opencl_error_codes` | OpenCL calls with invalid values return the code the specification lists (CL_INVALID_VALUE, CL_INVALID_BUFFER_SIZE, CL_INVALID_KERNEL_ARGS, CL_INVALID_WORK_GROUP_SIZE) and the context stays usable. |
| `sanity.err.arg.gl_error_flags` | OpenGL calls with invalid enums and values set GL_INVALID_ENUM, GL_INVALID_VALUE or GL_INVALID_OPERATION as specified, change no state, and glGetError clears the flag. |
| `sanity.err.arg.dxgi_invalid_swapchain` | Swap chain creation with invalid sizes, formats and buffer counts returns DXGI_ERROR_INVALID_CALL or E_INVALIDARG. |

#### Unsupported features

| check | contract |
|---|---|
| `sanity.err.unsupported.caps_match_use` | For each optional feature probed by capability query, a reported-unsupported feature fails when used, with the documented error, and a reported-supported feature works. |
| `sanity.err.unsupported.formats` | CheckFormatSupport for every DXGI format agrees with whether creating a resource and view of that format succeeds. |
| `sanity.err.unsupported.extensions_list_vs_entry` | Every extension a Vulkan, GL or OpenCL implementation advertises has all of its entry points resolvable, and no non-advertised entry points claim to work. |
| `sanity.err.unsupported.opencl_version_coherent` | CL_DEVICE_VERSION, CL_PLATFORM_VERSION and the advertised extensions are consistent with each other. |

#### Fault handling

| check | contract |
|---|---|
| `sanity.err.fault.device_removed_reason` | After removal injection, GetDeviceRemovedReason returns a failure code and every later call fails with the documented device-removed result, not a crash. [E] |
| `sanity.err.fault.device_lost_vulkan` | After a lost device, Vulkan calls return VK_ERROR_DEVICE_LOST, and new device creation works; skipped when the never-ending shader is not terminated within 25 s. [E] |
| `sanity.err.fault.tdr_recovers` | A shader that runs well past the timeout limit is aborted with a documented loss and a new device then runs normally; elapsed time of the test is under 30 seconds. [E] |
| `sanity.err.fault.process_crash_cleanup` | A process terminated by TerminateProcess in the middle of submission leaves no resources in use: the system returns to baseline and new processes allocate the full budget (at least 90% of what a fresh process allocated before the kills, for device and host-visible memory). [E] |
| `sanity.err.fault.oom_clean` | Creating resources until failure in every API ends with the documented out-of-memory error, and freeing allows work to resume. [E] |
| `sanity.err.fault.kill_during_map` | Killing a process while it holds large mappings does not leave memory that later allocators cannot get (at least 90% of the pre-kill amount). [E] |

#### Host survival

Each row below drives a path an application can reach and asserts that the host survives. The misbehaving guest device may end up lost, but another process rendering and reading back during the misbehaviour completes every cycle, a guest process started afterwards creates a device, renders and reads back correctly, and the check records what the guest itself saw, which must be the API's documented error or device-lost result. Misbehaviour runs in child processes that are never cleaned up, so the host sees the process end with the work in flight.

| check | contract |
|---|---|
| `sanity.err.vk_wrong_queue_family` | [E]: transfer, graphics and compute commands recorded into command buffers of every queue family that lacks the matching capability (for example a transfer-only or video family), then submitted, give only documented results or device loss; skips only when every family supports all three. |
| `sanity.err.gpu_wait_never_signalled` | [E]: a Vulkan vkCmdWaitEvents on an event nobody sets, a D3D12 queue Wait on a fence value nobody signals and a GL glWaitSync on a sync object created in a shared context that never flushes (so its fence is never reached), each followed by the process ending without setting or signalling anything; the guest sees the work pending or a documented loss. |
| `sanity.err.long_running_shader` | [E]: a D3D11, D3D12 and Vulkan compute shader running a bounded loop sized by calibration to about 10 s completes with the analytic result or the device is lost; the time to completion or loss is recorded. |
| `sanity.err.infinite_shader` | [E]: a D3D11, D3D12, Vulkan and OpenCL compute dispatch whose loop waits for a value in a buffer the CPU never changes (read with atomics so it cannot be removed) is terminated after 3 s of running; the time the guest device takes to report removal or loss is recorded when it does. |
| `sanity.err.kill_mid_submit` | [E]: child processes of every API submit heavy copy work in a loop and are ended by TerminateProcess at random points, 20 times; the seed is recorded. |
| `sanity.err.oom_every_api` | [E]: allocating device buffers until failure in each API ends with the API's out-of-memory error at every request size down to 1 MiB, freeing everything succeeds, and a fresh allocation, upload and readback of normal size works. |

#### Limits

| check | contract |
|---|---|
| `sanity.err.limit.handle_count` | Creating objects up to the API limits (100000 small buffers in D3D11, Vulkan and OpenCL, 100000 fences, a 1000000-descriptor heap and 4096 committed buffers in D3D12) succeeds or fails with the documented out-of-memory error, and releasing returns process memory and handles to baseline. |
| `sanity.err.limit.max_texture_dims` | Textures of the maximum reported dimensions can be created and used (D3D11, D3D12, OpenCL), and the next size up fails with the documented code. |
| `sanity.err.limit.max_threads_workgroup` | Dispatches at the reported maximum work-group size run correctly; one more is rejected (D3D11 and D3D12 at 1024 threads per group, OpenCL at CL_DEVICE_MAX_WORK_GROUP_SIZE). |
| `sanity.err.limit.many_contexts_per_process` | Creating contexts until the first failure gives the documented error, and at least 64 contexts (devices) work in each of D3D11, D3D12, Vulkan and OpenCL. |

### 4.22 Lifecycle (10)

| check | contract |
|---|---|
| `sanity.life.dwm.restart_apps_survive` | After DWM is restarted, a running D3D11 or Vulkan windowed application keeps rendering without losing its device, or reports a documented loss. [E] |
| `sanity.life.dwm.restart_desktop_recovers` | After a DWM restart the desktop is composed again within a stated time, and the display source is visible. [E] |
| `sanity.life.mode.change_apps_survive` | A mode change while a flip-model swap chain is live causes a resize event or documented status and rendering continues at the new size. [E] |
| `sanity.life.mode.refresh_rate_change` | Changing the refresh rate keeps the application alive and vblank cadence follows the new rate. [E] |
| `sanity.life.sleep.resume_contents` | After sleep and resume, resources created before the sleep are intact or the device is reported lost with a documented reason, and new devices work. [E] |
| `sanity.life.agent.restart_apps_unaffected` | Restarting the guest management agent does not affect running GPU applications. [E] |
| `sanity.life.agent.restart_new_apps_work` | Applications started after the agent restart see the same adapters and run correctly. [E] |
| `sanity.life.update.live_driver_update` | A driver update performed while apps are running leaves them either running correctly or removed with a documented loss; new apps run on the new version; the update is driven as a pnputil device restart of the adapter. [E] |
| `sanity.life.session.logoff_logon` | After the user session is disconnected and reconnected (tsdiscon and a SYSTEM tscon task), the adapters and displays enumerate identically and apps run. [E] |
| `sanity.life.display.output_off_on` | Turning the output off and on through the display configuration API restores a composed desktop and does not repeat itself by itself. [E] |

### 4.23 Bitness (12)

| check | contract |
|---|---|
| `sanity.bits.parity.operations_same_result` | A set of deterministic compute and render operations produces byte-identical output in 32-bit and 64-bit processes, for each API. |
| `sanity.bits.parity.integer_widths` | Struct sizes and ABI-visible values (pointer-sized fields, size_t in OpenCL, handle widths) are those the APIs define for each bitness. |
| `sanity.bits.parity.data_roundtrip` | The data roundtrip boundary sizes up to what 32-bit address space allows roundtrip intact. |
| `sanity.bits.share.cross_bitness_nt_handle` | A resource shared by NT handle from a 64-bit process opens in a 32-bit process and the reverse, with identical contents; each executable is the owner in its own run and the other bitness is the opener. |
| `sanity.bits.share.cross_bitness_fence` | A shared fence signalled in one bitness releases a waiter in the other. |
| `sanity.bits.share.cross_bitness_dxgi_keyed_mutex` | Keyed mutex hand-offs between a 32-bit and a 64-bit process serialise access and keep contents. |
| `sanity.bits.addr.allocation_fails_cleanly` | In a 32-bit process, allocating and mapping more than the address space allows fails with the documented error and never crashes or wraps. (x86) [E] |
| `sanity.bits.addr.large_address_aware_on` | A LARGEADDRESSAWARE 32-bit process can map up to the larger space and all data roundtrips intact; the flag is set in a patched copy of lsw-suites32.exe. (x86) [E] |
| `sanity.bits.addr.large_address_aware_off` | Without the flag, mappings that do not fit in 2 GiB fail cleanly, and no pointer is above the limit; the flag is cleared in a patched copy of lsw-suites32.exe. (x86) [E] |
| `sanity.bits.wow64.icd_loading` | All four ICD kinds (D3D UMD, OpenGL, OpenCL, Vulkan) load in a 32-bit process on the 64-bit OS and report the same adapter. (x86) |
| `sanity.bits.wow64.exit_cleanup` | A 32-bit process exiting with live mappings leaves the system at baseline (device and host-visible memory a new process can allocate stays within 90%). (x86) [E] |
| `sanity.bits.parity.error_codes` | The error-path checks of invalid arguments return the same codes in both bitnesses. |

### 4.24 Host invariants (12)

Judged by the matrix around runs. All rows are host-phase.

| check | contract |
|---|---|
| `sanity.host.alive.stack_stays_up` | The host stack processes are the same processes at the end of the run as at the start, with no restart or crash. (host-phase) |
| `sanity.host.alive.responsive_after_run` | After the run the host answers a trivial request within a stated time. (host-phase) |
| `sanity.host.baseline.memory_returns` | Host process memory and GPU memory return to within tolerance of the pre-run values after the guest processes have exited. (host-phase) |
| `sanity.host.baseline.objects_return` | The count of host-side GPU objects, handles and contexts returns to the pre-run baseline once the guest is idle. (host-phase) |
| `sanity.host.baseline.threads_fds_return` | Host thread counts and open file descriptors return to baseline within tolerance. (host-phase) |
| `sanity.host.errors.none_recorded` | The host error logs, driver validation messages and GPU error counters record no error during the run. (host-phase) |
| `sanity.host.stalls.none_over_threshold` | The host's stall detector reports no stall longer than the stall threshold during the run. (host-phase) |
| `sanity.host.accounting.bytes_agree` | The host's accounting of bytes transferred is at least the bytes the guest verified through roundtrips and not more than a stated multiple of them. (host-phase) |
| `sanity.host.accounting.submissions_agree` | The host's count of submissions is within tolerance of those that the guest-provoked workloads made. (host-phase) |
| `sanity.host.accounting.processes_agree` | The host's list of guest processes using the GPU during the run matches the processes the guest started. (host-phase) |
| `sanity.host.accounting.memory_peak_covers` | The host-reported peak GPU memory is not below the memory the guest verifiably held at its peak. (host-phase) |
| `sanity.host.shutdown.clean_exit` | After the guest finishes, stopping the stack ends all host processes with exit status zero and leaves no stale files or shared memory. (host-phase) |

### 4.25 LSW stack (12)

Implementation note: a guest process cannot drive the host mirror, so these rows assert the guest side of each contract with the same mechanisms the stack uses: windows of a launched process found by title, Win32 maximize, snap and restore, and SendInput keyboard, mouse and focus. Whether the host mirrors the result is judged by the host phase. `sanity.lsw.status.numbers_match_activity` skips because the status numbers exist only on the host. `sanity.lsw.mirror.frames_equal_guest` reuses the guest frame-order check of the capture area.

| check | contract |
|---|---|
| `sanity.lsw.mirror.launched_app_appears` | An application launched through the stack shows a mirrored window within a stated time, with the right title and size. |
| `sanity.lsw.mirror.relaunching_app` | An app that relaunches itself de-elevated through Explorer is mirrored from its first window, and the mirrored window stays attached to the final process. |
| `sanity.lsw.mirror.frames_equal_guest` | Frames in the mirror equal the guest's frames bit for bit (after known scaling) and arrive in order with none repeated or skipped for a drawn counter sequence. |
| `sanity.lsw.window.maximize_work_area` | Maximizing a mirrored window leaves the guest window at the work-area rectangle of its monitor. |
| `sanity.lsw.window.snap_restore` | Snapping to a half and restoring puts the guest window at the expected half rectangle and then at the original rectangle. |
| `sanity.lsw.window.move_resize_guest_follows` | Moving and resizing a mirrored window moves and resizes the guest window to the same rectangle within one pixel. |
| `sanity.lsw.input.keyboard_to_right_window` | Keyboard input to one of two mirrored windows reaches only that guest window. |
| `sanity.lsw.input.mouse_position_mapping` | A mouse click at a position in the mirrored window arrives at the matching guest coordinates within one pixel, including at non-100% scaling. |
| `sanity.lsw.input.focus_follows` | Focus changes between mirrored windows are reflected by the guest foreground window. |
| `sanity.lsw.status.numbers_match_activity` | The stack's GPU status shows memory use, process counts and activity that move in the direction and approximate amount of provoked workloads. |
| `sanity.lsw.update.preserves_requirements` | After a guest update, all requirements read as required again and apps still launch. [E] |
| `sanity.lsw.close.window_close_ends_app` | Closing the mirrored window closes the guest window, and the app exits when the app's rules allow. |

### 4.26 Regression contracts (10)

Implementation note: `sanity.reg.stats.transfer_counts_plausible` runs the known sequence and records its totals, then skips because the counters are host-side. `sanity.reg.lsw.maximized_not_at_drop_point` drags a maximized guest window by mouse injection. `sanity.reg.lsw.first_launch_mirrored` skips (host-observed). `sanity.reg.gl.core_context_works` creates the core profile contexts of every advertised version.

| check | contract |
|---|---|
| `sanity.reg.cl.buffer_alive_while_in_use` | An OpenCL buffer released by the application while a kernel using it is enqueued remains valid until the kernel finishes: the result is correct (reference counting contract). |
| `sanity.reg.d3d12.caps_enum_defined` | CheckFeatureSupport for every D3D12_FEATURE value either returns a documented structure with in-range values or a failure code; no field holds garbage that violates its enum range. |
| `sanity.reg.stats.transfer_counts_plausible` | The byte and operation counts shown by the stack's status for a known sequence of uploads and readbacks match the sequence. |
| `sanity.reg.upload.large_not_slower_than_small` | One full-screen texture upload is correct and takes no longer than 1.5 times the time of 64 uploads of 1/64 of its size. |
| `sanity.reg.dwm.output_not_toggled` | Over a 30-second window with DWM running, the output does not switch off and on; the display source stays visible and no mode change events occur. [E] |
| `sanity.reg.lsw.maximized_not_at_drop_point` | A maximized mirrored window, after being dragged and dropped elsewhere, ends at the work-area rectangle of its monitor, not at the drop point. |
| `sanity.reg.lsw.first_launch_mirrored` | The first launch of a self-relaunching app after boot is mirrored. |
| `sanity.reg.gl.core_context_works` | Core profile contexts of every version the driver advertises are created through wglCreateContextAttribsARB and render a triangle correctly. |
| `sanity.reg.objects.many_objects_work` | Creating 200000 small objects of every kind across APIs works, each usable, and destruction returns to baseline. |
| `sanity.reg.gl.legacy_context_version_string` | A legacy context's GL_VERSION string starts with a version number that matches the highest version the extension string enables. |

### 4.27 White-box diagnostics (recorded as measures, never pass or fail)

Implementation note: each measure scans the driver logs in C:\ProgramData\av for the keywords of its observation before and after a roundtrip workload and records counts, deltas and the first matching lines; `latency.roundtrip_percentiles` and `registry.object_table` measure directly.

| measure | observation |
|---|---|
| `sanity.diag.transfer.splits` | Number of pieces each upload and readback was split into, per size class. |
| `sanity.diag.transfer.arena_size` | Largest arena or staging memory in use during the run. |
| `sanity.diag.transfer.refusals` | Count of refused or retried transfer requests. |
| `sanity.diag.transfer.slice_waits` | Count and total time of waits for a slice to become free. |
| `sanity.diag.memory.lent_pages` | Pages lent to and returned from the host over the run. |
| `sanity.diag.memory.host_window_use` | Peak host window occupancy. |
| `sanity.diag.stats.channel_counters` | Channel statistics counters before and after each group. |
| `sanity.diag.registry.object_table` | Internal registry occupancy and growth during many-object tests. |
| `sanity.diag.sched.queue_depth` | Peak queue depth per context. |
| `sanity.diag.latency.roundtrip_percentiles` | Roundtrip latency percentiles per size class. |

## 5. Benchmark suite (later)

- Policy `serial`; refuses to run next to any other `lsw-suites` instance (the lock in section 2).
- Reuses the sanity helpers; checks report `measures` (no pass or fail except a sanity bound on each number).
- Candidates: upload and readback bandwidth per API and size, submit latency, present latency and pacing jitter,
  shader and pipeline creation time, many-object creation rates, the D3D11 sample workloads.
- The matrix compares runs and GPUs and flags regressions beyond a per-measure tolerance.

## 6. Order of work

1. Done: the runner, registry, workers, policies, output, JSON, both bitnesses, `build-suites.sh`, the
   `gpu_matrix.py` suite, and `sanity.env.adapters.present` (proven on a real guest in both bitnesses).
2. Check helpers: windows, devices per API on the right adapter by LUID, patterns, compare with artifacts,
   frame-number encoding and capture readers.
3. 4.1 environment, 4.2 enumeration, 4.4 data integrity, 4.5 memory, then the host phase for 4.24.
4. Per API: 4.10 D3D11 and 4.11 DXGI, 4.8 D3D9, 4.13 OpenGL, 4.14 OpenCL, 4.15 Vulkan, 4.12 D3D12, 4.9 D3D10,
   with 4.16 formats and 4.17 shaders alongside.
5. 4.18 interop (GDI first) and 4.19 capture.
6. 4.20 presentation and pacing, 4.7 synchronisation, 4.6 isolation.
7. 4.3 kernel-mode, 4.21 errors, 4.22 lifecycle, 4.23 bitness, 4.25 LSW stack; 4.26 grows with every fixed bug.

Existing tests stay where they are: accel-virt unit tests, fuzz targets and probes cover units and bring-up;
this suite covers what an application can rely on, on real GPUs.
