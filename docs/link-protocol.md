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

### Sessions

The agent is a Windows service (`lsw-agent`, LocalSystem, session 0),
and a Windows machine can have several user sessions. Commands that act
for a user take an optional integer `session` param:

| command | default without `session` | `session: 0` |
|---|---|---|
| `launch`, `console_open`, `start_window_mirror` | the console user's session, as that user, elevated (`elevated: false` for the filtered token); `start_window_mirror` with a `pid` defaults to that process's session | SYSTEM in session 0 |
| `run_script`, `download_install`, `download_extract` | SYSTEM in session 0 | the same |
| `list_windows`, `close_window`, `window_command`, `set_display`, every `uia_*` | the console user's session | refused: session 0 has no desktop |

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
