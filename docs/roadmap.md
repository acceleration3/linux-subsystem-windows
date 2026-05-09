# Roadmap

A staged plan from empty repo to "DXVK + a Windows game running on Linux through LSW." Each phase is independently demoable.

## Phase 0 — Repo scaffolding (week 1)

**Outcome**: project compiles to empty binaries on Linux and Windows, CI runs.

- Top-level CMake project, sub-projects: `server`, `client`, `manager`, `codegen`, `agent`.
- `third_party/Vulkan-Headers` submodule pinned.
- Clang-format, clang-tidy configs.
- CI: GitHub Actions matrix Linux (clang+gcc) and Windows (MSVC), build only.
- `lsw-protocol` shared static library with hand-rolled framing primitives (`Buffer`, `BufferReader`, varint).

## Phase 1 — Codegen MVP (weeks 2-3)

**Outcome**: codegen produces serialize/deserialize/dispatch C++ for a tiny subset of Vulkan, and round-trip tests pass.

- `lsw_codegen` Python package + CLI.
- Subset profile: only `vkCreateInstance`, `vkDestroyInstance`, `vkEnumeratePhysicalDevices`, `vkGetPhysicalDeviceProperties2`, `vkCreateDevice`, `vkDestroyDevice`, `vkGetDeviceQueue`. No extensions, no `pNext`.
- Snapshot tests in `codegen/tests/`.
- Generated files committed to `generated/`; CI checks regen idempotency.
- Hand-coded loopback test: client serializer → wire bytes → server deserializer → call into a fake Vulkan dispatch table → reply round-trips.

## Phase 2 — Loopback render path (weeks 4-6)

**Outcome**: a tiny Vulkan demo running on **Linux** uses the LSW client (compiled for Linux) to talk to LSW server over a Unix socket; the server runs real Vulkan and shows a window with a clear color.

This phase intentionally avoids the Windows / vsock complexity to validate the rendering and protocol design.

- Server: Wayland window backend + real Vulkan, single connection.
- Client (Linux build): minimal ICD that loads instead of Mesa, exposes `vkCreateInstance`...`vkQueuePresentKHR`.
- Codegen profile expanded to cover the "clear screen + present" subset (~40 functions).
- Linux Unix-domain socket transport (cheaper than vsock for dev).
- Concrete test: `vkcube` from Vulkan-Tools renders through LSW.

## Phase 3 — Codegen full coverage (weeks 5-8, parallel to 2)

**Outcome**: codegen handles all of Vulkan 1.3 core + a hand-picked extension set (DXVK / VKD3D requirements).

- `pNext` chains.
- Two-call enumeration idiom.
- All `<len>` expression forms.
- Unions.
- Per-platform inclusion/exclusion.
- `protocol_ids.toml` curated, every command assigned a stable ID.
- Snapshot suite has ~100 representative cases.

## Phase 4 — vsock transport + Windows ICD skeleton (weeks 8-10)

**Outcome**: a Windows VM hosting an empty Vulkan app connects to the host server over vsock; handshake completes; nothing renders yet.

- Windows ICD entry points + registry installer (.msi via WiX).
- HV_SOCKET transport on Windows.
- AF_VSOCK acceptor on Linux server.
- Service GUID registration.
- Manager-driven test: spin a VM with a known image, observe the connection in the server's log.

## Phase 5 — Render path over vsock (weeks 11-13)

**Outcome**: `vkcube`-equivalent on Windows renders into a Linux host window via LSW.

- Per-application host window via Wayland (xdg-shell).
- Win32 surface intercept.
- Swapchain + present pipeline working end-to-end.
- Memory uploads via Phase-1 staging path (no shared mem yet).
- Frame timings reported in server's log; not yet plumbed back.

## Phase 6 — VM Manager wizard (weeks 12-15, parallel to 5)

**Outcome**: a user can launch the manager, accept the EULA, click through download → install → first boot, ending with a Windows VM that has the LSW payload installed.

- Qt 6 wizard scaffolding.
- ISO downloader with resume + hash check.
- libvirt session connection, domain XML template.
- Autounattend generation.
- Payload ISO build (LSW MSI + post-install script).
- Guest agent service (Windows) with vsock heartbeat.
- "Done" page triggered when the agent says hello.

## Phase 7 — Manager UI + app launching (weeks 15-17)

**Outcome**: pressing "Launch" on a Notepad tile in the manager opens Notepad in a host window. Vulkan apps work; D3D apps still do not.

- VM list / detail views with live thumbnails.
- App catalog from guest agent.
- Stats gauges (libvirt CPU/RAM, server-reported VRAM).
- App launch RPC.
- Tray icon + single-instance.

## Phase 8 — DXVK + VKD3D-Proton integration (weeks 17-19)

**Outcome**: a real DX11 game (e.g., a small indie title) and a DX12 demo work via DXVK / VKD3D.

- Add the long tail of extensions DXVK requires (descriptor indexing, timeline semaphores, shader subgroup, robustness2, custom border color, pipeline library, dynamic rendering, etc.).
- Validate against DXVK's `_DXVK_LOG` to find missing features.
- Image layout/transition completeness.
- Pipeline cache passthrough (`vkGetPipelineCacheData` ↔ host disk cache).

## Phase 9 — Performance pass (weeks 20-22)

**Outcome**: target 60 fps on light workloads, 30 fps on a low-end discrete GPU for a mid-tier DX11 game.

- Shared memory window via virtio-shmem (Phase-2 memory path).
- `vkAcquireNextImageKHR` prefetch.
- Bulk channel split.
- Reply token batching for runs of small sync calls.
- Tracy integration; flamegraphs for replay loop hot spots.

## Phase 10 — Stability, packaging, distribution (weeks 22-26)

**Outcome**: an installable package on at least Arch, Fedora, Debian. Clean uninstall. Crash recovery.

- Flatpak or native packages for the host components.
- Code-signed Windows MSI.
- Crash dumps from server + ICD; symbol upload pipeline.
- Documentation site (this `docs/` folder rendered via mdbook).
- A "What works / what doesn't" compatibility list.

---

## Risk register

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Windows ISO download endpoints break | Medium | Medium | Always-supported manual ISO path |
| HV_SOCKET service GUID registration silently fails | Low | High | Wizard verifies after install; reinstall path |
| DXVK / VKD3D require extensions we can't easily proxy | Medium | High | Address per-extension; some may need shimming |
| Memory perf in Phase 1 too slow for real games | High | Medium | Phase 9 shared-memory path planned upfront |
| OVMF/TPM combo for Win11 trips up unattended install | Medium | Medium | Test matrix; fall back to Win10 LTSC if needed |
| `vk.xml` schema drift | Low | Low | Snapshot tests; codegen has an "unknown construct" hard error |
| Guest sandbox apps (UWP, AppContainer) can't open vsock | Medium | Low (deferred) | Out of scope v1 |

## What "done" looks like for v1

A user with a fresh Linux desktop:
1. Installs LSW from their distro's package manager.
2. Runs the manager, clicks through the wizard.
3. Waits ~20 minutes for download + install.
4. Sees a manager UI with a Win11 VM and a list of apps.
5. Clicks Notepad — Notepad opens in a host window.
6. Clicks a known DX11 game — it runs at a playable framerate.

Anything beyond that — multi-VM, GPU passthrough, audio, USB — is v2.
