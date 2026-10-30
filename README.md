# Linux Subsystem for Windows (LSW)

Run Windows applications on Linux as native desktop windows, with their graphics running on your real GPU.

LSW is WSL in reverse. It installs Windows 11 into a virtual machine and keeps it out of your way: each application gets its own windows, taskbar entry and input on your Linux desktop, instead of living inside a VM window.

> **Status:** early and moving fast. Expect rough edges, and expect things to break between releases.

## Features

- **Per-application windows.** Each Windows application appears as its own native Wayland windows, including its menus and popups.
- **GPU acceleration.** Windows sees a real display adapter named after your host GPU, and the work runs on that GPU.
- **Guided setup.** A first-run wizard installs Windows unattended, from your own ISO or a download from Microsoft.
- **Shared folders.** Any host folder can appear in Windows as a drive letter.
- **Run on VM.** Pick a local program, installer or script, and LSW copies it into Windows and runs it.
- **Desktop access.** A live preview of the Windows desktop in the manager, and a full desktop window when you need one.

### Graphics APIs

| API | Offered |
|---|---|
| Direct3D 12 | What vkd3d-proton offers on the host GPU |
| Direct3D 11 | Feature levels up to 12_1 |
| Vulkan | 1.3, with the host device's extensions |
| OpenGL | Up to 4.6, core and compatibility profiles |
| OpenCL | 3.0 |
| Direct3D 9 | No native driver yet |

All of these work in 32-bit processes too.

## Requirements

**Host**

- x86-64 Linux with KVM, where your user can open `/dev/kvm`
- A Wayland desktop. LSW places windows through KWin, and KDE Plasma is what it is tested on
- A Vulkan driver for your GPU; AMD, Intel and NVIDIA are the ones tested
- Optional: `virtiofsd` for shared folders, an OpenCL driver for OpenCL in the guest, and Mesa for OpenGL in the guest on AMD or Intel

**Guest**

- Windows 11 24H2 (build 26100) or newer, which the GPU driver's WDDM 3.2 requires
- The guest runs with test signing on, because the driver is test-signed. That means UEFI without enrolled Secure Boot keys

VMs run under your user's libvirt session (`qemu:///session`), so you don't need a system `libvirtd` or membership of the `libvirt` group.

## Install

