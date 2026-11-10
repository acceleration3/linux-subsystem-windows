# GPU testing

How to build, deploy and test the accel-virt driver stack on the test GPUs. Read all of it before touching a VM.

## Machines and GPUs

| Host | Reach | GPUs | Notes |
|---|---|---|---|
| `local` | this machine, domain `lsw` (`virsh -c qemu:///session`) | RX 9070 XT (adapter 0, drives the desktop), Radeon 610M (adapter 1, `display=off`, hybrid discrete) | the user's own machine |
| `accel` | `accel@192.168.1.15` | Iris Xe (adapter 0, display, hybrid integrated), RTX 3070 Ti Laptop (adapter 1, `display=off`, hybrid discrete) | 16 GB RAM, x86-64-v3, fish login shell: run remote commands through `bash -s` |

Hosts are listed in `scripts/gpu-test/hosts`. Remote hosts need an ssh master on `/tmp/lsw-gpu-test-%C`; it needs the
password, so only the user can open it:

```
ssh -o ControlMaster=yes -o "ControlPath=/tmp/lsw-gpu-test-%C" -o ControlPersist=yes -fN accel@192.168.1.15
```

`scripts/gpu-test/gpu-test.sh master accel` checks whether it is open.

## Safety rules

- No guest behaviour may hang or crash the host. A host GPU fault, ring reset or QEMU crash is a driver bug to fix, never
  a row to skip.
- Never try a risky GPU feature on the RX 9070 XT first: it drives the user's desktop. Prove it on the 610M (or accel).
- Watch the kernel log during every run: `journalctl -k -f | grep -iE 'amdgpu.*(fault|timeout|reset)'` locally,
  `journalctl -k | grep -iE 'xid|i915|gpu hang|fence expiration'` on accel. Stop at the first event.
- Rows that hang a GPU on purpose stay in the hold file and are never run until their fix is proven.
- Builds use at most 4 CPUs: `systemd-run --user --scope -p CPUQuota=400% -p MemoryHigh=6G nice -n 19 ionice -c3`, `-j4`,
  one build at a time on `${XDG_RUNTIME_DIR}/lsw-build.lock`.
- Keep the local VM off when it is not needed (`scripts/render-vm.sh down`). Do not run the full suites while the user is
  working on this machine unless asked.
- Never redefine a domain by hand. `gpu-test.sh stack-apply` (through `retarget`) is the only path that keeps the
  `hybrid=` roles.
- Never commit build output or the `third_party/` symlinks of a driver worktree; gitlinks stay mode 160000.

## Fixing versus testing

Fix in batches: code plus unit tests (`ctest` in the driver build), landed on the branch. Run the guest suites once,
after the batch, with `verify.sh`. Do not run a suite after every fix. A live probe of one row is fine when the cause
cannot be found from the code, the logs or a host-side harness (the full-stack tests in `tests/harness` run the guest
ICD against the real executor in one process, no VM needed; `AV_TEST_HOST_GPU` picks the GPU).

## Running the suites: `verify.sh`

Every guest run goes through `scripts/verify.sh`. It is the only way that guarantees the VM runs the stack you think it
runs, with suites and references of the same revision.

1. Build a stack from a clean, committed driver worktree (`external/accel-virt` branch or a worktree of it):

   ```
   scripts/stack-build.sh <driver worktree> <stack dir>
   ```

   It builds the executor (x86-64-v3, runs on both hosts), the guest drivers (x64 and x32), the D3D runtime (DXVK,
   vkd3d-proton with `tools/*-patches`) and the hybrid-capable QEMU modules, and writes `manifest.json` (driver sha,
   suite sha, md5 of every file). It refuses a dirty worktree unless `LSW_STACK_ALLOW_DIRTY=1`.

2. Run from an LSW tree whose HEAD is the manifest's `suite_sha`:

   ```
   scripts/verify.sh --host local|accel --stack <stack dir> --filter 'render.d3d11.clear*,render.vk.xfer.copy*' \
       [--suite render|sanity] [--adapters all|first] [--gpus SEL] [--bits 64|32|both] [--jobs N] [--max-hold SECONDS]
   ```

   It builds the suites and `lsw-render-ref` at the tree's revision, waits its turn in a first-come queue for that VM,
   deploys exactly the stack (local: the dev stack in `~/.local/share/lsw-dev/render-driver` and `redeploy.sh`; accel:
   `deploy-render` then `stack-apply`), checks the md5 of the executor and QEMU modules that QEMU really maps against
   the manifest (no rows run on a mismatch), runs the rows and writes `<stack dir>/results/<timestamp>-<host>/`.

   `--queue` shows the queue, `--dry-run` prints every step and touches nothing, `--redeploy` forces a deploy. A run is
   killed after `--max-hold` (default 3600 s): keep filters narrow.

