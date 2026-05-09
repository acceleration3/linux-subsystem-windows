# Transport: vsock / HV_SOCKET

The link between the Windows guest and the Linux host. Vsock is right because:

- It's a virtual socket that doesn't traverse the guest's TCP/IP stack — no firewalls, no DNS, no IP routing.
- It's bidirectional and stream-oriented (we use SOCK_STREAM).
- Linux has it natively (`AF_VSOCK`), and Windows exposes the same wire primitive as Hyper-V Sockets (`AF_HYPERV` family) when QEMU's `vhost-vsock-pci` device is present.

## Linux-side: AF_VSOCK

```c
int s = socket(AF_VSOCK, SOCK_STREAM, 0);
struct sockaddr_vm addr = {
    .svm_family = AF_VSOCK,
    .svm_cid    = VMADDR_CID_ANY,    // listen on any
    .svm_port   = LSW_PORT,          // 5101 by default
};
bind(s, (struct sockaddr*)&addr, sizeof(addr));
listen(s, 16);

struct sockaddr_vm peer;
socklen_t plen = sizeof(peer);
int c = accept(s, (struct sockaddr*)&peer, &plen);
// peer.svm_cid is the guest's CID; peer.svm_port is its ephemeral port.
```

CIDs:
- `VMADDR_CID_ANY = -1` — match any.
- `VMADDR_CID_HOST = 2` — the host (referred to from a guest as the "host" CID).
- `VMADDR_CID_LOCAL = 1` — local-only loopback (handy for testing without a VM).
- Per-VM CIDs are assigned by libvirt; the manager records them in `vms.json`.

Ports: we'll use `5101` for the Vulkan render-server and `5102` for the guest-agent control plane. These are LSW-internal, no IANA registration needed.

## Windows-side: HV_SOCKET

```c
SOCKET s = WSASocketW(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW,
                     NULL, 0, WSA_FLAG_OVERLAPPED);
SOCKADDR_HV addr = {0};
addr.Family    = AF_HYPERV;
addr.VmId      = HV_GUID_PARENT;     // talk to the host
addr.ServiceId = LSW_SERVICE_GUID;   // pre-registered, see below
WSAConnect(s, (SOCKADDR*)&addr, sizeof(addr), NULL, NULL, NULL, NULL);
```

`HV_GUID_PARENT` is the well-known GUID `a42e7cda-d03f-480c-9cc2-a4de20abb878`. It means "the partition that hosts me."

### Service GUID registration

A Windows guest can only `connect` (or accept) on a Service GUID that has been registered. The LSW MSI installer writes:

```
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Virtualization\GuestCommunicationServices
└── {LSW_SERVICE_GUID}
    └── ElementName = "LSW Render Server"
```

For the guest agent control channel, a separate GUID with the same registration pattern.

### Mapping vsock ↔ HV_SOCKET

QEMU's `vhost-vsock-pci` bridges between `AF_VSOCK` (Linux) and the Hyper-V socket protocol (Windows guest). The guest sees the host as `HV_GUID_PARENT`; the host sees the guest at `peer.svm_cid = <CID>`. Ports on the Linux side (`svm_port`) become Service GUIDs on the Windows side. We don't get to use Linux port numbers as-is from the Windows side; we agree on a Service GUID per logical service.

QEMU command-line bits the manager bakes into the libvirt XML:

```
-device vhost-vsock-pci,guest-cid=42
```

Libvirt XML equivalent:

```xml
<vsock model='virtio'>
  <cid auto='no' address='42'/>
</vsock>
```

## Connection multiplexing strategy

We open multiple vsock connections rather than multiplexing inside one:

| Connection | Service | Purpose |
|---|---|---|
| 1 | LSW Render Service GUID | Per-Vulkan-instance render channel; long-lived |
| 2 | LSW Guest Agent Service GUID | Status, app launching; long-lived; shared by host manager and guest agent |
| 3 (optional) | LSW Bulk Service GUID | Large memory transfers if we split bulk from cmd |

A guest application's lifecycle = one render connection. When the app exits, the connection closes; the server tears down its mirrored state.

## Connection setup latency

Vsock connect on a healthy QEMU+vhost path is sub-millisecond. We can afford to open a new connection per Vulkan instance without batching.

## Backpressure

`SOCK_STREAM` does send-buffer backpressure for free. The replay thread paces consumption; if the GPU stalls, the read socket fills up, vhost-vsock backpressures the guest writer, the ICD's sender thread blocks. App-visible Vulkan calls keep working as long as the per-device send queue isn't full.

The ICD's sender queue is bounded (default 1024 frames, configurable). When full, calls block on a condvar. This is correct — Vulkan apps already cope with `vkQueueSubmit` blocking when the GPU is busy; they just see a slightly different blocking point.

## Heartbeat / liveness

A control-stream `PING/PONG` frame every 10 s in idle. Three missed PONGs = connection dead, tear down. Apps that legitimately go idle (paused, minimized) are fine; the heartbeat is cheap.

## Loopback for development

`LSW_LOOPBACK_TCP=127.0.0.1:5101` makes the ICD use TCP instead of vsock. Build the same machine without spinning up a VM by:

1. Run the render-server on `127.0.0.1:5101`.
2. Run a Vulkan app under Wine with the LSW ICD selected as the only `VK_ICD_FILENAMES`.
3. The ICD opens TCP to localhost; everything works as if it were vsock.

Loopback is also a great test harness — drive the server with a fuzzer that writes raw protocol frames over TCP.

## When vsock isn't available

Some QEMU builds without vhost-vsock, or odd hypervisor combinations, won't have it. The TCP loopback above can be re-pointed at a host-only virtio-net interface:

- Host runs the server bound to a host-only bridge (e.g., 192.168.121.1).
- Guest connects to that IP on 5101.

This works but loses the security boundary — anyone on the host who can reach 192.168.121.1 can talk to the server. Document the option, default to vsock.

## Performance notes

- Vsock uses guest physical memory rings (vhost-vsock); throughput on a modern host hits multiple GB/s.
- Latency is dominated by the guest's scheduling, not the ring itself.
- Avoid lots of small writes. The framing layer should always write `header + payload` in a single `writev`.
- Use `MSG_MORE` / `TCP_CORK`-equivalent semantics where available; for vsock, batching at the application layer is what matters.

## Permissions

- Linux host: opening `AF_VSOCK` requires no special privilege. The render-server binds to a port without root.
- Windows guest: any process can `WSAConnect` to an `AF_HYPERV` service that's registered. Sandboxed (AppContainer) processes need `enableHyperVRdpAccess` or equivalent capability — out of scope for v1.

## Failure to connect

The ICD on first call:
1. Try vsock.
2. If `LSW_LOOPBACK_TCP` is set, try that instead.
3. On failure: log to `%LOCALAPPDATA%\LSW\client.log`, return `VK_ERROR_INITIALIZATION_FAILED` from `vkCreateInstance`. The application falls through to other ICDs (e.g., a software ICD if installed) or fails to start.

The render-server logs every accepted connection's CID + port + handshake info, and every rejected handshake (proto mismatch, malformed HELLO).