On Arch Linux, install [`lsw-bin`](https://aur.archlinux.org/packages/lsw-bin) from the AUR:

```sh
yay -S lsw-bin
```

Other distributions can use the `lsw-<version>-x86_64.tar.zst` tarball from [GitHub releases](https://github.com/acceleration3/linux-subsystem-windows/releases). It is built against Arch's libraries, so it may need the same library versions.

## Getting started

1. Start **LSW Manager** from your application menu, or run `lsw-manager`.
2. Follow the wizard: accept the Windows licence, choose an ISO or a download, then set the VM's size and your Windows account.
3. Wait for the unattended install to finish. The wizard moves on when Windows is ready.
4. Launch applications from the manager. Their windows open on your desktop.

To install without the GUI, pass the install flags; `lsw-manager --help` lists them.

## Building

> The `accel-virt` and `remotewin` submodules are currently private, so a public clone cannot fetch them. Without access to them, use the AUR package or a release tarball.

```sh
git clone --recurse-submodules https://github.com/acceleration3/linux-subsystem-windows
cd linux-subsystem-windows
./scripts/build.sh
```

`build.sh` cross-builds the Windows guest half, then builds the manager, the window mirror and the GPU stack.

| Command | Builds |
|---|---|
| `./scripts/build.sh` | Everything |
| `./scripts/build.sh --no-gpu` | Skips `accel-virt`; quickest when working on the manager |
| `./scripts/build.sh --host-only` | Skips the mingw cross-build |

To run the host build directly with CMake:

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

Run the result with `./build/src/host/lsw-manager`.

The first GPU build fetches about 550 MB (a Windows kit subset, the Khronos registries, QEMU's source and the OpenCL loader) and compiles QEMU, DXVK and vkd3d-proton, so it takes a while. Later builds are incremental.

### Build dependencies

- **Core:** CMake 3.25+, a C++20 compiler, Qt 6, libvirt, Vulkan headers and loader
- **Window mirror:** a C compiler, pkg-config, `wayland-client`, `wayland-scanner`, `wayland-protocols` 1.38+, `xkbcommon`, `libdrm` 2.4.116+, `libsystemd`
- **GPU stack:** clang, clang-cl, `lld`, `llvm-lib`, `llvm-rc`, `llvm-mt`, Python 3.11+, meson, glslang, and QEMU's build dependencies (glib2, pixman, gtk3, libepoxy, libslirp)
- **Guest half:** mingw-w64

Configure names whichever window-mirror dependency is missing. For the exact package list on Arch, see [aur/README.md](aur/README.md).

### Build options

| Option | Effect when `OFF` |
|---|---|
| `LSW_BUILD_ACCEL_VIRT` | No GPU stack, and no GPU for VMs |
| `LSW_BUILD_REMOTEWIN` | No window mirror |
| `LSW_BUILD_TESTS` | No unit tests |

If you already have a Windows kit, `-DAV_WINDOWS_KIT_DIR=<dir>` avoids downloading another.

### Tests

```sh
ctest --test-dir build --output-on-failure
```

`scripts/verify-install.sh` checks that an installed VM is up, and [docs/gpu-testing.md](docs/gpu-testing.md) covers the GPU suites.

## How it works

```
 Linux desktop                         Windows VM
┌──────────────────────┐              ┌───────────────────────────┐
│ lsw-manager          │  guest link  │ lsw-guest-agent.exe       │
│                      │◄────────────►│                           │
│ remotewin            │   windows    │ remotewin-guest.exe       │
│ (one per app)        │◄────────────►│ (beside the application)  │
│                      │              │                           │
│ QEMU with accel-virt │  GPU work    │ avgpu.sys + API drivers   │
│ runs on the host GPU │◄────────────►│ (D3D, Vulkan, GL, CL)     │
└──────────────────────┘              └───────────────────────────┘
```

- **The manager** provisions Windows, runs the VM through libvirt, and keeps LSW's files inside the guest up to date. The guest agent is a Windows service that does its work inside the VM.
- **[remotewin](external/remotewin)** carries windows. Each application gets its own `remotewin` process on the host, which owns that application's pixels, input, menus and taskbar identity.
- **[accel-virt](external/accel-virt)** carries GPU work. A WDDM kernel driver and user-mode drivers in the guest send commands through a patched QEMU device, and the host runs them on its own Vulkan, EGL and OpenCL, using DXVK and vkd3d-proton for Direct3D.

The two meet at a mirrored window's frames, which stay on the GPU instead of being copied back through the guest where the setup allows it. Otherwise frames cross as pixels over the guest's network.

Shared folders use a virtio-fs device per folder, with virtio-win's driver and WinFsp in the guest.

## Layout

```
src/
  host/        the Linux manager
  agent/       lsw-guest-agent.exe, the Windows service inside the VM
  guest/       other files that run in the VM, such as lsw-shares.ps1
  linkproto/   the host <-> guest wire contract, compiled by both sides
  base/        logging for the host
external/
  remotewin/   the window mirror
  accel-virt/  the GPU stack
scripts/       building, packaging, testing
docs/          protocol and testing documents
aur/           the lsw-bin AUR package and the release procedure
```

Every module has one `include/` tree and one `src/` tree, with one class per header, so includes read `lsw/<module>/<concern>/<class>.hpp`.

## Contributing

Start with [CONTRIBUTING.md](CONTRIBUTING.md), which covers development setup, style and how to change each part. In short:

- Format C++ with `clang-format`, using the repo's `.clang-format` and `.clang-tidy`.
- Write commit subjects in the imperative mood, around 70 characters.
- Make GPU changes in `accel-virt` and window changes in `remotewin`, then bump the submodule pointer here.
- Anything the manager and the guest agent must agree on belongs in `src/linkproto/`, specified in [docs/link-protocol.md](docs/link-protocol.md).

When reporting a bug, include your distribution, GPU and LSW version. The manager's log and, for shared folders, `C:\ProgramData\LSW\shares.log` in the guest are the most useful things to attach.

## License

LSW is licensed under Apache-2.0; see [LICENSE](LICENSE). Bundled third-party components keep their own licences, and release packages carry their texts in `usr/share/licenses`.

Shared folders are mounted with **WinFsp - Windows File System Proxy, Copyright (C) Bill Zissimopoulos**, <https://github.com/winfsp/winfsp>.
