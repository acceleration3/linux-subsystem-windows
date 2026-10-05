# lsw-bin — the AUR package

This directory is the AUR repository of `lsw-bin`: a `PKGBUILD` that
installs a prebuilt LSW release tarball, its `.SRCINFO`, and the
`lsw-bin.install` notes pacman prints. Nothing is compiled on the
user's machine. The tarball is built once per release by
`scripts/package-release.sh` and attached to the GitHub release
`v<version>`.

## What the tarball holds

Its root is a `usr/` tree, exactly what `cmake --install --component lsw`
lays out from the three build trees (the host build, and the mingw
x86_64 and i686 cross builds):

```
usr/bin/lsw-manager
usr/lib/lsw/remotewin                     the window mirror's host half
usr/lib/lsw/accel-virt/qemu/              accel-virt's QEMU, its modules,
                                          libav_executor.so
usr/lib/lsw/accel-virt/qemu/share/        QEMU's x86 firmware, ROMs, keymaps (-L)
usr/lib/lsw/accel-virt/d3d/               DXVK and vkd3d-proton
usr/share/lsw/guest/                      lsw-guest-agent.exe, remotewin-guest.exe,
                                          av-drivers/, loaders/{x64,x86}/vulkan-1.dll
usr/share/applications/lsw-manager.desktop
usr/lib/modules-load.d/lsw.conf           udmabuf at boot
usr/lib/udev/rules.d/70-lsw-udmabuf.rules /dev/udmabuf for the logged-in user
usr/share/licenses/lsw/                   LSW's license, and QEMU's, DXVK's,
                                          vkd3d-proton's and the Vulkan loader's
```

The manager finds all of it relative to its own binary, so the same
tree works installed in `/usr`, staged under a `DESTDIR`, or unpacked
anywhere.

## Cutting a release

