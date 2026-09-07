# Linux Subsystem for Windows (LSW)

A "reverse WSL" — run Windows applications on a Linux host with per-application native windows, their pixels captured in the guest and their GPU work replayed on the host.

## Status

Greenfield. Phase 0 scaffolding.

## Layout

```
src/
  agent/          lsw-guest-agent.exe — runs inside the Windows VM
  base/           logging, shared by both sides
  linkproto/      the host <-> guest wire contract
  host/           the Linux manager
external/
  render-server/  the GPU proxy: OpenCL, OpenGL and Vulkan drivers + server
```

There are two paths across the VM boundary and they are independent.

**Windows Graphics Capture** carries pixels. The agent captures each
window in the guest and writes BGRA frames into an IVSHMEM region; the
manager reads them out and paints one native Linux window per guest
window. This is how every application is mirrored, whatever it draws
with.

**The render server** carries GPU work. An application's OpenCL, OpenGL
and Vulkan calls are encoded by drivers the agent links into that
application's own directory, travel over a second IVSHMEM region, and
are replayed by `rvk-render-server` against the host's real GPU. That
half lives in [render-server](external/render-server), a submodule; it
knows how to move a graphics API across a shared-memory transport and
nothing about virtual machines, so it builds and tests entirely on its
own:

```sh
git submodule update --init --recursive
```

What this repository adds is the VM around both: provisioning Windows,
running it, and presenting its applications as native Linux windows.

`src/linkproto/` is the one module the Linux manager and the Windows
guest agent both compile: envelope framing, the IVSHMEM frame-ring
layout, and the RPC command vocabulary. It is header-only and depends on
nothing but the standard library — no Qt, no spdlog, no POSIX, no Win32.
Anything either side could change unilaterally and still compile does
not belong there.

Every module has exactly one `include/` tree and one `src/` tree, so every
include in the repo reads `lsw/<module>/<concern>/<class>.hpp`. One class
per header, one header/.cpp pair per class.

`src/host/` subdivides by concern:

```
src/host/
  include/lsw/host/<concern>/*.hpp
  src/<concern>/*.cpp

  platform/   DBus single-instance, vsock, XDG places
  config/     settings + the VM manifest
  vm/         libvirt lifecycle, stats, shared-memory transport
  link/       the multiplexed JSON channel to the guest agent
  apps/       guest app catalog, per-app config, launch, logs
  ui/         widgets shared by both UIs
  services/   HostServices — the container every feature takes
  provision/  first-run install pipeline (ISO, autounattend, setup)
  wizard/     the first-run QWizard and its pages
  manager/    the everyday window and guest-window mirrors
  mcp/        MCP automation surface over localhost HTTP
  app/        composition root + entry point
```

Those twelve concerns are linked as four libraries plus the executable —
see the header comment in `src/host/CMakeLists.txt`. Include paths never
name a target, so moving a concern between libraries touches no source.

## Building

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build
```

Requires CMake 3.25+, a C++20 compiler, Qt 6, libvirt, and — for the
render server — Vulkan, OpenGL (EGL) and OpenCL development packages on
the host. `scripts/build-agent.sh` cross-builds the Windows half with
mingw-w64.

## License

Apache-2.0. See [LICENSE](LICENSE).
