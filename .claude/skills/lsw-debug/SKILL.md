---
name: lsw-debug
description: Inspect what's happening inside the LSW Windows guest VM — pull recent agent logs, capture a screenshot of the desktop, drive cmd.exe interactively, and enumerate visible windows. Use when something feels broken in the manager UI (stats not updating, RPCs hanging, agent unreachable) or when you need to reproduce a guest-side issue without leaving the chat.
---

# LSW debug toolkit

The manager exposes an MCP server at `http://127.0.0.1:5105/`. These tools all route through it and operate on the running guest VM.

## When to use

- The manager UI shows something stale or wrong (stats, thumbnail, app list) — pull `recent_logs` and a `screenshot` to confirm what the agent sees vs. what the UI renders.
- An RPC the manager makes is hanging — `recent_logs` shows the REQ but no REP, suggesting either the agent's dispatcher threw or the manager's reader thread is stuck.
- Need to reproduce a guest-side issue interactively — open a cmd session, run commands, read output.

## Tools

### `system_info` (no args)
First call when investigating. Confirms the agent is reachable and reports OS version, hostname, IPv4, RAM total/avail, CPU%, disk total/avail, and `agent_version` (build marker). Empty reply / error = agent unreachable; treat as "VM is off or agent crashed."

### `recent_logs` (`count`: optional int, default 100)
The agent's structured log stream as the manager has accumulated it. Bounded ring buffer — old entries roll off. Look for:
- `host_endpoint: REQ id=N cmd=X` / `REP id=N` — every RPC the manager made.
- `host_endpoint: cmd=X threw: <msg>` — agent-side exception.
- `host_endpoint: peer ... EOF` / `connection accepted from` — connection churn (reconnects).
- Module-specific lines (`installer:`, `console:`, `app_catalog:`) for that subsystem's state.
- `sessions:` — the session helpers (one `lsw-agent.exe --session` per logged-on user, started by the service): started/connected/went away/relaunching, and while the install gate holds `:5102` closed, `still waiting for a usable desktop`. Lines prefixed `session N:` are that helper's own log, forwarded (its pre-connection log is `C:\ProgramData\LSW\agent-session-N.log`).
- `service:` — SCM start/stop.

### `screenshot` (no args)
Captures the user-session desktop as PNG, returned as an inline MCP image block. Good for "what is the guest actually showing right now" before diving into logs.

### `list_windows` (no args)
Top-level visible HWNDs as `{hwnd, pid, title, exe_path, client_w, client_h}`. Use to confirm a launched app actually opened a window, or to find HWND for a `close_window` RPC.

### Console session
Three-step pattern for running cmd.exe in the guest:
```
console_open                                            # → session_id (e.g. "c1")
console_input  session_id="c1"  data="cd C:\\Users\r\n"
console_read   session_id="c1"                          # returns accumulated stdout
console_close  session_id="c1"
```
Notes:
- The console runs as the logged-on user, elevated, in the console session (what the agent itself was before it became a service). `console_open session=0` gives a SYSTEM cmd in session 0 instead — e.g. to `sc query lsw-agent` or look at what install jobs see; `session=N` targets another user session.
- `data` is raw bytes — include `\r\n` to submit a line. Without it cmd.exe doesn't see the line as terminated.
- `console_read` drains the buffer (returns empty afterwards). Call again after sending input to get the next chunk.
- The agent fires `console_output` events as cmd.exe produces output; the MCP server accumulates them into a per-session ring. So `console_read` is non-blocking — if you read too early, you'll get partial output. Send input, wait ~500 ms, read.

### `rpc` (`cmd`: string, `params`: object)
Escape hatch for verbs not exposed as dedicated tools (e.g., `close_window`, `subscribe_window`, `window_command`). The reply body is whatever the agent's dispatcher returned.

## Common recipes

### "The manager UI froze — is the agent alive?"
1. `system_info` — should return immediately. If "agent unreachable", the VM/agent is down (check `virsh list` on the host).
2. If reachable: `recent_logs count=200` — look at the tail. If the last entry is several minutes old, the agent's connection to the manager is dead (reconnect loop). If recent, the agent is fine and the manager UI is stuck on the GUI thread (Qt event loop pegged).

### "I want to see the swap log after a redeploy"
```
console_open
console_input "type C:\\ProgramData\\LSW\\lsw-swap.log\r\n"
# wait ~1 s
console_read
console_close
```

### "An RPC keeps failing — what's the agent saying?"
1. `recent_logs count=100` — search for the verb name (`list_apps`, `screenshot`, etc).
2. Look for the `REQ id=N cmd=X` line and the matching `REP id=N`. If REP has `error: <msg>`, the agent's dispatcher threw — message is the C++ exception's `what()`.
3. If there's no REP, the agent crashed mid-call or the manager dropped the connection (look for `peer ... EOF`).
