---
name: lsw-deploy
description: Hot-swap the LSW guest agent (lsw-guest-agent.exe) running inside the Windows VM with a fresh cross-build, without rebooting the VM or running the wizard. Use after touching anything under src/agent/ — the manager UI keeps its existing target, MCP keeps the same port, but the guest-side process restarts under the new binary.
---

# Hot-deploy the LSW guest agent

The agent is the `lsw-agent` Windows service (LocalSystem, session 0, auto start, restarted by the SCM on failure) and listens on TCP `:5102` inside the guest (forwarded from host `:5102` via QEMU user-mode hostfwd). It keeps a helper process (`lsw-agent.exe --session`) in each logged-on user's session for windows, UI Automation and HKCU work; the deployed file is `C:\ProgramData\LSW\lsw-agent.exe` for both. Without this skill, replacing the binary means: shut down VM → wizard reinstall (slow!) OR scp+restart inside guest (no SSH yet). With this skill: a single tool call.

## Mechanism

1. Manager reads `lsw-guest-agent.exe` from `$LSW_AGENT_EXE` or `<repo>/build-agent/src/agent/lsw-guest-agent.exe`.
2. Spins up a one-shot HTTP server on a free localhost port (guest reaches it at `10.0.2.2:<port>`).
3. Sends a swap script via the `run_script` RPC (runs as SYSTEM in session 0): `curl` → `sc stop lsw-agent` (waits for STOPPED) → `taskkill /F /IM lsw-agent.exe` (session helpers / stragglers still mapping the old image) → `move /Y` → `sc start lsw-agent`. The script survives the service stopping: run_script's job object is not kill-on-close and the SCM does not end a service's children. It assumes the `lsw-agent` service exists.
4. Polls `system_info` for ~45 s; success when the `agent_version` build marker changes (a stamp regenerated on every build). The new service only opens `:5102` once its session helper has confirmed a usable desktop (a few seconds when someone is logged on).
5. Tears the HTTP server down.

## Two entry points, same machinery

### From an MCP client (Claude, Claude Desktop, anything speaking the 2024-11-05 protocol):
```
redeploy_agent     # no args
```
Returns a one-line success message like `agent_version 'May 13 2026 21:29:48' -> 'May 13 2026 21:55:02' (1 download served on port 41234)` or a structured failure (which always points at `C:\ProgramData\LSW\lsw-swap.log` on the guest for diagnostic detail).

### From a shell:
```sh
scripts/redeploy-agent.sh             # rebuild agent, then swap
scripts/redeploy-agent.sh --no-build   # use existing build-agent/ output
```
The script is a thin wrapper that POSTs `tools/call name=redeploy_agent` to `http://127.0.0.1:5105/` (override with `$LSW_MCP_URL`).

## Prereqs

- A running `lsw-manager` (the MCP server is hosted there — without it, neither entry point works).
- A running VM with the current agent reachable. `system_info` should answer before you start; if it errors, the swap can't be initiated because the manager kicks the script via the agent's own `run_script` RPC.
- `mingw-w64` toolchain on the host for `scripts/build-agent.sh` (only needed if rebuilding).
- Windows guest must have `curl.exe` on PATH (default on Windows 10 1803+ and all Win11).

## When to use vs. not

✅ Picked up a bug in [src/agent/](src/agent/) or want to test an experimental RPC — fastest iteration loop.
✅ Iterating on `system_info`, `screenshot`, `console_*`, installer steps — the swap takes ~10 s end to end.
❌ Renaming/removing an RPC the manager UI calls frequently — the manager has cached state from the old agent; safer to restart the manager too.
❌ The agent is unreachable — the swap mechanism needs to send `run_script` to the live agent. If the agent's wedged, VNC into the guest, `sc stop lsw-agent`, copy the file over `C:\ProgramData\LSW\lsw-agent.exe`, `sc start lsw-agent` (or `virsh reset` + redo wizard).

## Diagnosing a failed swap

The `redeploy_agent` reply includes the download count served and the previous `agent_version`. If it timed out:

1. **0 downloads served**: the guest never reached the host's HTTP server (curl failed — network). Check `C:\ProgramData\LSW\lsw-swap.log` via `lsw-debug`'s console session (`type C:\ProgramData\LSW\lsw-swap.log`).
2. **1+ downloads served, no version change**: the new binary was downloaded but didn't take over. Either the `move /Y` failed (something still mapped the old image — the log shows the retry), the service did not start (`sc query lsw-agent`, and the System event log's Service Control Manager entries), or it started but is still holding `:5102` closed at its desktop gate (nobody logged on, or the helper could not start — `agent.log` says `sessions: still waiting for a usable desktop`). If the agent is unreachable afterwards, VNC into the guest and run `sc start lsw-agent` / read `C:\ProgramData\LSW\agent.log`.
