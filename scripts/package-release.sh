#!/usr/bin/env bash
# Build a binary release: everything LSW installs, from all three build
# trees, as one tarball whose root is the usr/ tree.
#
#   scripts/package-release.sh [--version V] [--kit DIR] [--incremental]
#
#   --version V    the release version (default: project(LSW VERSION) in
#                  CMakeLists.txt). The tarball, the tag (v<V>) and the
#                  AUR package's pkgver all carry it.
#   --kit DIR      an already-fetched accel-virt Windows kit
#                  (AV_WINDOWS_KIT_DIR), to skip fetching it again
#   --incremental  reuse the release trees from the last run instead of
#                  starting them over: for iterating on this script, not
#                  for a release
#
# Writes dist/lsw-<V>-x86_64.tar.zst, its .sha256, and
# dist/lsw-<V>-x86_64.depends: the packages its ELF files link, for the
# AUR package's depends (aur/README.md).
#
# The trees are its own, under build-release/ (gitignored), so a
# developer's build/ and build-agent*/ — configured against whatever
# this machine's libraries are — are neither used nor touched, and they
# start over on every release run, since a tree configured on another
# system (the host, when this runs in a container sharing $HOME) would
# be reused as it is. The one tree outside build-release/ is accel-virt's
# own build-d3d/, where tools/build_d3d_runtime.sh always builds DXVK and
# vkd3d-proton and has no switch to build anywhere else; it is started
# over too. A developer's build keeps its own copy of those libraries
# (build/accel-virt/d3d/lib) and is unaffected until its next build,
# which then reuses these portable ones.
# What accel-virt fetches — the Windows kit subset, the Khronos
# registries, QEMU's tarball, the OpenCL loader's source, its submodules
# — the host build fetches as it always does (accel_virt_fetch), so this
# needs the network the first time and repeats no download after.
#
# Portability. Whatever is built here runs on other people's machines,
# so nothing may be tuned for this one. The compiler flags in the
# environment are dropped — makepkg.conf's on a distribution that sets
# -march=native, a developer's own — and the compilers must target the
# generic x86-64 baseline by default, which is checked, not assumed:
# accel-virt's QEMU configure and its DXVK / vkd3d-proton meson builds
# take their flags from the environment and the compiler's defaults,
# and there is no one place to pass a -march to all of them. After the
# build every generated build file is searched for a CPU-specific flag.
# The flags that remain are the projects' own baselines (QEMU's -mcx16,
# DXVK's -msse3), which upstream ships with everywhere.
#
# Library versions are the other half: the binaries link against the
# libraries installed where this runs. Cut releases in a clean Arch
# chroot so they link against Arch's own packages — aur/README.md has
# the commands.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${ROOT}/build-release"
DIST="${ROOT}/dist"
STAGE="${WORK}/stage"

VERSION=""
KIT=""
INCREMENTAL=0
while (( $# )); do
    case "$1" in
        --version)     VERSION="$2"; shift 2 ;;
        --kit)         KIT="$2"; shift 2 ;;
        --incremental) INCREMENTAL=1; shift ;;
        -h|--help) sed -n '2,/^$/p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)         echo "package-release: unknown argument '$1'" >&2; exit 2 ;;
    esac
done
if [[ -z "${VERSION}" ]]; then
    VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "${ROOT}/CMakeLists.txt" | head -1)"
fi
[[ -n "${VERSION}" ]] || { echo "package-release: no version (pass --version)" >&2; exit 2; }
NAME="lsw-${VERSION}-x86_64"

say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
fail() { echo "!!! $*" >&2; exit 1; }

# ---- flags and compilers ----
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS LTOFLAGS RUSTFLAGS DEBUG_CFLAGS DEBUG_CXXFLAGS
for cc in gcc clang; do
    command -v "${cc}" >/dev/null || fail "${cc} not found"
