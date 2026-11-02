#!/usr/bin/env bash

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

DEPS_ARCH=(
    base-devel git file which findutils tar patch ca-certificates cmake ninja python meson glslang clang lld llvm sdl3 wine mingw-w64-gcc
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
    build-essential dpkg-dev fakeroot git file findutils tar patch ca-certificates cmake ninja-build python3 python3-venv meson
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
    gcc gcc-c++ make git file which findutils tar patch ca-certificates cmake ninja-build python3 meson rpm-build
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

install_deps() {
    local su=()
    (( EUID == 0 )) || su=(sudo)
    case "$1" in
        arch) "${su[@]}" pacman -Syu --needed --noconfirm "${DEPS_ARCH[@]}" ;;
        deb)
            "${su[@]}" apt-get update
            "${su[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${DEPS_DEB[@]}"
            ;;
        rpm) "${su[@]}" dnf -y install --setopt=install_weak_deps=False "${DEPS_RPM[@]}" ;;
    esac
}

usage() {
    cat <<'EOF'
usage: scripts/dist.sh [--version V] [--kit DIR] [--setup] [--incremental] [TARGET...]

Builds the release packages, each in a distrobox of its distribution.
TARGET is arch, deb, rpm or all (the default).

  --version V    release version (default: the project version in CMakeLists.txt)
  --kit DIR      an already fetched accel-virt Windows kit
  --setup        install or update the build dependencies in each distrobox
  --incremental  reuse the release trees of the last run
  --deps TARGET  only install the build dependencies of arch, deb or rpm here

The distroboxes are named by LSW_BOX_ARCH, LSW_BOX_DEB and LSW_BOX_RPM.
Packages are written to dist/.

The release workflow runs the steps itself, in containers:
  scripts/dist.sh --in-box guest|arch|deb|rpm [--setup] [--version V]
EOF
}


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
        --deps)        install_deps "$2"; exit 0 ;;
        -h|--help)     usage; exit 0 ;;
        all)           targets+=("${TARGETS_ALL[@]}"); shift ;;
        arch|deb|rpm)  targets+=("$1"); shift ;;
        *)             fail "unknown argument '$1' (--help)" ;;
    esac
done
if [[ -z "${VERSION}" ]]; then
    VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "${ROOT}/CMakeLists.txt" | head -1)"
fi
[[ -n "${VERSION}" ]] || fail "no version (pass --version)"

if [[ -z "${IN_BOX}" ]]; then
    command -v distrobox >/dev/null || fail "distrobox not found"
    (( ${#targets[@]} )) || targets=("${TARGETS_ALL[@]}")
    declare -A seen=()
    order=()
    for t in "${targets[@]}"; do
        [[ -n "${seen[$t]:-}" ]] || { order+=("$t"); seen[$t]=1; }
    done
    if [[ -n "$(git -C "${ROOT}" status --porcelain --untracked-files=no --ignore-submodules=dirty)" ]]; then
        echo "!!! the working tree has uncommitted changes; the release will contain them" >&2
    fi
    cpus="$(taskset -pc $$ | sed 's/.*: //')"
    pass=(--version "${VERSION}")
    [[ -n "${KIT}" ]] && pass+=(--kit "${KIT}")
    (( INCREMENTAL )) && pass+=(--incremental)
    declare -A set_up=()
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

TARGET="${IN_BOX}"
case "${TARGET}" in guest|arch|deb|rpm) ;; *) fail "unknown step '${TARGET}'" ;; esac
WORK="${ROOT}/build-release/${TARGET}"
STAGE="${WORK}/stage"
GUEST="${ROOT}/build-release/guest"

if (( SETUP )); then
    say "${TARGET}: installing the build dependencies"
    if [[ "${TARGET}" == guest ]]; then install_deps arch; else install_deps "${TARGET}"; fi
fi

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

missing=()
for sub in external/remotewin external/accel-virt; do
    [[ -f "${ROOT}/${sub}/CMakeLists.txt" ]] || missing+=("${sub}")
done
if (( ${#missing[@]} )); then
    say "Initialising submodules: ${missing[*]}"
    git -C "${ROOT}" submodule update --init --recursive -- "${missing[@]}"
fi

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

say "Staging into ${STAGE}"
rm -rf "${STAGE}"
DESTDIR="${STAGE}" cmake --install "${WORK}/host" --prefix /usr --component lsw --strip
cp -a "${GUEST}/stage/." "${STAGE}/"
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
for f in viofs.inf viofs.sys viofs.cat virtiofs.exe lsw-shares.ps1; do
    [[ -f "${STAGE}/usr/share/lsw/guest/virtiofs/${f}" ]] \
        || fail "the staged guest tree has no virtiofs/${f}"
done
compgen -G "${STAGE}/usr/share/lsw/guest/virtiofs/winfsp-*.msi" >/dev/null \
    || fail "the staged guest tree has no WinFsp installer"
[[ ! -e "${STAGE}/usr/share/lsw/guest/av-drivers/adapters.txt" ]] \
    || fail "the staged driver tree carries the build machine's adapters.txt"
stray="$(find "${STAGE}" -name 'remotewin-probe*' -print -quit)"
[[ -z "${stray}" ]] || fail "the release carries remotewin's test rig: ${stray}"

mapfile -t elfs < <(find "${STAGE}" -type f -exec sh -c 'readelf -h "$1" >/dev/null 2>&1' _ {} \; -print)
strip --strip-unneeded "${elfs[@]}"

mapfile -t bundled < <({ find "${STAGE}" -name '*.so*' \( -type f -o -type l \) -printf '%f\n'
                         for f in "${elfs[@]}"; do
                             readelf -d "${f}" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p'
                         done; } | sort -u)
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

package_arch() {
    local name="lsw-${VERSION}-x86_64"
    local deps="${DIST}/${name}.depends"
    for lib in "${needed[@]}"; do
        pacman -Qoq "$(lib_path "${lib}")"
    done | sort -u > "${deps}"
    local tarball="${DIST}/${name}.tar.zst"
    say "Packing ${tarball}"
    tar -C "${STAGE}" --sort=name --owner=0 --group=0 --numeric-owner \
        --mtime="@${epoch}" -cf - usr | zstd -q -19 -T0 -f -o "${tarball}"
    (cd "${DIST}" && sha256sum "${name}.tar.zst" > "${name}.tar.zst.sha256")
    echo "  ${tarball} ($(du -h "${tarball}" | cut -f1))"
    echo "  sha256 $(cut -d' ' -f1 "${tarball}.sha256")"
    echo "  depends: $(tr '\n' ' ' < "${deps}")"
}

package_deb() {
    local root="${WORK}/pkg"
    local out="${DIST}/lsw_${VERSION}-1_amd64.deb"
    rm -rf "${root}"
    cp -a "${STAGE}" "${root}"
    mkdir -p "${root}/usr/share/doc"
    mv "${root}/usr/share/licenses/lsw" "${root}/usr/share/doc/lsw"
    rmdir --ignore-fail-on-non-empty "${root}/usr/share/licenses"
    cp "${ROOT}/LICENSE" "${root}/usr/share/doc/lsw/copyright"
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

package_rpm() {
    local top="${WORK}/rpmbuild"
    rm -rf "${top}"
    mkdir -p "${top}/SPECS"
    local excl
    excl="$(printf '%s\n' "${bundled[@]}" | sed 's/[.+]/\\&/g' | paste -sd'|')"
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
