---
name: lsw-deploy
description: Hot-swap the LSW guest agent (lsw-guest-agent.exe) running inside the Windows VM with a fresh cross-build, without rebooting the VM or running the wizard. Use after touching anything under src/agent/ — the manager UI keeps its existing target, MCP keeps the same port, but the guest-side process restarts under the new binary.
---

# Hot-deploy the LSW guest agent

The agent autostarts via `HKLM\...\Run` and listens on TCP `:5102` inside the guest (forwarded from host `:5102` via QEMU user-mode hostfwd). Without this skill, replacing the binary means: shut down VM → wizard reinstall (slow!) OR scp+restart inside guest (no SSH yet). With this skill: a single tool call.

## Mechanism

1. Manager reads `lsw-guest-agent.exe` from `$LSW_AGENT_EXE` or `<repo>/build-agent/src/agent/lsw-guest-agent.exe`.
2. Spins up a one-shot HTTP server on a free localhost port (guest reaches it at `10.0.2.2:<port>`).
3. Sends a swap script via the `run_script` RPC: `curl` → `taskkill /F /IM lsw-agent.exe` → `move /Y` → `start ""`. The script runs in a detached cmd.exe so killing the agent doesn't kill it.
4. Polls `system_info` for ~30 s; success when the `agent_version` build marker changes (each compile bakes `__DATE__ " " __TIME__` into the binary).
5. Tears the HTTP server down.

## Two entry points, same machinery

### From an MCP client (Claude, Claude Desktop, anything speaking the 2024-11-05 protocol):
```
redeploy_agent     # no args
```
Returns a one-line success message like `agent_version 'May 13 2026 21:29:48' -> 'May 13 2026 21:55:02' (1 download served on port 41234)` or a structured failure (which always points at `%TEMP%\lsw-swap.log` on the guest for diagnostic detail).

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
❌ The agent is unreachable — the swap mechanism needs to send `run_script` to the live agent. If the agent's wedged, RDP/VNC into the guest and copy the file manually (or `virsh reset` + redo wizard).

## Diagnosing a failed swap

The `redeploy_agent` reply includes the download count served and the previous `agent_version`. If it timed out:

1. **0 downloads served**: the guest never reached the host's HTTP server. Either curl failed (network), or `taskkill` killed the agent before the request landed. Check `%TEMP%\lsw-swap.log` via `lsw-debug`'s console session.
2. **1+ downloads served, no version change**: the new binary was downloaded but didn't take over. Likely the `move /Y` failed because the file was locked (Windows takes a moment to release after taskkill), or the relaunched agent crashed. The swap log will say which step failed.
3. **agent_version field empty in pre-swap**: the running agent predates this field. The swap will still work — the post-swap check just needs *any* non-empty `agent_version` to consider it changed.
