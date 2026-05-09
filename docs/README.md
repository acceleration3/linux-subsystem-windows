# Linux Subsystem for Windows (LSW)

A "reverse WSL" — run Windows applications and games on a Linux host with native-feeling integration. Windows runs inside a managed VM, and Vulkan rendering from the guest is proxied to the host so that GPU-accelerated apps (including DXVK/VKD3D-translated D3D9/10/11/12 titles) draw into real Linux windows on the host compositor with minimal overhead.

## Why this exists

The standard approach (running Windows in a VM with virtio-gpu/venus, or with GPU passthrough) trades off integration, performance, and complexity:

- **virtio-gpu venus** is the closest analogue but is tightly coupled to QEMU's virtio stack, requires Mesa-side cooperation, and traditionally exposes a single full-screen framebuffer rather than a per-application window on the host.
- **GPU passthrough** is fast but isolates the GPU from the host and forces a Looking-Glass-style framebuffer round-trip for output.
- **Software rendering** is too slow for modern apps.

LSW's approach: a transport-agnostic Vulkan command-stream proxy over `AF_VSOCK`, with a **per-application host window** model so a Windows app feels like a host-native app from the user's perspective. Translation layers (DXVK / VKD3D-Proton) inside the guest mean that D3D apps Just Work — they target Vulkan, our ICD ships their commands to the host, the host replays them on the real GPU.

## Components

| Component | Side | Language | Purpose |
|---|---|---|---|
| [render-server](render-server.md) | Linux host | C++ | Listens on vsock, deserializes Vulkan commands, replays on host GPU, manages a window per guest app |
| [client-icd](client-icd.md) | Windows guest | C/C++ | A Vulkan ICD DLL that intercepts every Vulkan call, serializes it, and sends it over vsock |
| [vm-manager](vm-manager.md) | Linux host | C++/Rust + Qt or GTK4/Adwaita | Downloads Windows ISO, automated install, manages running VMs, launches apps |
| [serialization-codegen](serialization-codegen.md) | Build-time tool | Python or C++ | Parses `vk.xml`, emits serialize/deserialize/dispatch code for both sides |
| [protocol](protocol.md) | Both | spec | Wire format, framing, handle remapping, presentation flow |
| [transport](transport.md) | Both | spec | `AF_VSOCK` connection model, multi-stream, backpressure |

## Documents

- [architecture.md](architecture.md) — system architecture, data flow, threading, lifetimes
- [render-server.md](render-server.md) — host-side renderer
- [client-icd.md](client-icd.md) — guest-side Vulkan ICD
- [vm-manager.md](vm-manager.md) — VM provisioning, automated install, manager UI
- [serialization-codegen.md](serialization-codegen.md) — `vk.xml` parser and code generator
- [protocol.md](protocol.md) — wire protocol
- [transport.md](transport.md) — vsock transport
- [roadmap.md](roadmap.md) — phased implementation plan
- [glossary.md](glossary.md) — terms and acronyms

## High-level data flow

```
 +-----------------------------------+        +-----------------------------------+
 |   Windows guest VM                |        |   Linux host                      |
 |                                   |        |                                   |
 |  D3D12 game                       |        |   render-server                   |
 |     |                             |        |     |                             |
 |  VKD3D-Proton (D3D12 -> Vulkan)   |        |     +-- Wayland/X11 surface       |
 |     |                             |        |     +-- Vulkan instance/device   |
 |  Vulkan loader                    |        |     +-- handle remap tables      |
 |     |                             |        |     ^                             |
 |  LSW client ICD ----+             |        |     |                             |
 |                     |             |        |     |                             |
 |             AF_VSOCK stream  ===========> AF_VSOCK listener                    |
 |                                   |        |                                   |
 +-----------------------------------+        +-----------------------------------+
```

## Status

Greenfield. No code written yet — these docs are the spec / design intent before implementation.