done
gcc_march="$(gcc -Q --help=target 2>/dev/null | awk '$1 == "-march=" { print $2 }')"
gcc_mtune="$(gcc -Q --help=target 2>/dev/null | awk '$1 == "-mtune=" { print $2 }')"
[[ "${gcc_march}" == x86-64 && "${gcc_mtune}" == generic ]] \
    || fail "gcc defaults to -march=${gcc_march} -mtune=${gcc_mtune}, not x86-64/generic"
clang_cpu="$(echo | clang -### -x c -c - 2>&1 | grep -o '"-target-cpu" "[^"]*"' | cut -d'"' -f4)"
[[ "${clang_cpu}" == x86-64 ]] || fail "clang defaults to -target-cpu ${clang_cpu}, not x86-64"
command -v i686-w64-mingw32-g++ >/dev/null \
    || fail "i686-w64-mingw32-g++ not found: a release needs the 32-bit Vulkan loader"

if [[ -n "$(git -C "${ROOT}" status --porcelain --ignore-submodules=dirty 2>/dev/null)" ]]; then
    echo "!!! the working tree has uncommitted changes; the release will contain them" >&2
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
if (( ! INCREMENTAL )); then
    say "Starting the release trees over"
    rm -rf -- "${WORK}" "${ROOT}/external/accel-virt/build-d3d"
fi

say "Host tree (manager, remotewin, accel-virt) -> ${WORK}/host"
# Arch's own link hardening for the host binaries — full RELRO, and no
# library linked that nothing uses. Passed as CMake variables rather than
# LDFLAGS, which every nested build would read, the Windows ones too;
# cmake/LSWAccelVirt.cmake hands them on to accel-virt's host half and
# its DXVK / vkd3d-proton builds.
link_flags="-Wl,-O1 -Wl,--as-needed -Wl,-z,relro -Wl,-z,now"
host_args=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DLSW_BUILD_TESTS=OFF
           "-DCMAKE_EXE_LINKER_FLAGS=${link_flags}"
           "-DCMAKE_SHARED_LINKER_FLAGS=${link_flags}"
           "-DCMAKE_MODULE_LINKER_FLAGS=${link_flags}")
[[ -n "${KIT}" ]] && host_args+=("-DAV_WINDOWS_KIT_DIR=${KIT}")
cmake -S "${ROOT}" -B "${WORK}/host" -G Ninja "${host_args[@]}"
cmake --build "${WORK}/host"

say "Guest trees (agent, remotewin-guest, Vulkan loaders)"
LSW_AGENT_BUILD="${WORK}/agent" LSW_AGENT32_BUILD="${WORK}/agent32" LSW_REQUIRE_I686=1 \
    "${ROOT}/scripts/build-agent.sh"

# ---- portability check ----
# Every compile command any of the builds generated: CMake's, QEMU's
# meson, the accel-virt guest trees', and the DXVK / vkd3d-proton meson
# trees, which accel-virt keeps in its own build-d3d/.
say "Checking the builds for CPU-specific compiler flags"
mapfile -t build_files < <(find "${WORK}" "${ROOT}/external/accel-virt/build-d3d" \
    \( -name build.ninja -o -name compile_commands.json \) -not -path "*/stage/*")
tuned="$(grep -hoE -- '-march=[^ "]+|-mtune=[^ "]+|-mcpu=[^ "]+|znver[0-9]+|-mavx[0-9a-z]*|-mfma|-mbmi2?|/arch:AVX[0-9A-Z]*' \
    "${build_files[@]}" | grep -vE -- '^-march=x86-64$|^-mtune=generic$' | sort | uniq -c || true)"
if [[ -n "${tuned}" ]]; then
    echo "${tuned}" >&2
    fail "CPU-specific flags in the build (above); the release would not run everywhere"
fi
echo "    none in ${#build_files[@]} build files"

# ---- stage ----
say "Staging into ${STAGE}"
rm -rf "${STAGE}"
for tree in host agent agent32; do
    DESTDIR="${STAGE}" cmake --install "${WORK}/${tree}" --prefix /usr --component lsw --strip
