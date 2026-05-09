# Glossary

Terms and acronyms used across LSW docs.

| Term | Meaning |
|---|---|
| **AF_VSOCK** | Linux address family for VM ↔ host stream sockets. The transport on the host side |
| **AF_HYPERV** | Windows address family for Hyper-V Sockets (`HV_SOCKET`). The transport on the guest side |
| **autounattend.xml** | Windows answer file that automates an OS install with no user input |
| **bulk channel** | Optional secondary stream for large memory transfers, decoupled from the command stream |
| **CID** | Context Identifier — vsock's analogue of an IP address, naming a VM or the host |
| **client ICD** | The Windows-side Vulkan ICD DLL that intercepts and proxies Vulkan calls to the host |
| **command stream** | The ordered sequence of serialized Vulkan calls flowing client → server |
| **DXVK** | Translation layer that implements D3D9/10/11 on top of Vulkan; runs inside the Windows guest |
| **frame** | A single wire-protocol message: header + payload, optionally with a reply token |
| **generated/** | Directory of code emitted by the codegen, committed to the repo |
| **guest agent** | Windows service inside the VM that the manager talks to for status, app listing, app launching |
| **handle remap** | Translating a Vulkan handle value between client and server. Most handles are pass-through; a few must be remapped |
| **HV_GUID_PARENT** | Well-known Hyper-V Socket GUID meaning "the host that runs me" |
| **HV_SOCKET** | Hyper-V Socket. The Windows-side equivalent of vsock |
| **ICD** | Installable Client Driver — the Vulkan loader's term for a driver implementation |
| **libvirt** | The host-side library/daemon for managing QEMU/KVM VMs |
| **LSW** | Linux Subsystem for Windows — this project |
| **manager** | The host-side GUI app that provisions and controls VMs |
| **OVMF** | Open Virtual Machine Firmware — UEFI firmware for QEMU; required by modern Windows installs |
| **payload ISO** | A small ISO the manager builds containing autounattend.xml + LSW installers, attached as a second CD-ROM during install |
| **pNext chain** | Vulkan's mechanism for extending structs via a linked list of typed extension structs |
| **protocol_ids.toml** | Hand-curated map of Vulkan command names to stable wire-protocol IDs |
| **render-server** | The Linux-host process that accepts vsock connections and replays Vulkan calls on the real GPU |
| **reply token** | A 64-bit value that pairs a sync request with its reply on the wire |
| **service GUID** | Windows-side identifier for a Hyper-V Sockets endpoint, equivalent to a vsock port |
| **shared memory window** | Phase-2 optimization: a virtio-shmem region mapped into both guest physical memory and the host process |
| **SPIR-V** | Vulkan's shader bytecode format |
| **staging buffer** | Server-owned buffer used to stage uploads when shared memory is unavailable or device-local memory is the target |
| **sType** | Vulkan struct discriminator — first member of every extensible struct |
| **virtio-shmem** | QEMU device exposing a shared memory region between guest and host |
| **virtio-vsock** | QEMU device implementing vsock between guest and host |
| **VKD3D-Proton** | Translation layer that implements D3D12 on top of Vulkan; runs inside the Windows guest |
| **vk.xml** | Khronos's machine-readable description of the Vulkan API; the codegen's primary input |
| **Vulkan loader** | The DLL/.so that applications link against (`vulkan-1.dll` / `libvulkan.so`); discovers and dispatches to ICDs |
| **vsock** | Generic name for the VM ↔ host socket family. We use `AF_VSOCK` on Linux, `AF_HYPERV` on Windows |
| **WSA** | Windows Sockets API |
| **xdg-shell** | Wayland protocol for windows; the render-server uses it to create per-app windows |
