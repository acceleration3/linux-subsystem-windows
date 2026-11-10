# Building LSW on Ubuntu

The official build workflow uses an Arch Linux container for the Windows guest
cross-build (`scripts/dist.sh` always runs `--in-box arch guest`). This document
covers building locally on Ubuntu for development purposes.

## Version compatibility

| Ubuntu | Host build | Guest cross-build | Notes |
|---|---|---|---|
| 24.04 | ✗ | ✗ | Qt 6.4.2 too old (needs ≥ 6.5), no `libsdl3-dev` |
| 24.10 / 25.04 | ✓ | Likely ✓ | Qt 6.7+, GCC 14 mingw-w64; not verified |
| **26.04 LTS** | **✓** | **✗** | Qt 6.10.2, GCC 13 mingw-w64 (see below) |

**26.04 LTS ships GCC 13 for the mingw-w64 cross-compiler.** All guest sources
compile cleanly but the final link fails: `_set_abort_behavior` is declared in
the UCRT headers but absent from Ubuntu's `libmsvcrt.a`. See
[Guest cross-build limitation](#guest-cross-build-limitation) below.

---

## Build dependencies

```sh
sudo apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  build-essential git cmake ninja-build python3 python3-venv meson \
  glslang-tools clang lld llvm \
  qt6-base-dev libvirt-dev libvulkan-dev libisofs-dev libvncserver-dev \
  libxcb1-dev libwim-dev libhivex-dev zlib1g-dev libspdlog-dev libfmt-dev \
  nlohmann-json3-dev libwayland-dev wayland-protocols libxkbcommon-dev \
  libdrm-dev pkgconf ocl-icd-opencl-dev libglvnd-dev \
  libsdl3-dev libglib2.0-dev libpixman-1-dev libslirp-dev libpipewire-0.3-dev \
  libaio-dev liburing-dev libseccomp-dev libfdt-dev libcapstone-dev \
  libgnutls28-dev libpng-dev libjpeg-turbo8-dev libsasl2-dev libsnappy-dev \
  liblzo2-dev libzstd-dev libnuma-dev libkeyutils-dev libsystemd-dev \
  libfuse3-dev libpam0g-dev libcbor-dev libbpf-dev libusb-1.0-0-dev \
  mingw-w64 mingw-w64-tools \
  curl unzip xz-utils zstd
```

> `libsdl3-dev` landed in Ubuntu 24.10; it is not available on 24.04.
> `--host-only` skips accel-virt and QEMU entirely and does not need it.

### Additional setup for the guest cross-build

The mingw-w64 cross-compiler must use the POSIX thread model for `std::thread`
support. Set it once after installation:

```sh
sudo update-alternatives --set x86_64-w64-mingw32-g++ \
  /usr/bin/x86_64-w64-mingw32-g++-posix
sudo update-alternatives --set x86_64-w64-mingw32-gcc \
  /usr/bin/x86_64-w64-mingw32-gcc-posix
```

The accel-virt D3D build requires `widl` by that exact name. The package
installs it with a target prefix; create a symlink before building:

```sh
mkdir -p ~/.local/bin
ln -sf /usr/bin/x86_64-w64-mingw32-widl ~/.local/bin/widl
```

Ensure `~/.local/bin` is on `PATH` before invoking `build.sh`.

---

## Building

Full build (host + guest):

```sh
./scripts/build.sh
```

Host-only (skips mingw-w64 guest and accel-virt QEMU; faster, no SDL3 needed):

```sh
./scripts/build.sh --host-only
```

Install to `~/.local`:

```sh
cmake --install build --prefix ~/.local --component lsw
```

---

## Running LSW — runtime dependencies

These are needed to run the manager, not just build it.

```sh
sudo apt-get install -y \
  libvirt-daemon-system libvirt-clients \
  ovmf \
  virtiofsd
```

| Package | Purpose |
|---|---|
| `libvirt-daemon-system` | libvirt user-session daemon (`qemu:///session`) |
| `libvirt-clients` | `virsh` for diagnostics |
| `ovmf` | UEFI firmware; Windows 11 requires UEFI |
| `virtiofsd` | Host folder sharing into the guest |

Start the user-session libvirt socket (no system daemon or root required):

```sh
systemctl --user enable --now virtqemud.socket
```

Verify with:

```sh
virsh -c qemu:///session list
```

### KVM

LSW requires KVM. Enable hardware virtualisation in BIOS/UEFI (labelled
**SVM** on AMD systems, **VT-x** / **Intel Virtualization Technology** on
Intel), then load the module:

```sh
# AMD
sudo modprobe kvm_amd
# Intel
sudo modprobe kvm_intel
```

To load automatically on boot:

```sh
# AMD
echo "kvm_amd" | sudo tee /etc/modules-load.d/kvm.conf
# Intel
echo "kvm_intel" | sudo tee /etc/modules-load.d/kvm.conf
```

### AppArmor — allow LSW's custom QEMU

Ubuntu's libvirt AppArmor profile only permits executing QEMU from system
paths. Add a local override to allow the installed binary:

```sh
sudo tee /etc/apparmor.d/local/usr.sbin.libvirtd > /dev/null << 'EOF'
@{HOME}/.local/lib/lsw/accel-virt/qemu/qemu-system-x86_64 PUx,
@{HOME}/.local/lib/lsw/accel-virt/qemu/qemu-system-x86_64/** r,
EOF
sudo apparmor_parser -r /etc/apparmor.d/usr.sbin.libvirtd
```

### QEMU firmware path

LSW's custom QEMU searches for EFI firmware descriptors relative to its own
binary. Symlink the system descriptors into the expected location:

```sh
mkdir -p ~/.local/lib/lsw/accel-virt/share/qemu-firmware
ln -s /usr/share/qemu/firmware/*.json \
  ~/.local/lib/lsw/accel-virt/share/qemu-firmware/
```

---

## Host-only build + guest binaries from release

If you are working on the manager, MCP server, or GPU stack and do not need to
modify the guest agent or remotewin-guest, build host-only and pull the guest
components from the latest release:

```sh
# Build the host side
./scripts/build.sh --host-only

# Download release and extract guest components
mkdir -p /tmp/lsw-release
curl -L https://github.com/acceleration3/linux-subsystem-windows/releases/download/v0.2.3/lsw-0.2.3-x86_64.tar.zst \
  | tar --zstd -xf - -C /tmp/lsw-release

# Install host build then overlay guest files
cmake --install build --prefix ~/.local --component lsw
cp -a /tmp/lsw-release/usr/share/lsw/guest ~/.local/share/lsw/
```

The manager locates guest artifacts relative to its own binary so it will pick
up the installed files automatically.

---

## Guest cross-build limitation

On Ubuntu 26.04 LTS, the guest cross-build compiles cleanly (272/272 sources)
but fails at link time:

```
undefined reference to `__imp__set_abort_behavior'
```

**Root cause.** Ubuntu 26.04 ships `mingw-w64 13.0.0` paired with GCC 13. GCC
13's linker specs link `-lmsvcrt` (the legacy Windows CRT) by default, but
Ubuntu's `libmsvcrt.a` does not stub `_set_abort_behavior`. The function is
present in `libucrt.a` (the modern UCRT), but linking both causes symbol and
`.rsrc` section conflicts. Switching the specs to use `-lucrt` exclusively
breaks `libwinpthread.a` which also expects MSVCRT-linked symbols.

**Paths to a working guest build:**

- Build GCC 14 + mingw-w64 14 from source, targeting UCRT. This matches what
  Arch Linux ships and what the official CI uses.
- Use llvm-mingw (Clang-based, UCRT by default). Requires adding
  `#include <cstddef>` to a small number of files in `external/remotewin/`
  that rely on `std::size_t` being transitively included, which libstdc++ does
  but libc++ does not.

Both are documented with step-by-step instructions in `docs/next.md`.
