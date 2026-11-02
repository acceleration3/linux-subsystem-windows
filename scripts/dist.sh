#!/usr/bin/env bash
# Build LSW's release packages, each in a distrobox of the distribution
# it is for.
#
#   scripts/dist.sh [--version V] [--kit DIR] [--setup] [--incremental] [TARGET...]
#
#   TARGET         arch, deb, rpm or all (the default: all three, one
#                  after another)
#                    arch  dist/lsw-<V>-x86_64.tar.zst, its .sha256 and
#                          .depends: the tarball the AUR's lsw-bin
#                          installs (aur/README.md), built on Arch
#                    deb   dist/lsw_<V>-1_amd64.deb, built on Ubuntu
#                    rpm   dist/lsw-<V>-1.<dist>.x86_64.rpm, built on Fedora
#   --version V    the release version (default: project(LSW VERSION) in
#                  CMakeLists.txt). The tarball, the tag (v<V>) and every
#                  package carry it.
#   --kit DIR      an already-fetched accel-virt Windows kit
#                  (AV_WINDOWS_KIT_DIR), to skip fetching it again
#   --setup        install or update the build dependencies in each
#                  target's distrobox before building. A distrobox that
#                  does not exist yet is created and set up regardless.
#   --incremental  reuse the release trees from the last run instead of
#                  starting them over: for iterating on this script, not
#                  for a release
#
# The distroboxes are Arch, Ubuntu and Fedora, made from the images
# below; LSW_BOX_ARCH, LSW_BOX_DEB and LSW_BOX_RPM name others. Each sees
# this checkout at the same path (distrobox shares $HOME). The build runs
# on the CPUs this script may use, which a container would otherwise not
# be held to.
#
# Why one build per distribution. The binaries link against the
# libraries of the system that builds them, and QEMU's configure enables
# every optional feature whose library it finds there, so a build is
# only right for the distribution it was made on, at the release it was
# made on. A distribution with its own library builds or CPU-tuned
# repositories (CachyOS's znver4 repos, say) would produce packages that
# are wrong for everyone else, which is why nothing here builds on the
# host.
#
# The guest half - the agent, remotewin's guest half and the Vulkan
# loaders (Windows programs built with mingw-w64), and accel-virt's
# drivers and OpenCL loaders (built with clang against the Windows kit) -
# does not depend on the host's distribution, so it is built once, in the
# Arch box, with the toolchains this project is developed against, and
# every package carries the same files.
#
# The trees are this script's own, build-release/<target>/ and
# build-release/guest/ (gitignored),
# so a developer's build/ and build-agent*/ are neither used nor
# touched, and they start over on every run, since a tree configured on
# another system would be reused as it is. The one tree outside
# build-release/ is accel-virt's own build-d3d/, where
# tools/build_d3d_runtime.sh always builds DXVK and vkd3d-proton and has
# no switch to build anywhere else: it is started over for every target,
# which is why the targets build one after another. A developer's build
# keeps its own copy of those libraries (build/accel-virt/d3d/lib) and is
# unaffected until its next build, which then reuses these portable ones.
# What accel-virt fetches - the Windows kit subset, the Khronos
# registries, QEMU's tarball, the OpenCL loader's source, its submodules
# - lands in the checkout and is shared by every target, so only the
# first run needs the network for it.
#
# Portability. Whatever is built here runs on other people's machines,
# so nothing may be tuned for this one. The compiler flags in the
# environment are dropped and the compilers must target the generic
# x86-64 baseline by default, which is checked, not assumed: accel-virt's
# QEMU configure and its DXVK / vkd3d-proton meson builds take their
# flags from the environment and the compiler's defaults, and there is
# no one place to pass a -march to all of them. After the build every
# generated build file is searched for a CPU-specific flag. The flags
# that remain are the projects' own baselines (QEMU's -mcx16, DXVK's
# -msse3), which upstream ships with everywhere.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SELF="${ROOT}/scripts/dist.sh"
DIST="${ROOT}/dist"
TARGETS_ALL=(arch deb rpm)

say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
fail() { echo "!!! dist: $*" >&2; exit 1; }

