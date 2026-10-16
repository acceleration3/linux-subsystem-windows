# App mimic suites: API traces of real applications

The `traces` suite replays API traces recorded from the applications LSW is
validated with (Affinity, DaVinci Resolve, Half-Life 2, Dark Souls
Remastered...) on every GPU of the test matrix, so a functionality that once
needed the application installed, signed in and driven by hand is tested in
bulk and unattended. A trace is the application's own calls, recorded while
someone used one functionality of it; nothing in a trace is hand-written.

```
traces.py capture  ->  traces.py pull  ->  traces.py replay / promote  ->  gpu-test.sh traces-sync  ->  gpu-test.sh run -- --suites traces
  (in the VM)          (into the store)     (reference pictures)            (to every host)                (every GPU)
```

## The tools

Pinned in `trace_tools.json` (URL, sha256, license), unpacked by
`traces.py fetch-tools` into the store, and only ever run in the guest.

| Tool | Version | For | License | Pinned archive sha256 |
|---|---|---|---|---|
| [apitrace](https://github.com/apitrace/apitrace) | 14.0 (win64 + win32) | D3D8/9/10/11 (DXGI) and OpenGL: `apitrace trace`, `d3dretrace`, `glretrace` | MIT | win64 `32c70268...7dc3`, win32 `3f254862...ebbe` (match GitHub's published digests) |
| [CLIntercept](https://github.com/intel/opencl-intercept-layer) | 3.0.6 (win64 + win32) | OpenCL: `CaptureReplay` writes each kernel with its source or binary, arguments, work sizes and every buffer/image before and after the enqueue, plus a `run.py` that replays it | MIT | win64 `d95bd958...0e51`, win32 `3724e1a5...22fe` (no published digest: measured at pinning) |
| [GFXReconstruct](https://github.com/LunarG/gfxreconstruct) | Vulkan SDK 1.4.363.0 | Vulkan capture layer; `gfxrecon-replay` for Vulkan and D3D12 | MIT | SDK installer `94a82d37...d141` (matches LunarG's published sha) |
| [GFXReconstruct](https://github.com/LunarG/gfxreconstruct) D3D12 (`gfxreconstruct_d3d12`) | dev 280a0ac (1.0.5-dev) | D3D12 capture (`d3d12.dll`, `dxgi.dll`, `d3d12_capture.dll` beside the executable) and `gfxrecon-replay` for D3D12, with its Agility SDK runtime (`D3D12/`) and `dxcompiler.dll` | MIT; Agility SDK and DXC redistributed as LunarG ships them | LunarG's CI artifact `gfxreconstruct-dev-windows-msvc-release` (Actions artifact 11917414057), `e6a64eba...16b8` (GitHub's published digest) |
| CPython embeddable + numpy + pyopencl | 3.13.16, numpy 2.5.4, pyopencl 2026.1.4 (+ pytools, platformdirs, siphash24, typing_extensions) | runs CLIntercept's `run.py` and `guest/clreplay.py` in the guest | PSF; BSD-3 and others; MIT (bundles the Khronos ICD loader, Apache-2.0) | wheels match PyPI's digests; full hashes in `trace_tools.json` |

Why these: apitrace and GFXReconstruct are the maintained, scriptable
capture/replay pairs with Windows builds (apitrace for everything before
D3D12, GFXR for Vulkan and D3D12), both replay to chosen frames' pictures
from a command line, and both replay through the application-facing API, so
a trace stays valid across our driver's versions. RenderDoc and PIX capture
single frames for interactive inspection, not whole functionalities for bulk
replay. CLIntercept is the only maintained OpenCL capture that records real
kernels with their real inputs *and* the outputs the application got, which
is exactly what an unattended verdict needs. GFXReconstruct's D3D12 capture
DLLs are not in the Vulkan SDK and no release ships them, so D3D12 is pinned
to LunarG's own Windows CI build (Release, MSVC) of its dev branch: an
Actions artifact, which GitHub serves only to a signed-in client and only
for 90 days, so `fetch-tools` gets it through `gh api` (`"gh_artifact"` in
the pin) and keeps it in the store; once it has expired, `fetch-tools
--from DIR` with a copy of `gfxreconstruct-dev-windows-msvc-release-280a0ac.zip`.
A D3D12 trace replays with the same build that recorded it
(`trace_store.REPLAYER_OF_API`).

## Where the other recorders fit

- **These traces** are the mimic suites: API level, replayable across driver
  builds and GPUs, judged on pictures and buffers.
- **`AV_D3D11_TRACE`** (avumd's DDI call log, `docs/testing.md` "Probes from an
  application's calls") is not replayable; it is how a failing trace becomes a
  focused probe step, as Dark Souls Remastered's became `DeferredMapWork`.
  Replay the trace with it set instead of launching the game:
  `traces.py replay dsr/title --out DIR --env AV_D3D11_TRACE=20000`, then read
  `C:\ProgramData\av\avumd-calls-<pid>.log`.
- **Wire recordings** (`record=<path>` on the device, `avrecord`) sit below
  the guest drivers: they replay the executor on the host with no VM, which
  is for bisecting an executor bug a trace replay found. They are tied to the
  protocol version and cannot replay host-memory imports, so they are no
  substitute for a trace.

## The store

`~/.local/share/lsw-gpu-test/traces/` (or `LSW_GPU_TEST_TRACE_STORE`). The
builder's is the master; `gpu-test.sh traces-sync [HOST...]` rsyncs it to each
host's `~/.local/share/lsw-gpu-test/traces/` (only what changed; `--delete`
keeps a host no bigger than the builder), and prints the host's free space.
The accel host has about 20 GB to spare: keep the whole store under ~8 GB.

```
traces/
  tools/<tool>-<version>/{x64,x86}/..., tool.json
  <app>/<functionality>/
    manifest.json       what to replay and how it is judged
    capture.trace       apitrace | capture.gfxr (GFXR) | Replay/ (CLIntercept)
    frames.txt          apitrace: the call number ending each frame
    capture.json        what trace-capture.ps1 recorded: exe, version, mode, adapters, Windows build
    capture-log.txt     the capture's log
    reference/frame-<n>.png, reference.json   the pictures a replay must match
```

`manifest.json` (validated by `trace_store.Manifest`):

| key | meaning |
|---|---|
| `schema`, `app`, `functionality`, `title` | 1; lower-case names; a line for people |
| `api` | `d3d8` `d3d9` `dxgi` (D3D10/11) `gl` (apitrace), `vulkan` `d3d12` (GFXR), `opencl` (CLIntercept) |
| `arch` | `x64` or `x86`: the apitrace build that replays it (the application's bitness) |
| `capture`, `files` | the trace, and every file the replay needs with its size and sha256 |
| `check.frames` | frames whose pictures are judged: apitrace 0-based, negative from the end; GFXR 1-based; `[]` for none |
| `replay` | `timeout_s` (900), `benchmark` (false; true adds a separate timed `-b` pass, else fps = frames / the check pass's seconds), `env`, `extra_args` (to the retracer), `cl_platform` (`avgpu`) |
| `judge` | `reference` (`promoted`, or `warp` for dxgi), `pixel_tolerance` 2, `psnr_min` 40, `changed_max_pct` 1, `psnr_warn` 30, `memory_growth_mib_max` 256, `min_fps` null, OpenCL `rtol` 1e-3 `atol` 1e-5 `itol` 0 `max_bad_fraction` 0 |
| `captured` | capture.json, plus `how`: what was done in the application, so it can be recorded again |

## How a replay is judged

Per trace, in the guest's console session (`guest/trace-replay.ps1` runs the
replayer and samples its private bytes every 500 ms):

- **apitrace**: a check pass that reads back the chosen frames as a PNM
  stream (`-s - -S <calls>`) and prints every failed call, its memory sampled
  and its frames over its seconds the fps (with `benchmark`, a `-b` pass
  first gives apitrace's own fps and the memory growth); for a `dxgi` trace
  judged against WARP with no promoted reference, a third pass renders the same
  frames with `--driver=sw`.
- **GFXReconstruct**: one pass with `--screenshots`; fps from its "Measured FPS" line.
  A `d3d12` trace replays with `gfxreconstruct_d3d12`'s `gfxrecon-replay`
  (its `D3D12/` Agility SDK beside it), a `vulkan` one with the SDK's.
- **OpenCL**: `guest/clreplay.py` builds every program source the capture
  dumped (`Programs/`, with each set of build options the application used, a
  process per build) and runs each captured kernel's own `run.py` on the
  `avgpu` platform, comparing every output with what the application's run
  left in `Post/`: floats within `atol + rtol*|ref|`, NaN equal to NaN, other
  types byte by byte within `itol`. A program the driver cannot build never
  reaches an enqueue, so the rebuild is what catches it.

Every pass is watched for the DLLs it loads (`trace-replay.ps1`, from the
32-bit PowerShell for an x86 replay): a replay must load avgpu's user-mode
driver (`avumd`, `avgl`, `avvk`) and not WARP (`d3d10warp`), and the WARP
reference pass must load WARP - so a replay that silently fell back to the
Basic Render Driver or WARP fails. A trace of fewer than 200 frames is over
too soon to be looked at, so it gets a `driver` pass of its own that keeps
its window (`-w`) until trace-replay.ps1 closes it. An OpenCL replay must
report the `avgpu` platform. The pass's loaded driver is the `driver`
measurement.

apitrace stops replaying at the last call it snapshots, so a check pass ends
at the last judged frame (the default frames include the trace's last).
VM disks are small: after each trace the guest's copy is removed when C: has
less than 4 GiB free, and sent again next time.

A trace fails on: a non-zero exit, a crash or a timeout; a device lost or
removed in the replayer's output; a GPU reset (LiveKernelEvent, display driver
4101) during it; a chosen frame missing or unlike its reference (PSNR below
`psnr_min` or more than `changed_max_pct` of pixels off by more than
`pixel_tolerance`; between `psnr_warn` and that is a note); private memory
growing more than `memory_growth_mib_max` between a third of the way in and
the end; fps under `min_fps`; an OpenCL kernel that does not replay or an
output with more than `max_bad_fraction` of its elements off. Each GPU's
frames are kept (`frame-<n>.png`, `diff-<n>.png`), and `aggregate.py` adds
**Trace frames across GPUs**: every frame and the OpenCL output hashes of every
GPU against the first GPU's, so a GPU that differs from the others shows even
before a reference is promoted. Measurements (fps, memory growth, peak private
MiB, retrace warnings, min PSNR) appear per trace in the report's
Measurements table.

## Recording a functionality

On the builder, with the LSW VM running (lsw-manager, or `gpu_matrix.py hold
--gpu N` for a chosen GPU; the commands use LSW's agent and the C$ share as
`lsw`/`lsw`):

1. `scripts/gpu-test/traces.py fetch-tools` once (and after `trace_tools.json` changes).
2. `traces.py push-tools` sends the tools and the guest scripts to `C:\av\traces`.
3. Find out what the application draws with: start it, then in the guest
   `powershell -File C:\av\traces\harness\trace-capture.ps1 -Tool probe -Process NAME`
   lists the graphics DLLs it loaded (d3d9, d3d11, opengl32, OpenCL, vulkan-1, wpfgfx...).
4. Start the capture, e.g.
   `traces.py capture hl2/trainstation --tool apitrace --api d3d9 --exe "C:\...\hl2.exe" --steam-app 220 --arguments "..." --seconds 60 --repack`.
   It runs `trace-capture.ps1` in the VM's desktop; use the application for the
   one functionality, briefly (the trace holds every call, so length is size),
   then quit it or let `--seconds` close it. CLIntercept's `system` mode (MSIX
   apps) swaps System32's `OpenCL.dll` and needs an elevated shell: `--elevated`
   starts the capture through `desk.py`'s elevated helper (`desk.py start`
   first), or run what `--print` prints in an elevated PowerShell in the VM.
   `--gl-trace APP/FUNC2` (with `--tool clintercept`) records the same run's
   OpenGL too, through apitrace's `opengl32.dll` wrapper, as a second trace;
   `--clear PATH` deletes a file first (an OpenCL kernel cache).
   `--drive SCRIPT.ps1` uses the application by scripted input instead of by
   hand: the script (with a `drive.ps1` beside it put first) runs in the VM's
   desktop once the capture started, and should quit the application at its
   end; `--wait N` then waits for the capture to finish (the Affinity suite
   below is recorded this way).
5. `traces.py pull hl2/trainstation --title "..." --how "what you did"` copies the
   capture into the store and writes `manifest.json` (five frames spread over the
   trace past its first tenth, `--frames` to choose; `dxgi` traces judged against WARP).
   An apitrace capture is mended on the way in (`trace_mend.py`, below), and so
   is a CLIntercept one whose application made programs from device binaries
   (`cl_sources.py`: each such kernel gets the recorded source of its program,
   built with its options, so it replays off the GPU it was captured on);
   `traces.py mend APP[/FUNC]` does the same to traces already in the store.
6. `traces.py replay hl2/trainstation --out /tmp/x` replays it on the running VM; look at
   `/tmp/x/traces/hl2/trainstation/frame-*.png`, then
   `traces.py promote hl2/trainstation /tmp/x` makes them the references (or, for a
   WARP-judged trace, `--warp` keeps the WARP frames). A matrix run's pass directory
   (`build/gpu-test/RUN/HOST/GPU`) promotes the same way; pick a GPU the
   cross-GPU table shows agreeing with the others.
7. `traces.py check`, then `gpu-test.sh traces-sync`, then
   `gpu-test.sh run [HOST...] -- --suites traces [--traces hl2,dsr/title]`
   (`--traces` takes apps, APP/FUNC and shell patterns: `resolve/fx-*`, `*-gl`).

A capture made on another host's VM: `traces.py pull` there, then
`gpu-test.sh traces-pull HOST APP/FUNC` brings it into the builder's store.

## Limits

- D3D9 and OpenGL have no WARP: their references are promoted from a GPU (or
  from a bare-metal Windows machine's replay, if one is ever available).
- apitrace cannot trim a Direct3D trace; keep captures short, use `--repack`
  (zstd), and cap the frame rate in games (`+fps_max 30` in Source).
- WGC (Windows.Graphics.Capture) frames arrive from DWM as cross-process shared
  surfaces that a replay cannot open: there is no WGC trace, and the `wgc` app
  suite (remotewin-wgcbench) stays its mimic.
- Content an application produced outside the traced API (OpenCL writing a GL
  or D3D texture through interop, video decode) is undefined in a replay:
  judge such frames across GPUs only, or not at all (`check.frames: []`).
- An application that caches OpenCL program binaries replays them as
  `DeviceBinary*.bin`, tied to the driver that built them: clear its kernel
  cache before capturing so CLIntercept records sources. Resolve's is
  `%APPDATA%\Blackmagic Design\DaVinci Resolve\Support\DVIP\Cache\OpenCL.cache`.
- WPF's D3D9Ex rendering uses shared surfaces, which d3dretrace does not
  recreate ("shared surfaces unsupported"): its presents fail in the replay,
  so the pictures are the render targets at the frame's calls - device,
  shaders, textures and draws still run through avumd.
- Injection (apitrace's `inject`) broke Resolve's plugin loading ("Unable to
  load plugin ... module could not be found"): Resolve is traced with the
  `opengl32.dll` wrapper beside it, from an elevated shell. An elevated
  application then takes input only from an elevated sender (UIPI):
  `desk.py` sends it - `desk.py start` runs `guest/desk-server.ps1` from a
  console-session task made with `schtasks /rl highest`, which then runs the
  PowerShell snippets `desk.py ps` drops into `C:\av\apps\desk` (Click,
  Drag, Press 'alt+s', Type-Text, Shot, Windows, Resolve-Lua...) in about a
  second each, and `traces.py capture --elevated` starts trace-capture.ps1
  through it.
- CLIntercept's `local` mode did not intercept Resolve (its OpenCL.dll is
  loaded from System32): use `system`.
- Half-Life 2 launched by Steam did not load apitrace's `d3d9.dll` wrapper;
  injected and started directly (steam_appid.txt) it traces fine. A
  `+exec` cfg with `wait` does not work before a map loads (no server):
  the capture is closed by `--seconds`.
- CLIntercept's controls live in `HKCU\Software\INTEL\IGFX\CLIntercept`;
  trace-capture.ps1 adds its own there and keeps any set by hand, so
  `CallLogging = 1` set first logs every OpenCL call of a capture (into
  `clintercept-log.txt` beside it, also when nothing was captured).

### Mending apitrace's traces (`trace_mend.py`)

apitrace 14.0 records two things its own d3dretrace cannot replay, and
`traces.py pull` (or `mend`) rewrites them, on the host, reading the trace as
data (apitrace's format, `docs/FORMAT.markdown`, version 6):

- `IDXGIDevice2::ReclaimResources`' `pDiscarded` is an array of
  `NumResources` BOOLs, which apitrace's `specs/dxgi.py` declares a pointer to
  one: the tracer records one, d3dretrace passes room for one, and the
  runtime writes them all - heap corruption (0xC0000374) a few calls later,
  on any driver, WARP's too. Direct2D offers and reclaims its buffers all
  the time (Affinity's whole UI), so every Affinity trace past its start
  died. The mended array holds `NumResources` copies of the recorded BOOL.
- Calls the application's exit cut off (`apitrace dump` ends with
  `// incomplete`) are replayed anyway, and a cut-off `Release` crashed the
  replay (affinity/text, WARP too): the mended trace ends before them.

A mended trace is written as one zstd frame followed by a 4 MiB skippable
frame: apitrace's reader for a zstd trace without its seek table stops at
the read that finds the file's end even with up to 2 MiB of compressed data
still buffered, which lost the last 1000-2000 calls (and frames) of each
Affinity trace until the padding. The manifest's `captured.mended` says what
was changed; call numbers do not change, so `frames.txt` stays valid.

## Recorded traces

Captured on the test host's Intel Arc (MTL) VM on 2026-10-15; references
promoted from that GPU (DSR's rendered by WARP). The first matrix run
(`traces-arc-1`) replayed every one on avgpu, matching its references
exactly, except `resolve/opencl-media`, which reproduced a driver failure
(CL_OUT_OF_RESOURCES creating its 293 KB program) and so held no build
options and no kernel. It was recorded again on 2026-10-16 on the builder's
RX 9070 XT, once large programs crossed the transport: Resolve's own options,
five programs and seven kernels, which replay bit-identically to Resolve's
outputs on the RX 9070 XT and the Radeon 610M (needs accel-virt 99eeaebb, a
host queue flush for an event the guest waits on).

| Trace | API / tool / how | Size | Covers | Judged |
|---|---|---|---|---|
| `hl2/trainstation` | D3D9 (x86), apitrace injected, hl2.exe started directly with steam_appid.txt, repacked zstd | 439 MiB, 3137 frames | The d1_trainstation_01 train ride at fps_max 30, G-Man intro included, no input | 5 frames, promoted reference |
| `dsr/title` | D3D11 / DXGI, apitrace injected, steam_appid.txt | 276 MiB, 3178 frames | Logos, title screen, the online-notice dialog | 5 frames, WARP reference (PSNR 61 against avgpu), memory growth |
| `affinity/login-wpf` | D3D9Ex (WPF), apitrace injected inside the MSIX package | 1 MiB, 2 frames | The signed-out login window: WPF device, shaders, textures, draws (shared surfaces not replayed) | 2 frames |
| `resolve/ui` | OpenGL (Qt), apitrace `opengl32.dll` wrapper, elevated | 2.7 MiB, 22 frames | Project manager, New Project 1 opened, Cut/Media/Edit pages | 5 frames |
| `resolve/opencl-media` | OpenCL, CLIntercept system mode, elevated, kernel cache cleared | 604 MiB, 5 programs, 7 kernels | The DVIP programs Resolve builds (293 KB to 1.3 MB, its own options) and the kernels it runs when New Project 1 opens and its Cut, Edit, Color and Media pages are shown | every program builds; every kernel's outputs against Resolve's, and across GPUs |

Not recorded, and why:

- `dsr/firelink`: the saved game in the VM is in the Undead Asylum (before
  its first boss), so the in-game Dark Souls traces are the Asylum's
  courtyard (the game suites, below).
- WGC: not traceable (see Limits).

## The game suites

### Dark Souls Remastered (`dsr`, Direct3D 11)

Besides `dsr/title`, three in-game traces of the Undead Asylum's courtyard
(the bonfire courtyard before its first boss), each one game session:
DarkSoulsRemastered.exe started directly (steam_appid.txt) under apitrace
`-a dxgi`, E through the logos, the title and its notices to Continue, the
scene, then WM_CLOSE. Recorded on the builder's RX 9070 XT on 2026-10-16,
the game at 1280x720 windowed, offline, its other settings default (TAA,
motion blur, depth of field, ambient occlusion on). The first ~1300 frames
of each are the logos, title and load, the scene the last ~280 (the traced
game runs at about 9 fps), so the judged frames are all in the scene.

| Trace | Frames | Size | What the script does after the load |
|---|---|---|---|
| `dsr/courtyard` | 1716 | 321 MiB | a slow 360-degree camera turn round the courtyard, the bonfire walked up to, the camera round it, up and down, a walk across and a turn back: the courtyard's stone and moss, the bonfire's fire, light and shadows |
| `dsr/combat` | 1489 | 312 MiB | a three-swing combo with the Straight Sword Hilt by the stairs, a backstep, a roll towards the bonfire, two swings by its fire, a roll left, a turn and two more swings: attack, roll and backstep animations with motion blur |
| `dsr/menus` | 1633 | 308 MiB | the Esc menu over the courtyard: its bar, the Inventory page (list moved through), Equipment (slots moved through), Status and System, each left with Q; nothing used, equipped or changed |

References are WARP's (`promote --warp`). Against WARP the in-game frames
are close but not exact - PSNR 40 to 50 dB, 1.4 to 16% of pixels off by more
than 2 levels and under 0.5% by more than 16, spread over the whole picture
(TAA, SSAO and motion blur carry each rasterizer's rounding from frame to
frame), so these three are judged at `pixel_tolerance` 16 and `psnr_min` 38.
The RX 9070 XT and the Radeon 610M render every judged frame within 2 levels
of each other on all but a few pixels (at most 9). `gpu-test.sh run local --
--suites traces --traces dsr` (run `dsr-local-1`): both GPUs pass all four
traces; 36 to 44 fps in-game, memory growth about 44 MiB.

To record one again, with the VM held (`gpu_matrix.py hold`):

    # first, in the VM's desktop session: scripts/gpu-test/dsr/prepare.ps1
    traces.py capture dsr/courtyard --tool apitrace --api dxgi \
      --exe 'C:\Program Files (x86)\Steam\steamapps\common\DARK SOULS REMASTERED\DarkSoulsRemastered.exe' \
      --steam-app 570940 --seconds 300 --repack --drive scripts/gpu-test/dsr/courtyard.ps1 --wait 900
    traces.py pull dsr/courtyard --replace

`dsr/prepare.ps1` must run first, before each capture: the game saves as it
is played, and every scene starts from the save kept in `C:\av\dsr`
(`DRAKS0005.sl2`, made by playing to the courtyard, and `DarkSouls.ini`,
1280x720 windowed). `dsr/drive.ps1` (put before each scene) sends keys as
scan codes and the camera as relative mouse motion, and gets from the launch
to the game by looking: E until the title menu's highlight is on Continue
(up arrows if it is not), Continue, and the HUD's stamina bar. Under apitrace
the game runs at about 9 fps and moves by a capped step a frame, so a walk
covers about a third of what it does untraced: an arcade scene (the shade
under the courtyard's arches) never got past the bonfire in two attempts
and was left out.

### Resident Evil 4 Chainsaw Demo (`re4demo`, Direct3D 12): not recorded

`traces.py capture re4demo/... --tool gfxr --api d3d12` records it (the
`gfxreconstruct_d3d12` tool's capture DLLs beside re4demo.exe, started
directly with steam_appid.txt), and a D3D12 trace replays and is judged like
a Vulkan one (`--screenshots`, 1-based frames, five past the first tenth by
default, the frame count from gfxrecon-info). No trace is in the store:

- The game quit at its start with "Fatal D3D error (30)": avumd answered
  D3D12's pipeline library slots E_NOTIMPL and the runtime removed the
  device. Fixed on accel-virt branch `d3d12-pipeline-library` (not yet
  deployed), with the D3D12 probe's `pipeline_library` step.
- With that fix the game reaches its menus, but the host then refused some
  of its compute pipelines (70 to 85 KB DXIL programs, which cross through
  the blob arena): the bytes the executor read were not the program the
  command named - another shader's header with a different size, or noise -
  and the device was removed. Not yet found.
- The VM's disk image on this builder's btrfs reads back with checksum
  errors (`csum failed` on lsw.qcow2 in the kernel log, Windows' disk event
  7 "has a bad block"), which the game's 10 GiB pack is exposed to.

### Half-Life 2 (`hl2`, Direct3D 9 and Vulkan): not recorded

Planned as `hl2/<scene>` (D3D9, apitrace, as `hl2/trainstation`) and
`hl2/<scene>-vk` (`-vulkan`, GFXReconstruct's layer), the same scenes in
both. The VM's disk was grown to 100 GiB, but C: cannot take the space: the
recovery partition sits right after it (C: ends at 63.6 GB, the recovery
partition follows, 40 GiB free after that).

## The Affinity suite

Affinity 3.3.1 (Canva's, MSIX, signed in to Canva) draws its canvas and the
canvas's tools with Direct3D 11 - Direct2D's own drawing (`D2D Internal`
buffers, its glyph and geometry atlases) and Affinity's - and its panels
with WPF's D3D9Ex. Each functionality is one `dxgi` trace,
`affinity/<functionality>`: Affinity started inside its package with apitrace
injected and a photo as its argument, the functionality used by a script,
Affinity closed without saving. `--traces affinity` selects them all (an
app name selects its every trace).

| Trace | Frames | Size | What the script does after the photo opens |
|---|---|---|---|
| `affinity/open-image` | 4 | 8 MiB | nothing (the 3000x2000 JPEG decoded, uploaded and drawn fit to the window) |
| `affinity/zoom-pan` | 85 | 57 MiB | Ctrl+wheel in 4 steps, middle-drag pans, 100%, pan, wheel out 6 steps, fit |
| `affinity/brush` | 143 | 43 MiB | four Paint Brush strokes on the photo's pixel layer, the last one wider |
| `affinity/selection-mask` | 89 | 13 MiB | a rectangular marquee made a layer mask, then painted into |
| `affinity/layers-blend` | 46 | 43 MiB | duplicated layers: Multiply at 50% moved, a Lighter Color copy |
| `affinity/vector-shapes` | 65 | 24 MiB | a rectangle (default fill) and a pen curve |
| `affinity/text` | 65 | 31 MiB | 496 pt artistic text typed |
| `affinity/adjustments` | 61 | 80 MiB | HSL (hue, saturation) and Curves adjustment layers |
| `affinity/live-filters` | 81 | 63 MiB | Gaussian Blur and Unsharp Mask live filter layers, radii dragged |
| `affinity/transform` | 23 | 25 MiB | a duplicate rotated by its handle and moved |
| `affinity/export-png` | 10 | 8 MiB | Export PNG of the photo to a file |

All with the photo `C:\av\affinity\photo.jpg`, made on the host (ImageMagick
7.1.2-33):

    magick -seed 4242 -size 3000x2000 plasma:skyblue-orange -blur 0x3 -modulate 110,140 \
      -fill '#f4d27a' -draw 'circle 2200,600 2200,850' -fill '#3f6d4e' -draw 'polygon 0,2000 900,900 1700,2000' \
      -fill '#5a8a63' -draw 'polygon 1100,2000 2100,1000 3000,2000' -attenuate 0.3 +noise Gaussian -quality 92 photo.jpg

Recorded on the builder's RX 9070 XT on 2026-10-16. References are WARP's
(`promote --warp`); the RX 9070 XT and the Radeon 610M render all 56 judged
frames identically to each other (bit for bit) and to WARP within the
limits, except two that are only close (a note): Direct2D draws its
outlines into an R16_UINT coverage atlas with target-independent
rasterization (`ForcedSampleCount = 16` with a render target bound, logic-op
OR), which avgpu rasterizes at one sample (DXVK takes `rasterizationSamples`
from the render target's sample count, and RADV offers 8 at most), so
hairline strokes in the atlas lose rows (from the first draw into the atlas,
call 5907 of export-png, found by snapshotting every draw of the frame on
avgpu and WARP): only the canvas border and the selection box differ in
`export-png` frame 9 (grey and a pixel off where WARP's are magenta) and
`layers-blend` frame 35. Those notes clear when TIR is implemented.
`gpu-test.sh run local -- --suites traces --traces affinity` (run
`affinity-local-1`, 2026-10-16): both GPUs pass all twelve traces.

To record one again (VM held, Affinity signed in, the window, persona and
tool state as `affinity/drive.ps1` describes, the photo copied to
`C:\av\affinity\photo.jpg`):

    traces.py capture affinity/brush --tool apitrace --api dxgi --package Canva.Affinity_8a0j1tnjnt4a4 \
      --arguments 'C:\av\affinity\photo.jpg' --seconds 600 --repack \
      --drive scripts/gpu-test/affinity/brush.ps1 --wait 300
    traces.py pull affinity/brush --replace --how "..."

The scripts click at fixed desktop coordinates (2560x1440 at 125%, the
window 1920x1010 at 320,320): a different desktop, window or UI layout
needs them found again (`Launch` in drive.ps1 starts Affinity outside a
capture for that). A trace's last frame is often cut short by Affinity's
exit; pull lists the frames apitrace saw, and mending ends the trace before
the cut-off calls.

OpenCL: no trace. With "Enable OpenCL compute acceleration" on, Affinity
makes one avgpu context (found through `clGetDeviceIDsFromD3D10KHR`), 18
queues, and probes memory (1025 small buffers, 1024 maps; it asks
`CL_DEVICE_GLOBAL_FREE_MEMORY_AMD` with one size_t's room, which avcl
refused until accel-virt `cl-free-memory-one-value`), then builds no program
and runs no kernel - not on opening, painting, adjustments, live filters,
zooming or exporting (CLIntercept system mode with `CallLogging`, and
avinspector showing the OpenCL contexts' batch counts unchanged while the
filters ran). Its raster work reaches the GPU through Direct3D 11.

## The Resolve suite

DaVinci Resolve 21.1.1.10 (free edition) draws its UI and viewers with
OpenGL (Qt) and does its image processing with OpenCL (DVIP), handing
pictures between them through CL-GL interop. Each feature below is one
Resolve session recorded twice at once: `resolve/<feature>-cl` (CLIntercept,
system mode: every program Resolve built, with each set of its options,
and the first enqueue of each kernel object with its buffers before and
after) and `resolve/<feature>-gl` (apitrace's `opengl32.dll` wrapper beside
Resolve.exe: every GL call of the session, zstd). `--traces resolve` runs
them all; `--traces 'resolve/*-cl'`, `'resolve/fx-*'` pick by pattern.

| Feature | `-cl` (OpenCL) | `-gl` (OpenGL) | What the session does |
|---|---|---|---|
| `media` | 612 MiB: 8 programs, 31 kernels | 6 MiB: 134 frames | the Media page: pattern.mp4 (H.264) selected in the media pool and played 4 s in its viewer |
| `edit` | 790 MiB: 8 programs, 29 kernels | 7 MiB: 138 frames | the Edit page: a timeline of four cuts (H.264, ProRes, DNxHR, PNG) over a WAV played 4 s, then scrubbed along the ruler and back |
| `dissolve` | 335 MiB: 8 programs, 27 kernels | 6 MiB: 162 frames | the Edit page: a 1 s Cross Dissolve between H.264 and DNxHR clips played through |
| `title` | 540 MiB: 10 programs, 30 kernels | 6 MiB: 33 frames | the Edit page: a Text+ title (Fusion) under an H.264 clip composited in Screen mode, played 4 s |
| `grade` | 696 MiB: 11 programs, 33 kernels | 7 MiB: 127 frames | the Color page: a primary grade (lift/gamma/gain wheels, contrast, custom S curve) on DNxHR, played 3 s with the parade scope |
| `nodes` | 719 MiB: 7 programs, 26 kernels | 13 MiB: 209 frames | the Color page: three serial nodes added and graded live (offset wheel, contrast 1.5, gamma wheel), then played 3 s |
| `qualifier` | 689 MiB: 6 programs, 25 kernels | 7 MiB: 133 frames | the Color page: an HSL qualifier (a yellow bar) and a circular power window with gain and offset inside, on H.264, played 3 s |
| `fx-blur` | 756 MiB: 9 programs, 37 kernels | 6 MiB: 138 frames | the Color page: Resolve FX Gaussian Blur on a node, played 3 s |
| `fx-glow` | 711 MiB: 9 programs, 35 kernels | 6 MiB: 127 frames | the Color page: Resolve FX Glow on a node, played 3 s |
| `fx-vignette` | 710 MiB: 10 programs, 33 kernels | 6 MiB: 123 frames | the Color page: Resolve FX Vignette on a node, played 3 s |
| `fx-sharpen` | 790 MiB: 10 programs, 32 kernels | 6 MiB: 125 frames | the Color page: the Blur palette's Sharpen (radius lowered) on a node, played 3 s |
| `scopes` | 784 MiB: 7 programs, 33 kernels | 6 MiB: 43 frames | the Color page's scopes over the graded clip's first frame: waveform, vectorscope, histogram, CIE chromaticity, parade |
| `fusion` | 588 MiB: 8 programs, 26 kernels | 8 MiB: 298 frames | the Fusion page: Background -> Transform -> Merge over a ProRes clip (MediaIn -> MediaOut), played 4 s |
| `fairlight` | 350 MiB: 3 programs, 10 kernels | 6 MiB: 388 frames | the Fairlight page: the edit timeline's WAV track and video played 4 s |
| `deliver` | 335 MiB: 5 programs, 15 kernels | 6 MiB: 53 frames | the Deliver page: the dissolve timeline (148 frames) rendered to ProRes 422 in a .mov |

The OpenCL traces are 9.5 GB as files but mostly zeros (CLIntercept's
`InitializeBuffers` and Resolve's pooled buffers): zstd takes the 601 MiB
`resolve/opencl-media` to 0.9 MB, and on the hosts' btrfs (`compress=zstd`)
adding all 32 traces did not move `df`. A trace that large is never kept in
the guest after its replay (`KEEP_MAX_MIB` in trace_suite.py), since the VM's
disk grows on the host's: keeping them all took the accel host from 14 to
8 GB free.

How they are judged, and the first results. Recorded on accel's RTX 3070 Ti
(avgpu over NVIDIA 580) on 2026-10-16; the GL references are that GPU's
replay (`traces.py promote`). The GL traces' viewers are black in a replay:
Resolve's pictures reach them through CL-GL interop, which apitrace does not
record. `fx-blur-gl` frame 44 is not judged - its vectorscope texture is
interop-filled and undefined (garbage on the Iris Xe). The OpenCL traces are
judged with `itol 5` and `max_bad_fraction 0.025`: Resolve builds with
`-cl-fast-relaxed-math -cl-mad-enable`, and on the Iris Xe up to 2% of some
byte masks flip and 8-bit values round off by a few (the RTX 3070 Ti
reproduces all 429 kernels bit for bit). Six kernels' outputs are not judged
(`fx-sharpen-cl` 17, 18, 54 and `scopes-cl` 217, 283, 302; each manifest's
`captured.mended` says so): both GPUs' replays agree with each other bit for
bit and differ from what Resolve's run recorded, so the recording, not the
replay, is the odd one out; they still build and run, and the cross-GPU
table compares them. Run `resolve-matrix-2` (`gpu-test.sh run accel --
--suites traces --traces resolve --gpus 0,1`): both GPUs pass all 30
traces (plus `resolve/ui` and `resolve/opencl-media`): 211 of 211 program
builds and 429 kernels replayed on each (bit-identical to Resolve's outputs:
429 on the RTX 3070 Ti, 320 on the Iris Xe), GL frames at PSNR 55 dB or more
against the references, and across the two GPUs; about 8 minutes of replay
per GPU (25-30 minutes with sending and booting).

Every session starts the same way, so every trace also covers Resolve's
start: the project manager, `lsw-mimic` opened, the console typed into (its
window is part of each GL trace) and Resolve quitting.

### Re-recording

1. Media: `scripts/gpu-test/resolve-media.sh DIR` (ffmpeg's lavfi sources:
   SMPTE HD bars as ProRes 422 Proxy, testsrc2 as H.264 High, a mandelbrot
   zoom as DNxHR LB - all 1280x720, 25 fps, 4 s - testsrc2's first frame as a
   1920x1080 PNG, and a 440/660 Hz stereo tone as 24-bit WAV), copied to
   `C:\av\media\resolve`; the grades `guest/resolve-grades/*.drx` to
   `C:\av\media\resolve\grades`; `guest/resolve-*.lua` to `C:\av\apps`.
2. The project: with Resolve open on any project, in its console (F6)
   `dofile([[C:\av\apps\resolve-setup.lua]])` makes `lsw-mimic` (1280x720,
   25 fps): the timelines `edit` (four cuts over the WAV), `dissolve` (a 1 s
   Cross Dissolve), `title` (Text+ under the H.264 clip in Screen mode),
   `fusion` (MediaIn -> Merge <- Transform <- Background -> MediaOut, built
   by Fusion's scripting), `audio`, and one clip each for the Color page
   features. Then each graded timeline gets its grade in a Resolve session of
   its own (`LSW_TIMELINE=[[qualifier]]; dofile([[C:\av\apps\resolve-grade.lua]])`,
   Resolve quit after): applying a second DRX in one session, or the
   three-node `nodes` grade to a fresh clip, crashed Resolve 21.1 every time
   (its UI thread calling address 1), so `nodes` is graded live in its
   capture instead. The DRX files were made by hand on the Color page and
   exported with `resolve-export-grades.lua`; the free edition renders
   Resolve FX Sharpen, Film Grain and Lens Blur with a Studio watermark, so
   the suite uses Gaussian Blur, Glow, Vignette and the Blur palette's
   Sharpen.
3. Captures: with the VM held (`gpu_matrix.py hold --gpu N`) and `desk.py
   start`, `scripts/gpu-test/resolve_captures.py capture FEATURE...` on the VM's
   host does each one end to end - `traces.py capture --tool clintercept
   --mode system --gl-trace ... --clear <the DVIP kernel cache> --repack
   --elevated`, the project opened from the project manager, `resolve-go.lua`
   typed into the console for the timeline and page, the feature's input
   (its table in `resolve_captures.py`, at the coordinates of Resolve
   maximised on the VM's 2560x1440 desktop), `resolve:Quit()`, then
   `traces.py pull` of both traces with the `how` written down. Pull each
   one into the builder's store (`gpu-test.sh traces-pull HOST APP/FUNC`)
   before anything syncs the store to that host: `traces-sync` deletes what
   the builder does not hold.

Resolve's free edition refuses external scripting (fuscript finds no
Resolve), its Scripts menu lists no Python script without a Python
install, and its console's Lua has no `io`, `os.remove` or `require`: so
the scripts above run through `dofile` typed into the console
(`desk.py resolve-lua`), and what they print is read off a screenshot.

### What recording it found

- **256 GL objects per OpenCL context on a copying host.** On a host whose
  OpenCL cannot share with an EGL context (NVIDIA), the executor copies GL
  objects for OpenCL (`CopiedGlSharing`) and held 256 per context: Resolve
  keeps every texture it registers for interop, and a few minutes of
  switching pages and timelines took it past 256 (CLIntercept call log:
  creation #259 refused at exactly 256 live) - "Failed to register OpenGL
  object for OpenCL interop: CL_OUT_OF_RESOURCES", then "Your GPU memory is
  full". accel-virt branch `cl-gl-mirrors-unbounded` holds as many as the
  guest holds cl_mems.
- **256 GLsyncs per GL context.** Resolve never calls `glDeleteSync` (84
  `glFenceSync`, 0 `glDeleteSync`, 5694 `glGetSynciv` in a 2-minute Color
  page trace); the executor bound 256 per context and broke every later one
  ("a GLsync no token could keep was deleted as it was made" in QEMU's log),
  which answers `glGetSynciv` with status 0 for ever. Resolve's texture pool
  then never recycles a texture: with the scopes playing it registered a new
  one for OpenCL every frame (1819 in a minute, all kept), which hit the cap
  above in seconds, and with that cap raised filled the host's memory until
  the kernel OOM-killed QEMU. accel-virt branch `gl-sync-tokens` retires
  signaled syncs, and makes the guest's token mint atomic (two threads
  minting at once repeated tokens). Until both are in, `scopes` is recorded
  with the playhead parked, and long Resolve sessions on NVIDIA fail.
- Resolve 21.1 itself crashed (its UI thread calling address 1) applying a
  second DRX grade in one session through the scripting API, and a
  three-node DRX to a fresh clip, every time (minidumps show the UI thread
  in Fusion and Qt frames, no avgpu frame); not traced further -
  `resolve-grade.lua` works around it.
