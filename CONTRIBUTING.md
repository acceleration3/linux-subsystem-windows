# Contributing

## Development setup

```sh
git clone <repo> linux-subsystem-windows
cd linux-subsystem-windows
git submodule update --init --recursive
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Default options:

- `LSW_BUILD_MANAGER` (Linux only)
- `LSW_BUILD_AGENT` (Windows only)
- `LSW_BUILD_ACCEL_VIRT` — the GPU (Linux host build only): accel-virt's
  patched QEMU, executor and host Direct3D, and its guest driver package,
  installed to `build/av-drivers`. Needs the clang/LLVM toolchain; see
  [README.md](README.md#building)
- `LSW_BUILD_REMOTEWIN` — the window mirror on Linux, its guest half on
  the mingw cross-build. The host half needs Wayland's client library,
  scanner and protocols (pkg-config), a C compiler and Qt's private Gui
  and WaylandClient modules; configure stops with the list of what is
  missing (README.md). remotewin's conformance harness and probe
  (`remotewin-conform`, `remotewin-probe`) are never in `all` and never
  installed: build them by name, or with `scripts/remotewin-conform.sh`
- `LSW_BUILD_TESTS`

Disable any with `-DLSW_BUILD_X=OFF`. `-DAV_WINDOWS_KIT_DIR=<dir>`
points accel-virt at a Windows kit fetched elsewhere instead of fetching
one into the submodule.

The accel-virt targets, each part of `all` in the host build:
`accel_virt_fetch` (what it pins and does not commit), `accel_virt_drivers`
(builds and installs the driver package, then adds the OpenCL loaders),
`accel_virt_host` (QEMU and `libav_executor.so`) and
`accel_virt_d3d` (DXVK and vkd3d-proton); `accel_virt` is all four.
`cmake/LSWAccelVirt.cmake` says where each output lands.

The Windows half is cross-built, not built here:

```sh
./scripts/build-agent.sh
```

That produces `lsw-guest-agent.exe` and `remotewin-guest.exe` under
`build-agent/`, and the Vulkan loader for x86-64 and i686 under
`build-agent/` and `build-agent32/`. The wizard picks all of them up from
there. The GPU drivers are not part of it: they are built by the host
build, with accel-virt's own toolchains.

## Installing and releasing

Every build tree installs its part with `cmake --install <tree>
--component lsw` (the component keeps out what the fetched Vulkan-Loader
would install of its own). Installed to one prefix, the host tree and
the two cross trees make a working install: `bin/lsw-manager`, the
private binaries and accel-virt's host half under `lib/lsw`, the guest
files under `share/lsw/guest`. The manager finds each artifact through
`platform::locate_build_artifact`: an environment override first, then
the build tree beside its binary, then the installed tree beside its
binary, so a staged `DESTDIR` works as it stands. It logs where it
found each one at startup (`artifact:` lines).

`scripts/package-release.sh` builds all three trees from scratch in
`build-release/` and packs that install as
`dist/lsw-<version>-x86_64.tar.zst`, which the AUR package in `aur/`
(`lsw-bin`) installs; `aur/README.md` has the release procedure.

## Changing the GPU

The kernel driver, the user-mode drivers, the QEMU device and the
executor all live in the [accel-virt](external/accel-virt) submodule,
which has its own build, its own tests and its own contributing rules
(see its `CLAUDE.md`). Change it there and bump the submodule pointer
here; the codegen that produces its Vulkan, OpenGL, OpenCL and Direct3D
wire format is its own too. Do not edit it in place from this
repository — the build here only drives it, through its `drivers`
target and install component and its `AV_QEMU` configuration.

What LSW owns is everything around it: which host GPU a VM's
`virtio-gpu-av-pci` device binds, the domain XML that puts the device
and the patched QEMU in the VM, staging `build/av-drivers` onto the
payload ISO, and running `provision.ps1` in the guest.

## Style

- C++: `.clang-format` and `.clang-tidy` at repo root. Run `clang-format -i` before committing.
- Python: `ruff check` in whichever tree the code lives in.
- Commit messages: imperative mood, ~70 char subject.

## Changing the host↔guest link

Anything the Linux manager and the Windows guest agent must agree on —
the envelope framing and the RPC command names and event topics —
lives in `src/linkproto/`, which BOTH toolchains compile, and is
specified in [docs/link-protocol.md](docs/link-protocol.md). Change it
there, once; do not restate a constant on either side. The framing is
covered by `lsw_linkproto_test`, so a divergence is a failed test
rather than a garbled frame at runtime.

Adding a guest RPC: one new TU under
`src/agent/src/rpc/handlers/<group>/`, its command name added to
`lsw/linkproto/commands.hpp`, and the TU listed in
`src/agent/CMakeLists.txt`. The transport is not involved — including
for log verbosity and reply summaries, which are handler overrides
(`RpcHandler::silent`, `RpcHandler::summarize`), and for where it runs:
the agent is a LocalSystem service in session 0, and a command that has
to run inside the user's session (windows, UI Automation, HKCU) says so
with `RpcHandler::placement()`; the service then forwards it to its
session helper there.

Adding an agent-pushed event: implement `rpc::EventSource` and register
it with the connection's `EventPump` in `host_endpoint.cpp`.

## Where to find what

- [README.md](README.md#layout) — the source tree, one line per module.
- Each module's `CMakeLists.txt` header comment states what belongs in it
  and what does not.
- [docs/link-protocol.md](docs/link-protocol.md) — the normative
  host↔guest spec.