box_of() {
    case "$1" in
        arch) echo "${LSW_BOX_ARCH:-Arch}" ;;
        deb)  echo "${LSW_BOX_DEB:-Ubuntu}" ;;
        rpm)  echo "${LSW_BOX_RPM:-Fedora}" ;;
    esac
}
image_of() {
    case "$1" in
        arch) echo docker.io/library/archlinux:latest ;;
        deb)  echo quay.io/toolbx/ubuntu-toolbox:26.04 ;;
        rpm)  echo registry.fedoraproject.org/fedora-toolbox:45 ;;
    esac
}

# ---- build dependencies, per distribution ----
# The first lines are the toolchains: a host compiler, clang and lld for
# the Windows drivers, meson and glslang for DXVK and vkd3d-proton, widl
# (from Wine) for vkd3d-proton's IDL, and on Arch the two mingw-w64
# toolchains of the guest half, which is built there only (below). Then
# the manager's and remotewin's libraries, then QEMU's: it links what it
# finds, and what it links becomes a dependency of the package, so the
# box holds what is listed here and not much more. sdl3 is there because
# DXVK's native build will not configure without a window-system
# backend.
DEPS_ARCH=(
    base-devel git cmake ninja python meson glslang clang lld llvm sdl3 wine mingw-w64-gcc
    qt6-base libvirt vulkan-headers vulkan-icd-loader libisofs libvncserver
    libxcb wimlib hivex zlib spdlog fmt nlohmann-json
    wayland wayland-protocols libxkbcommon libdrm pkgconf
    ocl-icd libglvnd
    glib2 pixman libslirp libpipewire libaio liburing libseccomp dtc capstone
    gnutls libpng libjpeg-turbo libsasl snappy lzo zstd numactl keyutils
    systemd-libs fuse3 pam libcbor libbpf libusb
    curl unzip xz
)
DEPS_DEB=(
    build-essential dpkg-dev fakeroot git cmake ninja-build python3 python3-venv meson
    glslang-tools clang lld llvm libsdl3-dev wine64-tools
    qt6-base-dev libvirt-dev libvulkan-dev libisofs-dev libvncserver-dev
    libxcb1-dev libwim-dev libhivex-dev zlib1g-dev libspdlog-dev libfmt-dev nlohmann-json3-dev
    libwayland-dev wayland-protocols libxkbcommon-dev libdrm-dev pkgconf
    ocl-icd-opencl-dev libglvnd-dev
    libglib2.0-dev libpixman-1-dev libslirp-dev libpipewire-0.3-dev libaio-dev liburing-dev
    libseccomp-dev libfdt-dev libcapstone-dev
    libgnutls28-dev libpng-dev libjpeg-turbo8-dev libsasl2-dev libsnappy-dev liblzo2-dev
    libzstd-dev libnuma-dev libkeyutils-dev
    libsystemd-dev libfuse3-dev libpam0g-dev libcbor-dev libbpf-dev libusb-1.0-0-dev
    curl unzip xz-utils zstd
)
DEPS_RPM=(
    gcc gcc-c++ make git cmake ninja-build python3 meson rpm-build
    glslang clang lld llvm SDL3-devel wine-devel
    qt6-qtbase-devel libvirt-devel vulkan-headers vulkan-loader-devel libisofs-devel
    libvncserver-devel libxcb-devel wimlib-devel hivex-devel zlib-ng-compat-devel spdlog-devel
    fmt-devel json-devel
    wayland-devel wayland-protocols-devel libxkbcommon-devel libdrm-devel pkgconf
    ocl-icd-devel libglvnd-devel
    glib2-devel pixman-devel libslirp-devel pipewire-devel libaio-devel liburing-devel
    libseccomp-devel libfdt-devel capstone-devel
    gnutls-devel libpng-devel libjpeg-turbo-devel cyrus-sasl-devel snappy-devel lzo-devel
    libzstd-devel numactl-devel keyutils-libs-devel
    systemd-devel fuse3-devel pam-devel libcbor-devel libbpf-devel elfutils-libelf-devel
    libusb1-devel
    curl unzip xz zstd
)

# Runs inside the box.
install_deps() {
    case "$1" in
        arch) sudo pacman -Syu --needed --noconfirm "${DEPS_ARCH[@]}" ;;
        deb)
            sudo apt-get update
            sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${DEPS_DEB[@]}"
            ;;
        rpm) sudo dnf -y install --setopt=install_weak_deps=False "${DEPS_RPM[@]}" ;;
    esac
}

