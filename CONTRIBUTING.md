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
  the mingw cross-build. The host half is a Wayland client without Qt:
  it needs Wayland's client library, scanner and protocols, xkbcommon,
  libdrm and libsystemd (pkg-config) and a C compiler; configure stops
  with the list of what is missing (README.md). remotewin's probe
  (`remotewin-probe`) is never in `all` and never installed: build it by
  name
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

RemoteWin launch/reopen regression checks run without a VM:

```sh
cmake --build build --target lsw_host_guest_app_launcher_test
ctest --test-dir build -R lsw_host_guest_app_launcher_test --output-on-failure
```

An opt-in live check starts Character Map, verifies repeated opens and
reattachment to the same Windows PID, then closes only its own window:

```sh
LSW_REMOTEWIN="$PWD/build/remotewin/source/host/remotewin" \
  build/src/host/tests/lsw_host_guest_app_launcher_test --live
```

This requires the running guest agent on `127.0.0.1:5102`, matching
RemoteWin halves, and a working desktop session. It refuses to run if
Character Map is already open. RemoteWin's standalone test suite also
covers parent-pipe lifecycle and malformed window counts.

To check MSIX/APPX mirroring against a package already copied to the guest:

```sh
LSW_REMOTEWIN="$PWD/build/remotewin/source/host/remotewin" \
  build/src/host/tests/lsw_host_guest_app_launcher_test \
  --live-package 'C:\ProgramData\LSW\uploads\<id>\package.msix'
```

This opens App Installer, verifies a real frame in application/PID mode
(never desktop mode), and closes only the test's host mirror. It does not
click Install or close the native installer window.

That produces `lsw-guest-agent.exe` and `remotewin-guest.exe` under
`build-agent/`, and the Vulkan loader for x86-64 and i686 under
`build-agent/` and `build-agent32/`. The wizard picks all of them up from
there. The GPU drivers are not part of it: they are built by the host
build, with accel-virt's own toolchains.

## Installing

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


## Releasing

Packaging files live in `packaging/`:

| folder | what |
| --- | --- |
| `packaging/aur/` | the `lsw-bin` AUR package, exactly what is pushed to the AUR |
| `packaging/arch/` | builds `lsw-bin` from a local `dist/` tarball, for testing before a release |
| `packaging/deb/` | the deb's control template and postinst |
| `packaging/rpm/` | the rpm spec template |

### Releasing with CI

`.github/workflows/release.yml` does the three steps below on GitHub,
in containers of the same distributions, by running the same
`scripts/dist.sh --in-box <step>` commands. Bump the version in
`CMakeLists.txt`, commit, then push an annotated tag:

```sh
git tag -a v<version> -m "notes for the release"
git push origin v<version>
```

