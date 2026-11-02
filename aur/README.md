# lsw-bin — the AUR package

This directory is the AUR repository of `lsw-bin`: a `PKGBUILD` that
installs a prebuilt LSW release tarball, its `.SRCINFO`, and the
`lsw-bin.install` notes pacman prints. Nothing is compiled on the
user's machine. The tarball is built once per release by
`scripts/dist.sh arch` and attached to the GitHub release
`v<version>`, next to the release's deb and rpm packages.

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
usr/share/lsw/guest/virtiofs/             the shared folders' guest half: virtio-win's
                                          viofs driver and virtiofs.exe, the WinFsp
                                          installer, lsw-shares.ps1
usr/share/applications/lsw-manager.desktop
usr/share/licenses/lsw/                   LSW's license, and QEMU's, DXVK's,
                                          vkd3d-proton's, the Vulkan loader's,
                                          virtio-win's and WinFsp's
```

The manager finds all of it relative to its own binary, so the same
tree works installed in `/usr`, staged under a `DESTDIR`, or unpacked
anywhere.

## Cutting a release

A release is built once per distribution it ships for, each time in a
clean userland of that distribution, never on a developer's own system:
the binaries link against the libraries of the system that builds them,
and QEMU's configure enables every optional feature whose library it
finds there. A distribution with its own library builds or CPU-tuned
repositories (CachyOS's znver4 repos, say) would produce packages that
are wrong for everyone else.

### 1. Build the packages

`scripts/dist.sh` builds each target in a distrobox of its own
distribution, which shares `/home`, so each sees the checkout at the
same path:

| target | distrobox | writes to `dist/` |
| --- | --- | --- |
| `arch` | `Arch` (plain Arch, core and extra only) | `lsw-<version>-x86_64.tar.zst`, its `.sha256`, and `.depends`: the packages the shipped ELF files link |
| `deb` | `Ubuntu` (26.04) | `lsw_<version>-1_amd64.deb` |
| `rpm` | `Fedora` (45) | `lsw-<version>-1.fc45.x86_64.rpm` |

```sh
cd ~/Coding/linux-subsystem-windows
git status                     # a release is built from what is checked out
scripts/dist.sh                # all three; or name some: scripts/dist.sh arch rpm
```

A distrobox that does not exist yet is created, and its build
dependencies installed, on first use; `--setup` installs or updates them
in an existing one. The package lists are in `dist.sh`. The second half
of each is QEMU's: it links what it finds, and what it links becomes a
dependency of the package, so a box should hold the libraries listed
there and not much more. remotewin's host half is a Wayland client of
its own, without Qt: it generates the client code of the protocols it
speaks from the system's protocol files (wayland-protocols 1.38 or
newer), links `libwayland-client`, reads the compositor's keymap with
`libxkbcommon`, waits on DRM syncobj timelines with `libdrm`, and places
windows through KWin over `sd-bus`. Wine is there only for `widl`, the
IDL compiler vkd3d-proton's build needs, and SDL3 because DXVK's native
build will not configure without a window-system backend (it also
loads SDL3 at run time, which is why every package depends on it).

The guest half - the agent, remotewin's guest half, the Vulkan loaders,
and accel-virt's drivers and OpenCL loaders - does not depend on the
host's distribution: it is built once, in the `Arch` box, into
`build-release/guest/`, and every package carries the same files. Then
the targets build their host halves one after another, each in its own
`build-release/<target>/` trees, installing that driver tree rather
than building one (`LSW_AV_PREBUILT_DRIVERS`). Every tree starts over on
every run, so nothing configured on the host is reused. The first run fetches what
accel-virt pins and does not commit, into the checkout, where every
target shares it: the Windows kit subset (cut from the WDK and SDK
NuGet packages, 270 MB to download, 44 MB kept; their licence forbids
redistributing them, so they are never committed), the Khronos
registries (120 MB), QEMU's tarball (140 MB), the OpenCL loader's
source. Roughly 550 MB the first time, plus the dxvk and vkd3d-proton
submodules if they were never initialised. Each build drops
`CFLAGS`/`LDFLAGS` from the environment, checks that gcc and clang
target generic x86-64, and fails if any build file carries a
CPU-specific flag. It runs on the CPUs `dist.sh` itself may use.

The version is `project(LSW VERSION ...)` in the top-level
`CMakeLists.txt`; `--version` overrides it.

The deb's library dependencies come from `dpkg-shlibdeps` and the rpm's
from rpmbuild's own soname scan, so those packages need no list kept by
hand. What the manager runs without linking it (the libvirt session
daemon, the UEFI firmware, `qemu-img`, 7-Zip, curl, SDL3) is in
`packaging/deb/control.in` and `packaging/rpm/lsw.spec.in`, by each
distribution's package names.

### 2. Publish the release

```sh
gh release create v<version> \
    dist/lsw-<version>-x86_64.tar.zst \
    dist/lsw_<version>-1_amd64.deb \
    dist/lsw-<version>-1.fc45.x86_64.rpm \
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

The manager links only Qt's public API, so a Qt update calls for no
rebuild beyond what Arch's own soname bumps call for: the package needs
the Qt it was built against or newer (`qt6-base>=`). remotewin, the window
mirror's host half, links no Qt at all.

### 4. Push to the AUR

The AUR repository holds only the package's files, at its root, on
`master`, with a history of its own: one commit per release, none of
them this repository's. So a `git subtree push` of this directory is
refused (it is not a fast-forward of that history, and it would bring
this README along). Keep a clone of the AUR repository, fetching over
HTTPS and pushing over SSH, and copy the files into it:

```sh
git clone https://aur.archlinux.org/lsw-bin.git ~/aur/lsw-bin    # once
git -C ~/aur/lsw-bin remote set-url --push origin ssh://aur@aur.archlinux.org/lsw-bin.git
git -C ~/aur/lsw-bin pull
cp aur/PKGBUILD aur/.SRCINFO aur/lsw-bin.install aur/.gitignore ~/aur/lsw-bin/
cd ~/aur/lsw-bin && git add -A && git commit -m "lsw-bin <version>" && git push
```

The push authenticates with the AUR key, `~/.ssh/aur`: `~/.ssh/config`
names it for `aur.archlinux.org`.

## After installing

`lsw-bin.install` says this too:

- VMs run under the user's own libvirt session, `qemu:///session`: no
  `libvirt` group, no system `libvirtd`.
- Shared folders need `virtiofsd`; OpenCL in the guest needs an OpenCL
  driver on the host.
