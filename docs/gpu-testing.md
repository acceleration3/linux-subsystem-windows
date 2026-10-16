# GPU testing

LSW's graphics stack (accel-virt) is tested on real hardware: every graphics
API's suites, on every GPU of every test machine, inside each machine's LSW
VM. Everything that does this is in [`scripts/gpu-test/`](../scripts/gpu-test).

One machine is the **builder**. It builds LSW and accel-virt once, copies what
a run needs to each test machine, starts the runs, and gathers the results.
Each machine then tests itself. For each of its GPUs, it redefines its VM
with one accel-virt adapter bound to that GPU, boots it, runs the suites in
the guest, and puts the VM's own definition back at the end.

```
gpu-test.sh build  ->  gpu-test.sh deploy  ->  gpu-test.sh run  ->  gpu-test.sh wait  ->  gpu-test.sh report
  (builder)            (to every host)         (every GPU, in       (until all done)       (failures, per host
                                                parallel per host)                         and GPU)
```

## Quick start

From the checkout, on the builder:

```sh
scripts/gpu-test/gpu-test.sh build              # LSW, accel-virt, the test programs
scripts/gpu-test/gpu-test.sh deploy             # rsync to every host, then preflight
scripts/gpu-test/gpu-test.sh run                # the default suites on every GPU
scripts/gpu-test/gpu-test.sh wait               # prints each host's progress until done
scripts/gpu-test/gpu-test.sh report             # the matrix and every failure
```

`gpu-test.sh all` runs build, deploy, run, wait, collect and report in one go.

To restrict hosts, name them. Pass `gpu_matrix.py run` options after `--`:

```sh
scripts/gpu-test/gpu-test.sh run accel -- --gpus 1 --suites probes,vulkan
scripts/gpu-test/gpu-test.sh run local accel -- --suites probes,d3d11,d3d12,vulkan,opengl,opencl,clgl,wgc,affinity,resolve,traces
LSW_GPU_TEST_RUN=my-run scripts/gpu-test/gpu-test.sh run local    # name the run yourself
```

A full run takes roughly 20–40 minutes per GPU for the default suites. The
app and trace suites add about an hour per GPU. Hosts run in parallel; the
GPUs of one host run one after another.

## The machines

[`scripts/gpu-test/hosts`](../scripts/gpu-test/hosts) lists them, one per
line: a name, then where ssh reaches it. `local` is the builder itself.

```
local   local
test    test@192.168.1.199
accel   accel@192.168.1.15
```

| Host | GPUs | Notes |
|---|---|---|
| local (builder) | AMD RX 9070 XT, Radeon 610M (iGPU) | CachyOS x86-64-v4 |
| test | Intel Arc (Meteor Lake) | fish login shell, pt_PT locale |
| accel | Intel Iris Xe, NVIDIA RTX 3070 Ti Laptop | x86-64-v3 only, fish login shell, NVIDIA 580.x |

Every host needs:

- **An LSW VM.** The libvirt domain `lsw` in `qemu:///session` has the guest
  account `lsw`/`lsw`, which the guest agent and the C$ share use. On a new
  host, `gpu-test.sh provision HOST` installs one.
- **These tools:** `virsh`, `smbclient`, `vulkaninfo`, `clinfo`, and
  `/dev/kvm` readable and writable by the user.
- **Non-interactive ssh from the builder.** Install the builder's key with
  `gpu-test.sh setup-keys`. Where a key cannot be installed, open an ssh
  master connection yourself and point the script at it:

  ```sh
  mkdir -p /tmp/cm
  ssh -o ControlMaster=yes -o ControlPath=/tmp/cm/%C -o ControlPersist=yes -fN accel@192.168.1.15   # asks for the password once
  export LSW_GPU_TEST_CONTROL_PATH=/tmp/cm/%C                                                          # before every gpu-test.sh
  ```

- **lsw-manager closed.** A run takes the VM over, shutting it down and
  restarting it several times. Preflight refuses to start while lsw-manager
  is open; `--allow-manager` overrides that.

`gpu-test.sh preflight [HOST...]` lists whatever a host lacks: missing tools,
no VM, lsw-manager open, files missing from the deployed tree, a library the
host's loader cannot find, no Vulkan GPU. `deploy` runs it at the end.

`deploy` copies the tree to `~/.local/share/lsw-gpu-test/tree` on each host,
laid out as in the checkout. Hosts run from that copy, never from a checkout
of their own. In particular, an lsw-manager launched from an old checkout on
a host is not the build under test. To use the tested build interactively,
run `~/.local/share/lsw-gpu-test/tree/build/src/host/lsw-manager`.