done
# Licenses: LSW's, and those of what the tarball carries built from
# someone else's source — QEMU and its firmware, DXVK, vkd3d-proton, the
# Vulkan loader.
licenses="${STAGE}/usr/share/licenses/lsw"
install -Dm644 "${ROOT}/LICENSE" "${licenses}/LICENSE"
qemu_src="${WORK}/host/accel-virt/qemu/source"
install -Dm644 "${qemu_src}/COPYING"           "${licenses}/qemu-COPYING"
install -Dm644 "${qemu_src}/COPYING.LIB"       "${licenses}/qemu-COPYING.LIB"
install -Dm644 "${qemu_src}/pc-bios/README"    "${licenses}/qemu-firmware-README"
install -Dm644 "${ROOT}/external/accel-virt/third_party/dxvk/LICENSE"         "${licenses}/dxvk-LICENSE"
install -Dm644 "${ROOT}/external/accel-virt/third_party/vkd3d-proton/LICENSE" "${licenses}/vkd3d-proton-LICENSE"
install -Dm644 "${WORK}/agent/_deps/vulkanloader-src/LICENSE.txt"             "${licenses}/vulkan-loader-LICENSE"
# The install leaves the build machine's GPU names out of the driver
# tree (cmake/LSWAccelVirt.cmake); a release that carried them would
# name every user's adapter after this machine's.
[[ ! -e "${STAGE}/usr/share/lsw/guest/av-drivers/adapters.txt" ]] \
    || fail "the staged driver tree carries the build machine's adapters.txt"

# The ELF files the install did not strip: --strip covers only what
# CMake installs as targets, and QEMU, its modules, the executor and
# DXVK / vkd3d-proton are files picked out of other projects' build
# trees. None carries debug info; their symbol tables are dead weight.
mapfile -t elfs < <(find "${STAGE}" -type f -exec sh -c 'readelf -h "$1" >/dev/null 2>&1' _ {} \; -print)
strip --strip-unneeded "${elfs[@]}"

# ---- dependencies ----
# The packages the shipped ELF files link, for the AUR package's
# depends: every NEEDED library the tarball does not carry itself,
# resolved through the dynamic linker's cache to its file and through
# pacman to the package that owns it. Run in the release container, the
# names are Arch's.
deps="${DIST}/${NAME}.depends"
mkdir -p "${DIST}"
if command -v pacman >/dev/null; then
    for lib in $(for f in "${elfs[@]}"; do
                     readelf -d "${f}" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
                 done | sort -u); do
        [[ -n "$(find "${STAGE}" -name "${lib}" -print -quit)" ]] && continue
        path="$(ldconfig -p | awk -v lib="${lib}" '$1 == lib && /x86-64/ && !found { print $NF; found = 1 }')"
        [[ -n "${path}" ]] || fail "${lib}, which the release links, is not installed here"
        pacman -Qoq "${path}"
    done | sort -u > "${deps}"
else
    echo "!!! no pacman: ${deps} not written" >&2
fi

# ---- tarball ----
# Reproducible as far as tar goes: sorted, owned by root, every mtime
# the commit's, so the same tree packs to the same bytes.
mkdir -p "${DIST}"
epoch="$(git -C "${ROOT}" log -1 --format=%ct)"
tarball="${DIST}/${NAME}.tar.zst"
say "Packing ${tarball}"
tar -C "${STAGE}" --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@${epoch}" -cf - usr | zstd -q -19 -T0 -f -o "${tarball}"
(cd "${DIST}" && sha256sum "${NAME}.tar.zst" > "${NAME}.tar.zst.sha256")

say "Done."
echo "  ${tarball} ($(du -h "${tarball}" | cut -f1))"
echo "  sha256 $(cut -d' ' -f1 "${tarball}.sha256")"
[[ -f "${deps}" ]] && echo "  depends: $(tr '\n' ' ' < "${deps}")"