# ---- arguments ----
VERSION=""
KIT=""
INCREMENTAL=0
SETUP=0
IN_BOX=""
targets=()
while (( $# )); do
    case "$1" in
        --version)     VERSION="$2"; shift 2 ;;
        --kit)         KIT="$2"; shift 2 ;;
        --incremental) INCREMENTAL=1; shift ;;
        --setup)       SETUP=1; shift ;;
        --in-box)      IN_BOX="$2"; shift 2 ;;
        -h|--help)     sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        all)           targets+=("${TARGETS_ALL[@]}"); shift ;;
        arch|deb|rpm)  targets+=("$1"); shift ;;
        *)             fail "unknown argument '$1' (--help)" ;;
    esac
done
if [[ -z "${VERSION}" ]]; then
    VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "${ROOT}/CMakeLists.txt" | head -1)"
fi
[[ -n "${VERSION}" ]] || fail "no version (pass --version)"

# ============================================================ on the host
if [[ -z "${IN_BOX}" ]]; then
    command -v distrobox >/dev/null || fail "distrobox not found"
    (( ${#targets[@]} )) || targets=("${TARGETS_ALL[@]}")
    # Each target once, in the order given.
    declare -A seen=()
    order=()
    for t in "${targets[@]}"; do
        [[ -n "${seen[$t]:-}" ]] || { order+=("$t"); seen[$t]=1; }
    done
    if [[ -n "$(git -C "${ROOT}" status --porcelain --untracked-files=no --ignore-submodules=dirty)" ]]; then
        echo "!!! the working tree has uncommitted changes; the release will contain them" >&2
    fi
    # The CPUs this script may run on, which podman does not carry into
    # the container.
    cpus="$(taskset -pc $$ | sed 's/.*: //')"
    pass=(--version "${VERSION}")
    [[ -n "${KIT}" ]] && pass+=(--kit "${KIT}")
    (( INCREMENTAL )) && pass+=(--incremental)
    declare -A set_up=()
    # run_in_box <distribution target> <in-box step>
    run_in_box() {
        local box args=("${pass[@]}")
        box="$(box_of "$1")"
        if ! distrobox list --no-color 2>/dev/null | awk -F'|' '{gsub(/ /, "", $2); print $2}' \
                | grep -qx "${box}"; then
            say "Creating the ${box} distrobox ($(image_of "$1"))"
            distrobox create --yes --name "${box}" --image "$(image_of "$1")"
            args+=(--setup)
        elif (( SETUP )) && [[ -z "${set_up[$box]:-}" ]]; then
            args+=(--setup)
        fi
        set_up[$box]=1
        say "$2: building in the ${box} distrobox, on CPUs ${cpus}"
        distrobox enter "${box}" -- taskset -c "${cpus}" "${SELF}" --in-box "$2" "${args[@]}"
    }
    # The guest half first, once, on Arch: every package carries the same.
    run_in_box arch guest
    for t in "${order[@]}"; do
        run_in_box "$t" "$t"
    done
    built=()
    for t in "${order[@]}"; do
        case "$t" in
            arch) built+=("lsw-${VERSION}-x86_64.tar.zst") ;;
            deb)  built+=("lsw_${VERSION}-1_amd64.deb") ;;
            rpm)  built+=("$(cd "${DIST}" && ls -t lsw-"${VERSION}"-1.*.x86_64.rpm | head -1)") ;;
        esac
    done
    say "Done, in ${DIST}: ${built[*]}"
    exit 0
fi

# ============================================================ in the box
# Two kinds of step. "guest" builds the guest half once, in the Arch box:
# the mingw-w64 programs into build-release/guest/stage, and accel-virt's
# driver tree into build-release/guest/drivers/av-drivers. It is the same
# for every distribution, so every package carries the same files, built
# by the toolchains this project is developed against (Arch's mingw-w64
# links the UCRT; Fedora's and Ubuntu's link msvcrt.dll, which lacks
# functions the agent imports; their clang is another release). A
# distribution target builds the host half on that distribution,
# installing that driver tree rather than building one
# (LSW_AV_PREBUILT_DRIVERS), and adds the mingw-w64 programs to it.
TARGET="${IN_BOX}"
case "${TARGET}" in guest|arch|deb|rpm) ;; *) fail "unknown step '${TARGET}'" ;; esac
WORK="${ROOT}/build-release/${TARGET}"
STAGE="${WORK}/stage"
GUEST="${ROOT}/build-release/guest"

