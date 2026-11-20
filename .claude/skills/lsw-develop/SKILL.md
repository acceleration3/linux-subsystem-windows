---
name: lsw-develop
description: Build, test and deploy LSW's own components (the Qt manager, the Windows guest agent, remotewin as LSW ships it, the guest suites) with the lsw-dev MCP server, and hot-swap a new guest agent into the running VM. Use for any edit-build-test loop in this repository, after touching src/host, src/agent, src/linkproto, suites/ or scripts/, and when a swap of the guest agent fails.
---

# Develop LSW

The `lsw-dev` MCP server (`scripts/mcp/lsw_dev.py`) runs every step. Long steps are background jobs: a call
returns a job id at once, `wait_job` waits up to 55 s and returns the log tail, `job_log` greps the log, and
`cancel_job` stops the job and everything it started. Jobs outlive the server, so `list_jobs` finds them again after
a restart. Builds take `$XDG_RUNTIME_DIR/lsw-build.lock`, run at the limits `LSW_BUILD_JOBS`,
`LSW_BUILD_CPU_QUOTA` and `LSW_BUILD_MEMORY_HIGH` set, and refuse to start beside another build.

Start with `status`: the branch and changes of LSW, accel-virt and remotewin, whether the VM and the manager run,
and what holds the build lock.

## What builds where

| Component | Source | `build` target | Output |
|---|---|---|---|
| Manager | `src/host` | `manager` | `build/src/host/lsw-manager` |
| Host tests | `src/host/tests`, `src/linkproto`, `src/agent/tests` | `host-tests` | ctest in `build/` |
| Guest agent and remotewin-guest | `src/agent`, `external/remotewin/source/guest` | `agent` | `build-agent/src/agent/lsw-guest-agent.exe` |
| remotewin host | `external/remotewin/source/host` | `remotewin` | `build/remotewin/source/host/remotewin` |
| accel-virt as LSW ships it | `external/accel-virt` | `accel-virt` | `build/av-drivers`, `build/accel-virt/` |
| Guest suites | `suites/` | `suites` (`bits` 64, 32 or both) | `build-suites{,32}/lsw-suites*.exe` |
| Host references | `suites/render-ref` | `render-ref` | `build-render-ref/` |
| Everything | | `all`, or `all-without-gpu` | |

`build/` must be configured once (`all`) before the single targets work. accel-virt and remotewin have their own
servers for work inside them: `av-dev` and `rwin-dev`.

## The loops

- **Manager**: `build manager`, `test kind=host` (the CI set; `filter` takes a ctest regular expression), then run
  `LSW_LOG=info build/src/host/lsw-manager`. The manager serves its own MCP server on 127.0.0.1:5105
  (`lsw-manager` tools); `status` says whether it answers.
- **Guest agent**: `deploy_agent` builds the agent and remotewin-guest.exe and swaps them into the running VM;
  `build=false` deploys what is built. Confirm with the manager's `system_info` (`agent_version` changes).
- **Scripts**: `test kind=python` runs every `scripts/**/test_*.py`; CI does not run them, so run them after editing
  a script they cover.
- **C++ style**: `tidy` with the paths you touched.
- **The guest's desktop**: `guest_screenshot` saves a PNG and returns its path; read it to look.

## How the agent swap works

The manager serves the new `lsw-guest-agent.exe` (from `$LSW_AGENT_EXE` or `build-agent/`) on a one-shot HTTP port
the guest reaches at `10.0.2.2`, then sends a `run_script` that downloads it, stops the `lsw-agent` service, kills
stragglers, moves the file over `C:\ProgramData\LSW\lsw-agent.exe` and starts the service. It then polls
`system_info` for up to 120 s until `agent_version` changes. The new service opens `:5102` only once its session
helper sees a usable desktop.

It needs the manager running and the current agent answering. Restart the manager too after renaming or removing an
RPC the UI calls.

When the swap times out:

1. **0 downloads served**: the guest never reached the host. Read `C:\ProgramData\LSW\lsw-swap.log` through a
   manager console (`console_open`, `type C:\ProgramData\LSW\lsw-swap.log`).
2. **Downloads served, version unchanged**: the move failed (the log shows the retry), the service did not start
   (`sc query lsw-agent`, the System event log), or it waits at its desktop gate (`agent.log`: `sessions: still
   waiting for a usable desktop`, nobody logged on).
3. **Agent unreachable afterwards**: `sc start lsw-agent` from the VM's console, then read
   `C:\ProgramData\LSW\logs\agent.log`.

## Rules

- One build at a time, under the build limits: the server enforces both, so do not start builds beside it by hand.
- Keep the VM off when nothing needs it (`vm action=down`); the user's desktop runs on this machine.
- Long or heavy runs (upgrade test, full suites) are the user's call: ask first.
- Commit on the checked-out branch; no AI attribution in commits.