Releases are built in a clean Arch Linux userland, never on a
developer's own system: the binaries link against the libraries of the
system that builds them, and QEMU's configure enables every optional
feature whose library it finds there. A distribution with its own
library builds or CPU-tuned repositories (CachyOS's znver4 repos, say)
would produce a package that is wrong for plain Arch.

### 1. Build the tarball in an Arch container

The release machine has a distrobox called `Arch` (plain Arch, core and
extra only, sharing `/home`). Once, install the build dependencies in
it:

```sh
distrobox enter Arch -- sudo pacman -Syu --needed \
    base-devel git cmake ninja python meson glslang clang lld llvm sdl3 \
    wine \
    mingw-w64-gcc \
    qt6-base libvirt vulkan-headers vulkan-icd-loader libisofs libvncserver \
    libxcb wimlib hivex zlib spdlog fmt nlohmann-json \
    wayland wayland-protocols pkgconf \
    ocl-icd libglvnd \
    glib2 pixman libslirp libpipewire libaio liburing libseccomp dtc capstone \
    gnutls libpng libjpeg-turbo libsasl snappy lzo zstd numactl keyutils \
    systemd-libs fuse3 pam libcbor \
    curl unzip xz zstd
```

`wayland`, `wayland-protocols` and `pkgconf` are remotewin's: its host
half generates the client code of the pointer-constraint, relative-pointer
and viewporter protocols from the system's protocol files
(`wayland-scanner`, found through pkg-config) and links
`libwayland-client`. Its Qt private headers (QtGui's and QtWaylandClient's)
are in `qt6-base` as Arch packages Qt 6.11 (an older Qt had them in
`qt6-wayland`).
The conformance harness and probe that live beside it are never part of
the release: they are only built by name.

Two of those are easy to wonder about: `wine` is there only for `widl`, the
IDL compiler vkd3d-proton's build needs, and `sdl3` because DXVK's native
build will not configure without a window-system backend (it also loads
SDL3 at run time, which is why the package depends on it).


The second half of that list is QEMU's: it links what it finds, and
what it links becomes a dependency of the package, so the container
should hold the libraries listed here and not much more.

Then, from the checkout (the container sees it at the same path):

```sh
cd ~/Coding/linux-subsystem-windows
git status                     # a release is built from what is checked out
distrobox enter Arch -- scripts/package-release.sh
```

`package-release.sh` builds in its own `build-release/` trees, started
over on every run, so nothing configured on the host is reused. It
fetches what accel-virt pins and does not commit — the Windows kit
subset (cut from the WDK and SDK NuGet packages, 270 MB to download,
44 MB kept; their licence forbids redistributing them, so they are
never committed), the Khronos registries (120 MB), QEMU's tarball
(140 MB), the OpenCL loader's source — unless they are already in the
checkout. Roughly 550 MB the first time, plus the dxvk and
vkd3d-proton submodules if they were never initialised. It drops `CFLAGS`/`LDFLAGS` from the environment, checks
that gcc and clang target generic x86-64, and fails if any build file
carries a CPU-specific flag. It writes:

```
dist/lsw-<version>-x86_64.tar.zst
dist/lsw-<version>-x86_64.tar.zst.sha256
dist/lsw-<version>-x86_64.depends      the packages the shipped ELF files link
```

The version is `project(LSW VERSION ...)` in the top-level
`CMakeLists.txt`; `--version` overrides it.

#### Without distrobox: a devtools chroot

The same build in a throwaway chroot, with `devtools` installed:

```sh
CHROOT=$HOME/chroots/lsw
mkdir -p "$CHROOT"
mkarchroot "$CHROOT/root" base-devel
arch-nspawn "$CHROOT/root" pacman -Syu --needed --noconfirm <the package list above>
sudo arch-nspawn --bind="$PWD" "$CHROOT/root" \
    bash -c "cd '$PWD' && git config --global --add safe.directory '*' && scripts/package-release.sh"
sudo chown -R "$USER:" build-release dist external/accel-virt/build-d3d
```

### 2. Publish the tarball

```sh
gh release create v<version> dist/lsw-<version>-x86_64.tar.zst \
    --title "LSW <version>" --notes "..."
```

### 3. Update the package

In `aur/`:

1. Set `pkgver` to the version, and `pkgrel=1`.
2. Set `sha256sums` to the hash in `dist/lsw-<version>-x86_64.tar.zst.sha256`
   (or run `updpkgsums` once the release is up).
3. Compare the library half of `depends` with
   `dist/lsw-<version>-x86_64.depends` and bring it in line: a library
   QEMU linked this time, or no longer does, changes it.
4. Regenerate `.SRCINFO` — the AUR reads it, not the `PKGBUILD`:

   ```sh
   makepkg --printsrcinfo > .SRCINFO
   ```

5. Check it: `namcap PKGBUILD`, then `makepkg -f` and
   `namcap lsw-bin-<version>-1-x86_64.pkg.tar.zst`.

### Rebuilding on a Qt update

`usr/lib/lsw/remotewin`, the window mirror's host half, uses Qt's private
API: QtWaylandClient's own cursor surface, through which a guest cursor is
drawn one guest pixel to one screen pixel at a fractional scale. Private
API has no compatibility promise from one Qt release to the next, so the
release is rebuilt - and `pkgrel` bumped - whenever Arch moves `qt6-base`
to a new version, as Arch does for its own packages that use it.

Until then, on a Qt other than the one it was built against (`qVersion()`
is compared with the build's `QT_VERSION_STR` before any private class is
touched), the mirror leaves that path alone and resamples its pixel
cursors instead: right on the glass, a little soft, and everything else
unchanged. What that check cannot cover is the dynamic linker: remotewin
imports a handful of private symbols (`Qt_6_PRIVATE_API`), and a Qt that
drops or renames one of them stops the mirror from starting at all: the
manager's log then has the loader's error and remotewin's exit (127), and
launched applications show no windows until the package is rebuilt.

### 4. Push to the AUR

The AUR repository holds only these files, at its root, on `master`.
From the main repository, push this directory's history as that branch:

```sh
git subtree push --prefix aur ssh://aur@aur.archlinux.org/lsw-bin.git master
```

or keep a separate clone of the AUR repository and copy the files over:

```sh
git clone ssh://aur@aur.archlinux.org/lsw-bin.git ~/aur/lsw-bin
cp aur/PKGBUILD aur/.SRCINFO aur/lsw-bin.install aur/.gitignore ~/aur/lsw-bin/
cd ~/aur/lsw-bin && git add -A && git commit -m "Update to <version>" && git push
```

## After installing

`lsw-bin.install` says this too:

- `/dev/udmabuf` is loaded at boot; before the first reboot,
  `sudo modprobe udmabuf`. The logged-in user gets access to it through
  udev's `uaccess` tag (systemd 256 and later grant it themselves).
- VMs run under the user's own libvirt session, `qemu:///session`: no
  `libvirt` group, no system `libvirtd`.
- Shared folders need `virtiofsd`; OpenCL in the guest needs an OpenCL
  driver on the host.