if (( SETUP )); then
    say "${TARGET}: installing the build dependencies"
    if [[ "${TARGET}" == guest ]]; then install_deps arch; else install_deps "${TARGET}"; fi
fi

# ---- flags and compilers ----
# A host's ccache launcher reaches the box through the environment and
# the box has no ccache; the release trees start over anyway.
unset CMAKE_C_COMPILER_LAUNCHER CMAKE_CXX_COMPILER_LAUNCHER
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LTOFLAGS RUSTFLAGS DEBUG_CFLAGS DEBUG_CXXFLAGS
for cc in gcc clang; do
    command -v "${cc}" >/dev/null || fail "${cc} not found (dist.sh --setup ${TARGET})"
done
gcc_march="$(gcc -Q --help=target 2>/dev/null | awk '$1 == "-march=" { print $2 }')"
gcc_mtune="$(gcc -Q --help=target 2>/dev/null | awk '$1 == "-mtune=" { print $2 }')"
[[ "${gcc_march}" == x86-64 && "${gcc_mtune}" == generic ]] \
    || fail "gcc defaults to -march=${gcc_march} -mtune=${gcc_mtune}, not x86-64/generic"
clang_cpu="$(echo | clang -### -x c -c - 2>&1 | grep -o '"-target-cpu" "[^"]*"' | cut -d'"' -f4)"
[[ "${clang_cpu}" == x86-64 ]] || fail "clang defaults to -target-cpu ${clang_cpu}, not x86-64"
if [[ "${TARGET}" == guest ]]; then
    command -v i686-w64-mingw32-g++ >/dev/null \
        || fail "i686-w64-mingw32-g++ not found: a release needs the 32-bit Vulkan loader"
fi

# ---- submodules ----
# Only the missing ones, as scripts/build.sh does: an unconditional
# update would move a submodule someone is working in.
missing=()
for sub in external/remotewin external/accel-virt; do
    [[ -f "${ROOT}/${sub}/CMakeLists.txt" ]] || missing+=("${sub}")
done
if (( ${#missing[@]} )); then
    say "Initialising submodules: ${missing[*]}"
    git -C "${ROOT}" submodule update --init --recursive -- "${missing[@]}"
fi

# ---- build ----
# Every compile command a build generated: CMake's, QEMU's meson, the
# accel-virt guest trees', and the DXVK / vkd3d-proton meson trees,
# which accel-virt keeps in its own build-d3d/.
check_portable() {
    say "Checking the builds for CPU-specific compiler flags"
    local build_files tuned
    mapfile -t build_files < <(find "$@" \( -name build.ninja -o -name compile_commands.json \) \
        -not -path "*/stage/*" -not -path "*/pkg/*")
    tuned="$(grep -hoE -- '-march=[^ "]+|-mtune=[^ "]+|-mcpu=[^ "]+|znver[0-9]+|-mavx[0-9a-z]*|-mfma|-mbmi2?|/arch:AVX[0-9A-Z]*' \
        "${build_files[@]}" | grep -vE -- '^-march=x86-64$|^-mtune=generic$' | sort | uniq -c || true)"
    if [[ -n "${tuned}" ]]; then
        echo "${tuned}" >&2
        fail "CPU-specific flags in the build (above); the release would not run everywhere"
    fi
    echo "    none in ${#build_files[@]} build files"
}

if [[ "${TARGET}" == guest ]]; then
    (( INCREMENTAL )) || rm -rf -- "${WORK}"
    say "Guest trees (agent, remotewin-guest, Vulkan loaders)"
    LSW_AGENT_BUILD="${WORK}/agent" LSW_AGENT32_BUILD="${WORK}/agent32" LSW_REQUIRE_I686=1 \
        "${ROOT}/scripts/build-agent.sh"
    say "Guest drivers (accel-virt's driver tree)"
    drv_args=(-DCMAKE_BUILD_TYPE=Release -DLSW_BUILD_TESTS=OFF)
    [[ -n "${KIT}" ]] && drv_args+=("-DAV_WINDOWS_KIT_DIR=${KIT}")
    cmake -S "${ROOT}" -B "${WORK}/drivers" -G Ninja "${drv_args[@]}"
    cmake --build "${WORK}/drivers" --target accel_virt_drivers
    [[ -f "${WORK}/drivers/av-drivers/provision.ps1" ]] || fail "the driver build left no driver tree"
    check_portable "${WORK}"
    say "Staging into ${STAGE}"
    rm -rf "${STAGE}"
    for tree in agent agent32; do
        DESTDIR="${STAGE}" cmake --install "${WORK}/${tree}" --prefix /usr --component lsw --strip
    done
    install -Dm644 "${WORK}/agent/_deps/vulkanloader-src/LICENSE.txt" \
        "${STAGE}/usr/share/licenses/lsw/vulkan-loader-LICENSE"
    exit 0
fi
[[ -d "${GUEST}/stage/usr" && -f "${GUEST}/drivers/av-drivers/provision.ps1" ]] \
    || fail "no guest half in ${GUEST} (dist.sh builds it first)"

if (( ! INCREMENTAL )); then
    say "Starting the ${TARGET} release trees over"
    rm -rf -- "${WORK}" "${ROOT}/external/accel-virt/build-d3d"
fi

say "Host tree (manager, remotewin, accel-virt) -> ${WORK}/host"
# Link hardening for the host binaries - full RELRO, and no library
# linked that nothing uses. Passed as CMake variables rather than
# LDFLAGS, which every nested build would read, the Windows ones too;
# cmake/LSWAccelVirt.cmake hands them on to accel-virt's host half and
# its DXVK / vkd3d-proton builds.
link_flags="-Wl,-O1 -Wl,--as-needed -Wl,-z,relro -Wl,-z,now"
host_args=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DLSW_BUILD_TESTS=OFF
           "-DCMAKE_EXE_LINKER_FLAGS=${link_flags}"
           "-DCMAKE_SHARED_LINKER_FLAGS=${link_flags}"
           "-DCMAKE_MODULE_LINKER_FLAGS=${link_flags}"
           "-DLSW_AV_PREBUILT_DRIVERS=${GUEST}/drivers/av-drivers")
[[ -n "${KIT}" ]] && host_args+=("-DAV_WINDOWS_KIT_DIR=${KIT}")
cmake -S "${ROOT}" -B "${WORK}/host" -G Ninja "${host_args[@]}"
cmake --build "${WORK}/host"

check_portable "${WORK}" "${ROOT}/external/accel-virt/build-d3d"

# ---- stage ----
say "Staging into ${STAGE}"
rm -rf "${STAGE}"
DESTDIR="${STAGE}" cmake --install "${WORK}/host" --prefix /usr --component lsw --strip
cp -a "${GUEST}/stage/." "${STAGE}/"
# Licenses: LSW's, and those of what the release carries built from
# someone else's source - QEMU and its firmware, DXVK, vkd3d-proton, and
# the Vulkan loader (staged with the guest half) - or as someone else
# released it: virtio-win's viofs driver and virtiofs.exe, and WinFsp's
# installer (the shared folders' guest half, cmake/LSWGuestShares.cmake).
licenses="${STAGE}/usr/share/licenses/lsw"
install -Dm644 "${ROOT}/LICENSE" "${licenses}/LICENSE"
qemu_src="${WORK}/host/accel-virt/qemu/source"
install -Dm644 "${qemu_src}/COPYING"           "${licenses}/qemu-COPYING"
install -Dm644 "${qemu_src}/COPYING.LIB"       "${licenses}/qemu-COPYING.LIB"
install -Dm644 "${qemu_src}/pc-bios/README"    "${licenses}/qemu-firmware-README"
install -Dm644 "${ROOT}/external/accel-virt/third_party/dxvk/LICENSE"         "${licenses}/dxvk-LICENSE"
install -Dm644 "${ROOT}/external/accel-virt/third_party/vkd3d-proton/LICENSE" "${licenses}/vkd3d-proton-LICENSE"
install -Dm644 "${ROOT}/packaging/licenses/virtio-win-LICENSE"                "${licenses}/virtio-win-LICENSE"
install -Dm644 "${ROOT}/packaging/licenses/winfsp-LICENSE"                    "${licenses}/winfsp-LICENSE"
# The shared folders' guest half, whole: without it a share is a device
# Windows has no driver for. The configure that fetched it checked every
# file against its pin.
for f in viofs.inf viofs.sys viofs.cat virtiofs.exe lsw-shares.ps1; do
    [[ -f "${STAGE}/usr/share/lsw/guest/virtiofs/${f}" ]] \
        || fail "the staged guest tree has no virtiofs/${f}"
done
compgen -G "${STAGE}/usr/share/lsw/guest/virtiofs/winfsp-*.msi" >/dev/null \
    || fail "the staged guest tree has no WinFsp installer"
# The install leaves the build machine's GPU names out of the driver
# tree (cmake/LSWAccelVirt.cmake); a release that carried them would
# name every user's adapter after this machine's.
[[ ! -e "${STAGE}/usr/share/lsw/guest/av-drivers/adapters.txt" ]] \
    || fail "the staged driver tree carries the build machine's adapters.txt"
# remotewin's probe is a tool for whoever works on the mirror, built by
# name only; a release that carried it would ship a test rig.
stray="$(find "${STAGE}" -name 'remotewin-probe*' -print -quit)"
[[ -z "${stray}" ]] || fail "the release carries remotewin's test rig: ${stray}"

# The ELF files the install did not strip: --strip covers only what
# CMake installs as targets, and QEMU, its modules, the executor and
# DXVK / vkd3d-proton are files picked out of other projects' build
# trees. None carries debug info; their symbol tables are dead weight.
mapfile -t elfs < <(find "${STAGE}" -type f -exec sh -c 'readelf -h "$1" >/dev/null 2>&1' _ {} \; -print)
strip --strip-unneeded "${elfs[@]}"

# The shared objects the release carries itself (QEMU's modules, the
# executor, DXVK, vkd3d-proton), which no package provides and no
# dependency may name.
# By file and link name and by SONAME, which is what a NEEDED entry says.
mapfile -t bundled < <({ find "${STAGE}" -name '*.so*' \( -type f -o -type l \) -printf '%f\n'
                         for f in "${elfs[@]}"; do
                             readelf -d "${f}" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p'
                         done; } | sort -u)
# Every NEEDED library the release does not carry: what its packages
# depend on.
mapfile -t needed < <(for f in "${elfs[@]}"; do
                          readelf -d "${f}" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
                      done | sort -u | grep -vxF -f <(printf '%s\n' "${bundled[@]}"))
lib_path() {
    ldconfig -p | awk -v lib="$1" '$1 == lib && /x86-64/ && !found { print $NF; found = 1 }'
}
for lib in "${needed[@]}"; do
    [[ -n "$(lib_path "${lib}")" ]] || fail "${lib}, which the release links, is not installed here"
done

mkdir -p "${DIST}"
epoch="$(git -C "${ROOT}" log -1 --format=%ct)"
export SOURCE_DATE_EPOCH="${epoch}"

# ============================================================ arch
# The tarball the AUR's lsw-bin installs, and the packages its ELF files
# link, through pacman, for the PKGBUILD's depends (aur/README.md).
package_arch() {
    local name="lsw-${VERSION}-x86_64"
    local deps="${DIST}/${name}.depends"
    for lib in "${needed[@]}"; do
        pacman -Qoq "$(lib_path "${lib}")"
    done | sort -u > "${deps}"
    # Reproducible as far as tar goes: sorted, owned by root, every mtime
    # the commit's, so the same tree packs to the same bytes.
    local tarball="${DIST}/${name}.tar.zst"
    say "Packing ${tarball}"
    tar -C "${STAGE}" --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime="@${epoch}" -cf - usr | zstd -q -19 -T0 -f -o "${tarball}"
    (cd "${DIST}" && sha256sum "${name}.tar.zst" > "${name}.tar.zst.sha256")
    echo "  ${tarball} ($(du -h "${tarball}" | cut -f1))"
    echo "  sha256 $(cut -d' ' -f1 "${tarball}.sha256")"
    echo "  depends: $(tr '\n' ' ' < "${deps}")"
}

# ============================================================ deb
# packaging/deb/control.in, with the libraries' packages and versions
# from dpkg-shlibdeps (the symbols files of what is installed here).
package_deb() {
    local root="${WORK}/pkg"
    local out="${DIST}/lsw_${VERSION}-1_amd64.deb"
    rm -rf "${root}"
    cp -a "${STAGE}" "${root}"
    # Debian keeps a package's license texts with its documentation.
    mkdir -p "${root}/usr/share/doc"
    mv "${root}/usr/share/licenses/lsw" "${root}/usr/share/doc/lsw"
    rmdir --ignore-fail-on-non-empty "${root}/usr/share/licenses"
    cp "${ROOT}/LICENSE" "${root}/usr/share/doc/lsw/copyright"
    # dpkg-shlibdeps reads debian/control from where it runs, and finds
    # the carried libraries through -l.
    local scratch="${WORK}/shlibdeps"
    rm -rf "${scratch}"
    mkdir -p "${scratch}/debian"
    printf 'Source: lsw\n\nPackage: lsw\nArchitecture: amd64\n' > "${scratch}/debian/control"
    local libdirs=()
    mapfile -t libdirs < <(for f in "${elfs[@]}"; do
                               [[ "${f}" == *.so* ]] && dirname "${f/#"${STAGE}"/${root}}"
                           done | sort -u)
    local shlibs pkg_elfs=("${elfs[@]/#"${STAGE}"/${root}}")
    if ! shlibs="$(cd "${scratch}" && dpkg-shlibdeps -O --ignore-missing-info \
                       "${libdirs[@]/#/-l}" "${pkg_elfs[@]}" 2>"${scratch}/warnings")"; then
        cat "${scratch}/warnings" >&2
        fail "dpkg-shlibdeps failed (above)"
    fi
    grep -v "warning: package could avoid a useless dependency" "${scratch}/warnings" >&2 || true
    shlibs="$(sed -n 's/^shlibs:Depends=//p' <<< "${shlibs}")"
    [[ -n "${shlibs}" ]] || fail "dpkg-shlibdeps found no dependencies"
    mkdir -p "${root}/DEBIAN"
    sed -e "s/@VERSION@/${VERSION}-1/" -e "s/@SHLIBS@/${shlibs}/" \
        -e "s/@SIZE@/$(du -sk --exclude=DEBIAN "${root}" | cut -f1)/" \
        "${ROOT}/packaging/deb/control.in" > "${root}/DEBIAN/control"
    install -m755 "${ROOT}/packaging/deb/postinst" "${root}/DEBIAN/postinst"
    find "${root}" -exec touch -h -d "@${epoch}" {} +
    say "Packing ${out}"
    dpkg-deb --root-owner-group -Zxz --build "${root}" "${out}" >/dev/null
    echo "  ${out} ($(du -h "${out}" | cut -f1))"
    echo "  depends: $(dpkg-deb -f "${out}" Depends)"
}

# ============================================================ rpm
# packaging/rpm/lsw.spec.in. rpmbuild finds the libraries' sonames
# itself; the ones the release carries are kept out of its requires and
# provides.
package_rpm() {
    local top="${WORK}/rpmbuild"
    rm -rf "${top}"
    mkdir -p "${top}/SPECS"
    local excl
    excl="$(printf '%s\n' "${bundled[@]}" | sed 's/[.+]/\\&/g' | paste -sd'|')"
    # Substituted by bash, not sed: the pattern is full of sed's
    # delimiters and escapes.
    local spec
    spec="$(<"${ROOT}/packaging/rpm/lsw.spec.in")"
    spec="${spec//@VERSION@/${VERSION}}"
    printf '%s\n' "${spec//@BUNDLED@/${excl}}" > "${top}/SPECS/lsw.spec"
    say "Building the rpm"
    rpmbuild -bb --quiet --define "_topdir ${top}" --define "_stage ${STAGE}" \
        --define "source_date_epoch_from_changelog 0" \
        --define "use_source_date_epoch_as_buildtime 1" \
        --define "clamp_mtime_to_source_date_epoch 1" \
        "${top}/SPECS/lsw.spec"
    local rpm
    rpm="$(find "${top}/RPMS" -name "lsw-${VERSION}-1.*.x86_64.rpm" -print -quit)"
    [[ -n "${rpm}" ]] || fail "rpmbuild wrote no package"
    cp "${rpm}" "${DIST}/"
    rpm="${DIST}/$(basename "${rpm}")"
    echo "  ${rpm} ($(du -h "${rpm}" | cut -f1))"
    echo "  requires: $(rpm -qp --requires "${rpm}" 2>/dev/null | grep -v '^rpmlib(' | tr '\n' ' ')"
}

"package_${TARGET}"
