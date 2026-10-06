# The host↔guest link

Normative description of everything the Linux manager and the Windows
guest agent must agree on. The code that implements it is
`src/linkproto/` — a header-only module compiled by **both** toolchains,
so this document and that module are the two places the contract exists,
and nowhere else. Where they disagree, the headers win and this file is
the bug.

An application's windows and its GPU work do not travel here. Windows
are carried by [remotewin](../external/remotewin), one connection per
application, and one for the whole desktop when the manager's Open
desktop has it open; GPU work by [accel-virt](../external/accel-virt), through
the `virtio-gpu-av-pci` device the VM has for a display adapter — its
rings live in guest memory and its doorbells in the device's BAR, so it
needs no port or connection of LSW's. What is left is the
control plane: asking the agent to do things, and hearing what it has
to say.

## Transport

A single TCP connection to a guest-side listener on port **5102**
(`linkproto::kAgentTcpPort`), reached from the Linux host through QEMU
user-mode networking's `hostfwd` rule in `src/host/embedded/domain.xml`.

AF_VSOCK would be the natural choice and was tried; host kernels in this
class have no working `vhost_vsock`, so TCP-over-hostfwd is the reliable
channel. Nothing above the transport depends on which it is.

## Envelope framing

    [type:u8][len:u32 big-endian][payload:len bytes]

Length prefixing (rather than newline delimiting) is what lets a payload
contain anything without escaping.

| type | name               | payload                                     |
|-----:|--------------------|---------------------------------------------|
| 1    | `Control`          | one JSON object, UTF-8, no trailing newline |

Big-endian because that is still the network byte
order convention and it keeps packet dumps readable.

Implementation: `lsw/linkproto/envelope.hpp`, `byte_order.hpp`, and
`frame_reader.hpp` (the stream reassembler both ends run).

## Control messages

    Request  {"id": N, "cmd": "<command>", "params": { ... }}
    Reply    {"id": N, "result": { ... }}
             {"id": N, "error":  "<message>"}
    Event    {"event": "<topic>", ...payload}

Ids are sender-allocated per direction. Concurrent in-flight requests are
fine; replies route back by id. The manager gives up on a request after
`AgentRpcClient::kDefaultCallTimeout` and discards a late reply.

The agent defines every command; the manager sends them. There are no
agent-initiated requests today — both sides drop what they do not expect.

### Failure

A request fails in one of two ways, and a peer must treat both as
failure:

* an `error` reply — the command is unknown, or its handler threw;
* a `result` that says so itself, with `"ok": false` and/or a non-empty
  `"error"` string. A handler answers this way when the request was
  well-formed but the guest refused it (an executable that would not
  start, a folder Windows would not open), and keeps any other fields it
  has — a refused `list_dir` still names its `path`.

On the manager both forms, and every transport failure, arrive as one
`link::AgentReply` with `ok == false` and a sentence in `error`.

### Commands

The vocabulary lives in `lsw/linkproto/commands.hpp` as constants that
both sides name, so a rename is a compile error rather than a runtime
`unknown command`. Grouped as: system, installed apps, browsing the
guest, display, the window mirror, windows, install/provisioning,
console, UI Automation.

Each command is one agent-side handler TU under
`src/agent/src/rpc/handlers/<group>/<name>.cpp`, self-registering via
`LSW_REGISTER_RPC`. The transport does one registry lookup and knows no
command names at all — including for logging, which asks the handler
(`RpcHandler::silent`, `RpcHandler::summarize`).

#### Opening an application

`launch` takes `app_id` (an executable or shortcut path), optional `args`
and `env`, and optional `reuse_existing` (default `false`, preserving
explicit new-instance launches). With `reuse_existing: true`, a bare
executable launch may return an existing live process with the same full
image path and elevation in the requested session, preferring the oldest
process over same-image renderer/helper processes. Shortcut targets are resolved first.
Reuse skips execution only when that process or its descendants have a visible
titled window. A tray-only app runs its normal activation command, but returns
the original PID rather than the short-lived handoff process.
Arguments, including those carried by a shortcut, and environment
overrides prevent guest-side reuse: they cannot be silently applied to an
existing process. A successful reply carries `pid`, `session`, `elevated`,
and, when tracked, `job` naming the launch group that owns that process.
The job's root need not be the returned process id.

LSW's UI shares pending/running launches between entry points. Opening an
already mirrored bare app sends another reuse-enabled `launch` RPC to restore
tray-hidden UI, then raises the same mirror. Configs with arguments or environment
overrides only raise their existing mirror. A successful
RPC only means the Windows process exists; the UI reports success after
RemoteWin receives its first frame. Mirror failure does not terminate the
Windows application. `start_window_mirror` requires a listening port in
`1..65535`; invalid ports are refused rather than truncated to 16 bits.

`launch` also accepts MSI/MSP installers, batch files and Windows scripts.
These start a tracked runner in the requested interactive session:
`msiexec /i` (MSI), `msiexec /p` (MSP), `cmd /d /s /c` (BAT/CMD),
`powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File` (PS1), or
`wscript` (VBS/JS/WSF). Installer UI is not suppressed. Arguments follow
the file argument, and the working directory remains the file's folder.

MSIX/MSIXBundle and APPX/APPXBundle use `open_package` instead of `launch`.
It takes `path` and optional `session`, runs in that user's session helper,
and uses `ShellExecuteExW` with the `open` verb to activate the registered
Windows App Installer UI. Missing file associations/activation failures are
reported as errors; no silent Add-AppxPackage installation or signature
bypass is performed. Package activation does not accept launch arguments or
environment overrides. Shell activation has no reliable process handle: it
can return no process or a short-lived activation broker. The helper instead
waits up to six seconds for the actual visible `AppInstaller.exe` window in
its session, preferring new/changed windows (especially the foreground one),
and allowing a foreground or unique reused instance after a one-second grace
period. A successful reply
is `{ok: true, pid}` with that window's nonzero owning PID. Missing or ambiguous
installer windows fail rather than falling back to a desktop mirror. The host
mirrors only that application's windows. UI success means the installer is
visible, not that the package has been installed; the user completes
installation in App Installer.