Never run `render-run.sh --hosts accel` without `--parallel`: that path does a full `gpu-test.sh deploy` and replaces
the stack on accel.

## Locks

| Lock | Guards | Taken by |
|---|---|---|
| `${XDG_RUNTIME_DIR}/lsw-build.lock` | builds on this machine (one at a time) | `stack-build.sh`, `verify.sh` suite builds, any manual build |
| `${XDG_RUNTIME_DIR}/lsw-verify-queue/<host>` | first-come order for a VM | `verify.sh` (`scripts/vm_queue.py`) |
| `${XDG_RUNTIME_DIR}/lsw-render.lock` | the local VM: deploy, run, fetch | `verify.sh` while it holds the queue |
| `/tmp/lsw-accel-vm.lock` on accel | the accel VM | `verify.sh` while it holds the queue |

Hold a VM for the whole deploy, run and fetch of one verification, never per step and never with a placeholder
(`sleep N`). A run is valid only if nothing else deployed to, restarted or ran on that VM during it.

## The suites

`lsw-suites64.exe` / `lsw-suites32.exe` (built by `scripts/build-suites.sh`) run in the guest. `--suite render` is the
rendering suite (`suites/render/{d3d9,d3d10,d3d11,d3d12,vk,gl}`), `--suite sanity` the API and system contract suite.
Useful options: `--filter`, `--adapter uuid:HEX`, `--list-adapters`, `--jobs`, `--refs`, `--dump-captures`,
`--device-under-test warp` (self-validation against WARP), `--exclude-file`, `--json`.

Oracles: WARP for D3D10/11/12, D3D9On12 over WARP12 for D3D9, host references for Vulkan and GL made by
`lsw-render-ref` (`suites/render-ref`, native Linux, built by `scripts/build-render-ref.sh`), and analytic or CPU
references for rows that need them. Suites and references carry a `REVISION`; `render-run.sh` refuses a mismatch.

The hold file (one check id per line, globs allowed) lists rows never run, e.g. rows that hang a GPU on purpose. Pass it
with `--hold-file`.

Hybrid graphics: each adapter has a role (`hybrid=integrated|discrete` on the QEMU device). D3D9 and GL reach the
display-less adapter through the Windows GPU preference; the suite re-runs itself under the matching preference.

## Results and reports

- `scripts/render-report.py --tree <results dir> --out report.html`: one HTML page with every row of every GPU,
  side-by-side, swipe, onion and difference views. `--baseline-tree` compares with an earlier run.
- `scripts/render-audit.py --guest <artifacts> --host <refs> --json <result.json> --strict`: checks that each row
  tests what it says (swap test, identical captures across families, the Python judge).
- `render-progress` (in `~/.local/bin`) shows a running guest run's progress per GPU, local and remote.
- Sanity results land per bitness under `sanity/<host>/<gpu>/x64|x32/result.json`.

## Other tools

| Tool | Use |
|---|---|
| `scripts/render-vm.sh up\|down\|done\|status\|idle-watch` | start/stop the local VM; `idle-watch` stops a VM it started after `LSW_RENDER_VM_IDLE` seconds idle |
| `scripts/gpu-test/gpu-test.sh stack-status\|stack-restore <host>` | what stack a remote host runs; put its original stack back |
| `scripts/gpu-test/gpu-test.sh gpus <host>` | list a host's GPUs |
| avinspector MCP | live state of a running stack (stalls, connections, surfaces, threads); check it before reading logs |

## Where to look when something breaks

- QEMU log: `~/.cache/libvirt/qemu/log/lsw.log` (both hosts).
- Core dumps: `coredumpctl list | grep qemu`; a stack through a Vulkan layer means a host implicit layer got in.
- Guest driver logs: `C:\` of the guest over SMB on port 5445 (`smbclient //127.0.0.1/C$ -p 5445 -U lsw%lsw`; on accel
  run it there).
- A VM that died mid-run: `render-run.sh` keeps crash evidence under the run's `evidence/` and resumes in a new segment.
