---
name: lsw-control
description: Drive the LSW Windows VM — enumerate installed apps, launch a program in the user session, watch its window appear, and shut windows down. Use to script repeatable workflows ("install Foo, launch it, screenshot the first window") or to verify a build by exercising the agent's launch/list pathways from outside the manager UI.
---

# Drive the LSW guest

The agent already runs inside the guest (the `lsw-agent` service, as SYSTEM in session 0, with a helper in each logged-on user's session). These tools turn it into a remote-control surface — useful for smoke tests, demos, and one-off "do X in the VM" jobs that don't fit a console session.

## Tools

### `list_apps` (no args)
Enumerates Add/Remove Programs entries with a launchable .exe. Returns `[{id, name, exe_path}]`. The first call can be slow (~hundreds of ms) because the agent extracts icons; subsequent calls hit a warm cache.

Use `id` (an absolute path to the .exe or .lnk) as the `app_id` for `launch`.

### `launch` (`app_id`: string, `session`: optional int)
Spawns the executable as the logged-on user, elevated (their full token), in the console session — or in `session` if given; `session: 0` starts it as SYSTEM in session 0, where nobody sees it. Returns `{ok, pid, job, session, elevated, error?}`. `rpc cmd="list_sessions"` lists the sessions (`{sessions: [{id, user, state, console, helper}], default}`).
- `ok=true, pid=N`: the process started. Doesn't mean a window has appeared yet — Windows can take seconds to spin up large apps.
- `ok=false, error=...`: spawn failed; usually a missing path or the user session wasn't active.

After a successful launch, poll `list_windows` until you see a row whose `pid` matches.

### `list_windows` (no args)
Top-level visible windows in the console user's session (another session: `rpc cmd="list_windows" params={"session": N}`). Each entry: `{hwnd, pid, title, exe_path, client_w, client_h}`. `client_w/h` are the window's content rect, useful for screenshot sizing.

### `rpc cmd="close_window" params={"hwnd": <int>}`
Posts WM_CLOSE to the window. The app gets a chance to refuse (e.g., "save changes?" dialogs); for force-close use `taskkill` via a console session.

### `screenshot` (no args)
Captures the full desktop. The MCP returns it as an inline image block — readable directly by an LLM client without saving to disk.

## Common recipes

### "Launch Notepad and screenshot its window"
```
list_apps
# Find {"name": "Notepad", "exe_path": "C:\\Windows\\System32\\notepad.exe"}
launch app_id="C:\\Windows\\System32\\notepad.exe"
# wait ~1 s for the window to render
list_windows
# Find the row with pid matching the launch reply
screenshot
```

### "Verify an installer ran cleanly"
The agent's `download_install` / `download_extract` / `run_script` RPCs return a `job_id` and push `setup_progress` / `setup_done` events. From an MCP client these events don't surface — but you can confirm the end-state:
1. Fire the install via `rpc cmd="download_install" params={...}`.
2. Poll `list_apps` until the new program appears (the agent re-enumerates each call).
3. Optionally `launch` the new app_id and `screenshot` to confirm it runs.

### "Close every window from a specific exe"
```
list_windows
# Filter results by exe_path
# For each matching hwnd:
rpc cmd="close_window" params={"hwnd": <hwnd>}
```

## Caveats

- **Interactive session required**: `launch` spawns in a user session. With nobody logged in it fails ("no user is logged on") unless you pass `session: 0`; at a lock screen processes start but you won't see their windows. Window and UI Automation RPCs fail with "the session helper in session N is not running" if that session's helper is down — the service relaunches it within seconds (`list_sessions` shows `helper`).
- **Per-call cost**: `screenshot` returns ~250 KB of base64 PNG by default (1280×800 desktop). Cheap to call once, expensive in a tight loop — prefer subscribing to window mirrors via the agent's `subscribe_window` RPC if you need a stream.
- **HWNDs are unstable**: window handles change every time the app is launched. Re-fetch via `list_windows` before each `close_window` / window-targeted RPC.