#### Store applications

Packaged (Microsoft Store, MSIX) applications have no path anybody may
run; Windows starts them by their Application User Model ID,
`<package family>!<application>`. Both commands run in the user's session
helper, because packages are installed per user and SYSTEM in session 0
sees none of them.

`list_store_apps` takes optional `icon_size` (pixels, 16 to 256, default 48),
`aumid` (answer for that one application only) and `session`. It walks the
shell's AppsFolder, the Start menu's All apps list, and keeps the items
whose ID names a package. The reply is
`{apps: [{aumid, name, family, icon_png_base64}]}`, sorted by name, each
icon drawn by Windows at `icon_size` with its transparency.

`launch_store_app` takes `aumid`, optional `args` and `session`, and
activates the application through `IApplicationActivationManager`. The
reply is `{ok: true, pid}`: the process Windows started, or for a
single-instance application already running, the existing one. The
process comes from Windows' activation broker, not the agent, so it is in
no job of the agent's and the reply carries no `job`; the host mirrors it
by `pid`. Activation takes no environment overrides, and the host refuses
to send any.

#### Uploading a local file

The manager's **Run on VM…** action uploads one local file, then calls
`launch` on the returned guest path through the shared application launcher.
The file is not executed until the entire upload has been acknowledged.
`upload_file` runs in the service and takes an `action`:

| action | parameters | successful result |
|---|---|---|
| `begin` | `name` (UTF-8 basename), `size` (unsigned byte count) | `{ok: true, upload_id}` |
| `chunk` | `upload_id`, `offset` (unsigned), `data` (strict padded RFC 4648 base64) | `{ok: true, offset}` (next byte offset) |
| `finish` | `upload_id` | `{ok: true, path}` (absolute Windows path) |
| `abort` | `upload_id` | `{ok: true}` (idempotent) |

Files must be non-empty and at most 16 GiB; chunks are at most 192 KiB
decoded (`lsw/linkproto/file_transfer.hpp`). Chunks must be sequential and
must not exceed the declared size. Finish requires exactly that size,
flushes/closes the file, and renames the partial file to its original name.
No arbitrary guest destination is accepted: each upload gets a unique
directory under `C:\ProgramData\LSW\uploads`. Unsafe Windows names
(including path separators, device names, alternate streams, `%` and `!`)
are refused. At most eight incomplete uploads are kept; ones inactive for
five minutes are removed on the next begin/chunk/finish request. Service
teardown also discards unfinished uploads. Abort never deletes a completed
file. Completed files are retained, including if launching fails, so an
installer or running program can still access its source.

The host sends only one chunk at a time asynchronously, reports acknowledged
bytes as progress, and aborts on errors or cancellation. Cancellation racing
with finish may retain the completed file but never starts it. Closing during
a pending begin waits for its ID so the partial file can be aborted. Only the
selected file is copied, not adjacent DLLs or other companion files.

### Sessions

The agent is a Windows service (`lsw-agent`, LocalSystem, session 0),
and a Windows machine can have several user sessions. Commands that act
for a user take an optional integer `session` param:

| command | default without `session` | `session: 0` |
|---|---|---|
| `launch`, `console_open`, `start_window_mirror` | the console user's session, as that user, elevated (`elevated: false` for the filtered token); `start_window_mirror` with a `pid` defaults to that process's session | SYSTEM in session 0 |
| `run_script`, `download_install`, `download_extract` | SYSTEM in session 0 | the same |
| `list_windows`, `close_window`, `window_command`, `set_display`, every `uia_*` | the console user's session | refused: session 0 has no desktop |
| `open_package` | the console user's session, using its file associations | refused: session 0 has no desktop |
| `list_store_apps`, `launch_store_app` | the console user's session, as that user (packages are per user) | refused: session 0 has no desktop |

Replies of the process-starting commands carry the `session` they ran
in (`launch` and `console_open` also `elevated`). The commands of the
last row are served by a helper process the agent keeps in each user
session; a request for a session without one fails with an error reply
("no user session N", "the session helper in session N is not running")
rather than timing out. UI Automation `sub_id`s carry their session in
their top 32 bits, so `uia_unsubscribe` needs no `session`.

`list_sessions` answers which sessions there are:
`{"sessions": [{"id", "user", "state": "active"|"connected"|"disconnected",
"console", "helper"}], "default": id|null}`. Session 0 is not listed.

### Events

Agent-pushed, one-way, delivered on the connection's tick. Topics are in
the same header (`linkproto::topic`): agent logs, install-job progress
and completion, console output and exit, and the UI Automation
notifications (raised in a session helper and forwarded unchanged).

The agent does not listen on 5102 at all until a user's desktop is
genuinely usable (not just logged on: the first-logon "Preparing
Windows" overlay is gone), or 30 minutes have passed; the manager's
post-install watcher relies on that.

Agent-side, every event source implements `rpc::EventSource` and is
registered with the connection's `EventPump`; adding a topic touches no
loop. A source may implement `undeliverable` to have events survive a
connection that dies mid-drain — the installer does, because the wizard
blocks on `setup_done`.

Manager-side, `AgentRpcClient::subscribe` returns an `EventSubscription`
that unregisters its handler when destroyed, waiting out a call already
in progress on the reader thread.
