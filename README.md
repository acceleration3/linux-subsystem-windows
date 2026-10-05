# Linux Subsystem for Windows (LSW)

A "reverse WSL" — run Windows applications on a Linux host with per-application native windows, their windows mirrored out of the guest and their GPU work run on the host's GPU.

## Status

Greenfield. Phase 0 scaffolding.

## Layout

```
src/
  agent/          lsw-guest-agent.exe — the Windows service inside the VM
  base/           logging for the host (the agent has its own)
  linkproto/      the host <-> guest wire contract
  host/           the Linux manager
external/
  remotewin/      the window mirror: an application's windows, host and guest halves
  accel-virt/     the GPU: a paravirtual WDDM adapter, its QEMU device and its guest drivers
```

There are two paths across the VM boundary. They meet in one place: a
mirrored window's frames, which the guest half copies into shared
textures on the guest's GPU and remotewin imports from accel-virt's
device as the host GPU memory they are in (the device's export socket,
`$XDG_RUNTIME_DIR/lsw-export.sock`, which the domain names and every
`remotewin` is given). Without it - an older QEMU, another GPU, a VM
defined before it - frames cross as pixels over the guest's network, as
they always did.

**remotewin** carries windows. Launching an application starts a
`remotewin` process for it, which owns that application's windows on this
desktop — their pixels, their input, their menus and their taskbar
identity — and a `remotewin-guest.exe` beside the application in the
guest, which dials back out to it. One process per application, because
a menu has to be a popup of the window it was opened from and no
compositor lets one process place a surface against another's. That half
lives in [remotewin](external/remotewin), a submodule; it knows how to
move a Windows application's windows across a boundary and nothing about
virtual machines.

**accel-virt** carries GPU work. The guest gets a real display adapter,
named after the host GPU it runs on, and its applications use Direct3D
11 and 12, Vulkan, OpenGL and OpenCL on it as they would on a physical
card. It has two halves, and LSW builds both:

- *On the host*, a patched QEMU with the `virtio-gpu-av-pci` device,
  which the VM runs on in place of the distribution's QEMU, and
  `libav_executor.so`, which runs every command the guest sends on the
  host's own Vulkan, EGL and OpenCL — with DXVK and vkd3d-proton, built
  natively, for Direct3D. The executor runs inside QEMU: libvirt
  confines QEMU with a seccomp filter that lets it start no process,
  so accel-virt's separate renderer process is not used.
- *In the guest*, `avgpu.sys`, a WDDM 3.2 kernel-mode display miniport,
  and the user-mode drivers beside it — `avumd.dll` for Direct3D,
  `avvk.dll`, `avgl.dll` and `avcl.dll`, each again for 32-bit
  processes. They are built on the host, with accel-virt's own clang-cl
  toolchains, into `build/av-drivers`; the payload ISO carries that tree
  and the guest installs it as `C:\av` with accel-virt's
  `provision.ps1`.

The desktop runs on it too: DWM composes on the host GPU and the host
scans the result out without copying it back into the guest. That half
lives in [accel-virt](external/accel-virt), a submodule; it knows how to
carry a graphics API across a virtio-gpu device and nothing about
provisioning Windows or mirroring its windows, so it builds and tests
entirely on its own:

```sh
git submodule update --init --recursive
```

It sets the guest's floor. The driver declares WDDM 3.2, which Windows
refuses below **Windows 11 24H2** (build 26100), and it is
**test-signed**: the guest runs with test signing on, which means UEFI
without enrolled Secure Boot keys. accel-virt's
[README](external/accel-virt/README.md) has the rest — what each API
offers, and where it falls short.

What this repository adds is the VM around both: provisioning Windows,
running it, and presenting its applications as native Linux windows.

`src/linkproto/` is the one module the Linux manager and the Windows
guest agent both compile: envelope framing, stream reassembly, and the
RPC command vocabulary. It is header-only and depends on
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
  vm/         libvirt lifecycle, the domain, stats
  link/       the multiplexed JSON channel to the guest agent
  apps/       guest app catalog, per-app config, launch, logs
  mirror/     the window mirror a launch starts, one per application
  ui/         widgets and theme shared by both UIs
  services/   HostServices — the container every feature takes
  provision/  first-run install pipeline (ISO, autounattend, setup)
  tools/      the image tools, opened by the wizard and the manager
  wizard/     the first-run QWizard and its pages
  manager/    the everyday window
  mcp/        MCP automation surface over localhost HTTP
  app/        composition root + entry point
```

Those fourteen concerns are linked as five libraries plus the executable —
see the header comment in `src/host/CMakeLists.txt`. Include paths never
name a target, so moving a concern between libraries touches no source.

## Building

```sh
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build
```

or `scripts/build.sh`, which does the same and cross-builds the Windows
half first.

Requires CMake 3.25+, a C++20 compiler, Qt 6, libvirt (with
`libvirt-qemu`), and the Vulkan development package. The window mirror's
host half (remotewin) adds a C compiler, pkg-config, `wayland-client`,
`wayland-scanner`, `wayland-protocols` and EGL, and Qt's DBus, OpenGL and
OpenGLWidgets modules and its private Gui and WaylandClient modules (in
`qt6-base` on Arch's Qt 6.11, `qt6-wayland` before): configure names
whichever is missing, and
`-DLSW_BUILD_REMOTEWIN=OFF` builds without the mirror. The private API
ties the built `remotewin` to the Qt it was built against; rebuild it
after a Qt update (aur/README.md says what a mismatch costs). accel-virt brings
its own requirements, because the host build is also where its guest
drivers are built:

- clang and clang++, clang-cl, lld (`lld-link`), `llvm-lib`, `llvm-rc`
  and `llvm-mt` — accel-virt builds with nothing else, so it is driven
  as an external project with its own compiler rather than as part of
  this build;
- Python 3.11+, meson and glslang (DXVK and vkd3d-proton), and QEMU's
  own build dependencies — glib2, pixman, gtk3, libepoxy, libslirp;
- on the host at run time: KVM, the `udmabuf` module, and a Vulkan
  driver, EGL and an OpenCL ICD for each API the guest should have.

The first build fetches what accel-virt pins and does not commit — the
Windows kit subset, the Khronos registries, QEMU's tarball, the OpenCL
loader's source — and compiles QEMU, DXVK and vkd3d-proton, so it takes
a while. The Windows kit is cut from multi-gigabyte WDK and SDK
packages; with one already fetched elsewhere, pass
`-DAV_WINDOWS_KIT_DIR=<accel-virt>/third_party/windows-kit`.
`-DLSW_BUILD_ACCEL_VIRT=OFF` builds the manager without any of it, and
without a GPU for its VMs.

What the GPU build leaves, beside the manager in `build/`:

```
build/
  av-drivers/                 the guest's C:\av: provision.ps1, adapters.txt,
                              package/ (avgpu.sys, avgpu.inf, the user-mode
                              drivers, package.sha256), loaders/{x64,x86}/OpenCL.dll
  accel-virt/qemu/            qemu/qemu-system-x86_64 and its modules,
                              libav_executor.so
  accel-virt/d3d/lib/         DXVK and vkd3d-proton for the host
```

The manager looks for them there, relative to its own executable;
`LSW_AV_DRIVERS`, `LSW_AV_QEMU_BUILD` and `LSW_AV_D3D_PATH` point it
elsewhere.

`scripts/build-agent.sh` cross-builds the rest of the Windows half with
mingw-w64: the guest agent, `remotewin-guest.exe`, and the Vulkan loader
for x64 and x86, which Windows does not ship and accel-virt does not
package.

## License

Apache-2.0. See [LICENSE](LICENSE).