## The suites

`--suites` takes a comma-separated list. The default is the first seven rows.

| Suite | What runs | How it is judged |
|---|---|---|
| `probes` | avd3d9probe, avd3d11probe, avd3d12probe, avvkprobe, avglprobe, avclprobe | step by step, every step the probe must pass |
| `d3d11` `d3d12` `vulkan` `opengl` `opencl` | accel-virt's sample suites: every sample in the VM and natively on the same host GPU | the VM ran it, and its picture matches the host's |
| `clgl` | the OpenCL–OpenGL sharing samples | each checks the values it moved |
| `wgc` | remotewin-wgcbench: a D3D11 or GDI window, the monitor or the window captured | its own PASS line (at least 57 fps captured, no gap spanning more than 3 refresh intervals, a real picture), and the desktop still composed |
| `affinity` | Affinity (Canva), installed from its MSIX | the adapters and OpenCL device its log reports; its WPF window draws |
| `resolve` | DaVinci Resolve | the OpenCL device `gpudetect.bin` chose |
| `hl2` | Half-Life 2 (Steam 220): 60 s of the opening train ride | runs, timed by PresentMon |
| `dsr` | Dark Souls Remastered (Steam 570940): title and menus | runs, timed by PresentMon |
| `traces` | API traces recorded from real apps and games, replayed | pictures, buffers, memory and speed against references (see below) |

Every suite also fails on a GPU reset during it, meaning a LiveKernelEvent or
display driver event 4101 in the guest's logs.

Other `run` options:

| Option | Meaning |
|---|---|
| `--gpus 0,1` | indices, UUIDs or parts of names (default: all) |
| `--traces resolve,dsr/courtyard,resolve/fx-*` | the traces suite's selection: apps, APP/FUNC, or patterns (default: the whole store) |
| `--frames N`, `--warmup N` | timed frames per sample (the samples test correctness, not speed) |
| `--force-driver` / `--skip-driver` | reinstall the guest driver package even if unchanged / leave the guest's as it is |
| `--no-device-loss` | leave out the test layer that loses a host device on request (and the D3D11 probe's loss steps) |
| `--no-reboot-between` | every suite in one boot instead of a fresh boot per suite |
| `--qemu-env NAME=VALUE` | set in QEMU's environment for this run |

### What the app suites need in the VM

The application suites use the user's own installs in each host's VM. A
suite whose application is missing fails and says so.

- **affinity:** Affinity installed from its MSIX and signed in to Canva.
- **resolve:** DaVinci Resolve installed and opened once (licence accepted).
- **hl2, dsr:** Steam installed and signed in, with the game installed and
  run once.
- **wgc** on a laptop: the host in the **performance** power profile. In
  power-saver the Iris Xe measured 54 fps, under the 57 fps floor.

## The trace suites (app mimics)

The `traces` suite replays API traces recorded from the real applications
while someone used one feature of each. A recording is the application's own
D3D9/D3D11/D3D12, OpenGL, Vulkan or OpenCL calls, nothing hand-written. That
way an application is tested on every GPU without installing, signing in to
or driving it. [`scripts/gpu-test/TRACES.md`](../scripts/gpu-test/TRACES.md)
documents this in full: the tools, the store layout, judging, how to record a
new feature, and each suite's table.

| App | Traces | API |
|---|---|---|
| `affinity` | open-image, zoom-pan, brush, selection-mask, layers-blend, vector-shapes, text, adjustments, live-filters, transform, export-png, login-wpf | D3D11 |
| `resolve` | media, edit, dissolve, title, grade, nodes, qualifier, fx-blur, fx-glow, fx-vignette, fx-sharpen, scopes, fusion, fairlight, deliver, each as `-cl` and `-gl`; plus opencl-media, ui | OpenCL, OpenGL |
| `dsr` | title, courtyard, combat, menus | D3D11 |
| `hl2` | trainstation (D3D9); more scenes and `hl2-vulkan` once HL2 is installed in the builder's VM | D3D9 (Vulkan planned) |
| `re4demo` | title | D3D12 (GFXReconstruct) |

The store holds the traces, their references and the pinned capture and
replay tools. It is about 13 GB, so it is kept outside git in
`~/.local/share/lsw-gpu-test/traces/` (override with
`LSW_GPU_TEST_TRACE_STORE`). The builder's copy is the master:

```sh
scripts/gpu-test/traces.py fetch-tools          # the pinned apitrace / GFXReconstruct / CLIntercept builds
scripts/gpu-test/traces.py check                # every manifest, file and reference is consistent
scripts/gpu-test/gpu-test.sh traces-sync        # mirror the store to every host (rsync --delete)
scripts/gpu-test/gpu-test.sh run -- --suites traces --traces affinity
```

`traces-sync` mirrors the builder with `--delete`. A capture made on another
host must be pulled to the builder first (`gpu-test.sh traces-pull HOST
APP/FUNC`), or the next sync deletes it.

Recording a new feature happens in a running VM on the builder (TRACES.md,
"Recording a functionality"):

```sh
python3 scripts/gpu-test/gpu_matrix.py hold --gpu 0     # boot the VM on GPU 0 and keep it (Ctrl-C releases it)
scripts/gpu-test/traces.py capture APP/FUNC --tool apitrace ...
scripts/gpu-test/traces.py pull APP/FUNC
scripts/gpu-test/traces.py replay APP/FUNC --out DIR
scripts/gpu-test/traces.py promote APP/FUNC DIR         # the replay's frames become the references
```

The per-app drive scripts in `scripts/gpu-test/affinity/`, `dsr/` and
`re4demo/`, and `resolve_captures.py`, record each feature the same way every
time. Their `how` is written into each trace's manifest.

## Results

Each run writes to `~/.local/share/lsw-gpu-test/runs/RUN/` on each host:
`run.log`, `status.json`, `summary.json`, `failures.md`, and one directory per
GPU with each suite's logs, pictures and reports.

- `gpu-test.sh status [RUN]` prints one line per host: its state, whether
  the run's process is alive, and the last log line.
- `gpu-test.sh collect [RUN]` copies the results into `build/gpu-test/RUN/`
  on the builder.
- `gpu-test.sh report [RUN]` prints a pass/fail matrix (suites × host/GPU),
  the measurements (WGC fps and worst gap, trace fps), the failures common
  to every GPU, and then each GPU's own failures.

`RUN` defaults to the last run started.

## When something goes wrong

- **A run was killed.** `gpu-test.sh restore [HOST...]` puts back the VM
  definition the run had changed.
- **"Domain is already active" / the VM won't start.** A VM another tool
  holds, or one that is still shutting down. Close lsw-manager, stop any
  `gpu_matrix.py hold`, and wait for `virsh -c qemu:///session list --all`
  to show `shut off`.
- **Binaries refuse to run on a host.** The builder is x86-64-v4 and accel is
  v3 only. `deploy` strips the ISA note; build nothing with
  `-march=native`.
- **Commands over ssh misbehave.** The test hosts' login shell is fish. The
  scripts send everything through `bash -s`; do the same for manual
  commands (`ssh host 'bash -c "..."'`).
- **A host runs out of disk.** Runs and the trace store live under the
  host's home. `traces-sync` prints each host's free space. Keep the store
  well under a host's free space (accel has about 12 GB).
- **The WGC suite fails on fps.** Look at the CPU load (another run or build
  on the same host) and the power profile first.

## Files

| File | What it is |
|---|---|
| `gpu-test.sh` | the builder's entry point: build, deploy, run, status, wait, collect, report, restore, provision, setup-keys, traces-sync, traces-pull |
| `hosts` | the test machines |
| `gpu_matrix.py` | runs on each host: preflight, gpus, hold, run, restore |
| `app_suites.py` | the wgc, affinity, resolve, hl2 and dsr suites |
| `aggregate.py` | the report: matrix, measurements, failures |
| `qga_bridge.py`, `lsw_link.py` | drive the guest through LSW's agent and the C$ share |
| `desk.py`, `guest/desk-server.ps1` | an elevated helper in the guest desktop: input, screenshots, app console commands |
| `traces.py`, `trace_store.py`, `trace_suite.py`, `trace_judge.py`, `trace_mend.py`, `cl_sources.py`, `trace_tools.json` | the trace store, its pinned tools, recording, replay and judging |
| `guest/` | what runs in the guest: trace capture/replay, the OpenCL replayer, screenshots, input, Resolve's Lua scripts and grades |
| `affinity/`, `dsr/`, `re4demo/`, `resolve_captures.py`, `resolve-media.sh` | the scripted feature recordings |
| `TRACES.md` | the trace suites in full |
| `test_traces.py`, `test_judge_summary.py` | the harness's own tests: `python3 -m unittest test_traces test_judge_summary` from `scripts/gpu-test` |