The tag must equal `v` plus the project version. The workflow builds
the Windows half once (`guest`), then `arch`, `deb` and `rpm`, creates
the release `v<version>` titled "LSW <version>" with the tarball, its
`.sha256`, the deb and the rpm (the notes are the tag's message), and
then updates the AUR package and commits `packaging/aur` to `master`.

- The `aur` job runs only when the repository secret `AUR_SSH_KEY` is
  set: a private key without a passphrase whose public half is
  registered on the AUR account that maintains `lsw-bin`. It fails,
  asking for a manual review, when the tarball links a library whose
  package `packaging/aur/PKGBUILD` does not list in `depends`.
- Run the workflow by hand from the Actions tab (or `gh workflow run
  release.yml -f publish=false`) to build and download the packages as
  workflow artifacts without releasing anything. It also runs that way
  every Monday, to notice when a distribution's libraries drift.

The steps below are the same procedure done locally.

### 1. Build the packages

`scripts/dist.sh` builds every package in a clean distrobox of its
distribution, never on the development machine, because the binaries
link the libraries of the system that builds them.

| target | distrobox | output in `dist/` |
| --- | --- | --- |
| `arch` | `Arch` | `lsw-<version>-x86_64.tar.zst`, `.sha256`, `.depends` |
| `deb` | `Ubuntu` 26.04 | `lsw_<version>-1_amd64.deb` |
| `rpm` | `Fedora` 45 | `lsw-<version>-1.fc45.x86_64.rpm` |

```sh
git status          # the release is built from what is checked out
scripts/dist.sh     # all targets; or name some: scripts/dist.sh arch rpm
```

- A missing distrobox is created with its build dependencies on first
  use. `--setup` updates an existing one. The package lists are in
  `dist.sh`.
- The Windows half (agent, mirror, loaders, accel-virt's drivers) is
  built once in the `Arch` box and shared by every package.
- The first run downloads about 550 MB that accel-virt needs (Windows
  kit subset, Khronos registries, QEMU source, OpenCL loader source).
- The version comes from `project(LSW VERSION ...)` in `CMakeLists.txt`;
  `--version` overrides it.
- The deb and rpm work out their library dependencies themselves. Tools
  the manager runs without linking them are listed in
  `packaging/deb/control.in` and `packaging/rpm/lsw.spec.in`.

The tarball's root is a `usr/` tree: `bin/lsw-manager`, the host
binaries and accel-virt's QEMU under `lib/lsw`, the Windows files under
`share/lsw/guest`, the desktop file, and the licenses.

### 2. Publish the GitHub release

```sh
gh release create v<version> \
    dist/lsw-<version>-x86_64.tar.zst \
    dist/lsw_<version>-1_amd64.deb \
    dist/lsw-<version>-1.fc45.x86_64.rpm \
    --title "LSW <version>" --notes "..."
```

The deb and rpm are installed straight from the release.

### 3. Update the AUR package

In `packaging/aur/`:

1. Set `pkgver` to the version and `pkgrel=1`.
2. Set `sha256sums` from `dist/lsw-<version>-x86_64.tar.zst.sha256`, or
   run `updpkgsums` once the release is up.
3. Bring the library part of `depends` in line with
   `dist/lsw-<version>-x86_64.depends`.
4. `makepkg --printsrcinfo > .SRCINFO`
5. Check with `namcap PKGBUILD`, `makepkg -f` and
   `namcap lsw-bin-<version>-1-x86_64.pkg.tar.zst`.

A Qt update needs no rebuild: the package only requires the Qt it was
built against or newer.

The AUR repository has its own history, so copy the files into a clone
of it and push from there:

```sh
git clone https://aur.archlinux.org/lsw-bin.git ~/aur/lsw-bin    # once
git -C ~/aur/lsw-bin remote set-url --push origin ssh://aur@aur.archlinux.org/lsw-bin.git
git -C ~/aur/lsw-bin pull
cp packaging/aur/{PKGBUILD,.SRCINFO,lsw-bin.install,.gitignore} ~/aur/lsw-bin/
cd ~/aur/lsw-bin && git add -A && git commit -m "lsw-bin <version>" && git push
```

The push uses the AUR SSH key named for `aur.archlinux.org` in
`~/.ssh/config`.

## Continuous integration

`.github/workflows/ci.yml` runs on every push to `master` and every pull
request: the manager and the host tests (`ctest -R "lsw_host_|linkproto|agent_"`,
built without accel-virt's QEMU), the Windows agent and window mirror
cross build, PowerShell checks (parser and PSScriptAnalyzer, including
the scripts embedded in `src/host/embedded/autounattend.xml`), shellcheck
and a clang-format check of the changed lines under `src/`. `remotewin`
and `accel-virt` have their own workflows.

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
lives in `src/linkproto/`, which BOTH toolchains compile. Change it
there, once; do not restate a constant on either side. The framing is
covered by `lsw_linkproto_test`, so a divergence is a failed test
rather than a garbled frame at runtime.

Adding a guest RPC: one new TU under
`src/agent/src/rpc/handlers/<group>/`, its command name added to
`lsw/linkproto/commands.hpp`, and the TU listed in
`src/agent/CMakeLists.txt`. The transport is not involved — including
for log verbosity and reply summaries, which are handler overrides
(`RpcHandler::request_level`, `RpcHandler::summarize`), and for where it runs:
the agent is a LocalSystem service in session 0, and a command that has
to run inside the user's session (windows, UI Automation, HKCU) says so
with `RpcHandler::placement()`; the service then forwards it to its
session helper there.

Adding an agent-pushed event: implement `rpc::EventSource` and register
it with the connection's `EventPump` in `host_endpoint.cpp`.

## Reporting a bug

Attach a log archive. In the manager, open the Log page or Settings, Logs,
and press **Collect logs**. It writes
`~/.local/share/lsw/logs/lsw-logs-<date>-<time>.tar.gz` and shows where.
The archive holds:

- the manager's log, each window mirror's log and the VM's logs (QEMU,
  virtiofsd and the GPU driver);
- Windows' logs: the agent, the mirrors, LSW updates, shared folders,
  Windows setup and the GPU drivers (when Windows is running);
- `system.txt`: LSW version and release, distribution, kernel, desktop,
  Qt and libvirt versions, the host GPUs and drivers, and the VM's
  definition;
- `settings.txt`: LSW's settings, with passwords and keys removed.

Logs can contain window titles and file names. Look through the archive
before posting it publicly.

To capture more detail:

1. In Settings, Logs, press **Clear all logs** so the archive only holds
   the new run.
2. Press **Debug logging for 30 minutes**, or raise the level of the part
   you suspect.
3. Reproduce the problem, then press **Collect logs**.

On disk, the host's logs are in `~/.local/share/lsw/logs/` and Windows'
are in `C:\ProgramData\LSW\logs\`. The manager also writes a
`crash-<date>-<time>.log` there if it crashes; include it.

## Where to find what

- [README.md](README.md#layout) — the source tree, one line per module.
- `scripts/gpu-test/gpu-test.sh` — runs every graphics suite and the
  app/game trace suites on every GPU of the test machines.
