---
name: lsw-gpu-verify
description: Verify a driver change on the GPU test machines - build a stack from an accel-virt worktree, run guest suite rows on the local VM or a remote host through verify.sh, read the results and reports, and watch the host's kernel log for GPU faults. Use whenever a change to accel-virt or the suites has to be proven on real GPUs, for smoke gates, and for questions about test hosts or stacks.
---

# Verify on the GPUs

Everything goes through the `lsw-dev` MCP server. AGENTS.md has the full rules; these are the steps.

## 1. Build a stack

`stack_build out=<dir>` builds a clean, committed accel-virt worktree (default `external/accel-virt`) into a stack:
the executor, the guest drivers x64 and x32, the Direct3D runtime from the forks, the QEMU modules and
`manifest.json` with every file's md5. A dirty worktree is refused unless `allow_dirty`; `dry_run` prints the steps.
The stack directory is remembered: later calls default to it.

## 2. Run rows

`verify host=<local|test|accel>` with a `filter` (comma-separated check id globs, e.g.
`render.d3d11.clear*,render.vk.xfer.copy*`) or `smoke=true`. It waits its turn in the VM's queue
(`verify_queue`), deploys exactly the stack, checks the md5 of what QEMU maps against the manifest, runs the rows and
writes `<stack>/results/<timestamp>-<host>/`. One verify per host runs at a time.

- `smoke` is the fixed gate of `scripts/smoke-selection.txt`, about 240 rows from both suites, run twice; it fails on
  any failed, crashed or missing row, QEMU crash, kernel GPU event, slow row or row that degraded between passes.
- `suite` picks render (default) or sanity; `bits`, `adapters`, `gpus` and `jobs` narrow it; `max_hold` caps it
  (default 3600 s).
- A whole suite needs `full=true`, and only after the user agreed: it runs for hours and takes the machine.
- The suite tree's HEAD must be the stack's `suite_sha`; rebuild the stack after committing suite changes.

## 3. Watch the host

Run `gpu_kernel_events` (with `host` for a remote machine) during and after every run. One fault, ring reset, hang or
Xid means stop: it is a driver bug to fix, never a row to skip. Rows that hang a GPU on purpose stay in the hold file.

## 4. Read the results

- `results` lists the newest result directories with their smoke summaries.
- `report results=<dir>` writes `report.html` beside them (side-by-side, swipe, onion and difference views);
  `baseline` compares with an earlier run.
- `scripts/render-audit.py --guest <artifacts> --host <refs> --json <result.json> --strict` checks that each row
  tests what it claims.

## Remote hosts

`hosts` lists the machines of `scripts/gpu-test/hosts` and whether their ssh master is open. The user opens masters;
when one is closed, show them the command `hosts` returns. `remote action=gpus|stack-status host=<name>` shows a
host's GPUs or the stack and disk cache its VM runs; `stack-restore` puts its original stack back.

## Rules

- Never prove a risky GPU feature first on the GPU that drives the user's desktop.
- Never redefine a domain by hand; stack-apply (inside verify) keeps the hybrid roles.
- Fix in batches with unit tests, then verify once; do not run a suite after every fix. A host-side harness test
  (`av-dev` `test area=gpu`) is the faster first check.
- Keep the local VM down afterwards (`vm action=down`).
