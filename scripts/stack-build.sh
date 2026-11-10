#!/usr/bin/env bash

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAIN_TREE="${LSW_MAIN_TREE:-$(git -C "$(dirname "$0")" worktree list --porcelain | sed -n "1s/^worktree //p")}"
DEV_STACK="${LSW_RENDER_DRIVER_DIR:-${XDG_DATA_HOME:-${HOME}/.local/share}/lsw-dev/render-driver}"
GUEST_ORIG="${LSW_GUEST_AV_ORIG:-${DEV_STACK}/state/guest-av-orig}"
BUILD_LOCK="${XDG_RUNTIME_DIR:-/tmp}/lsw-build.lock"
MARCH="${LSW_STACK_MARCH:-x86-64-v3}"
QEMU_BUILD="${MAIN_TREE}/build/accel-virt/qemu/qemu"
MAIN_AV="${MAIN_TREE}/external/accel-virt"

usage() {
    cat <<'EOT'
usage: scripts/stack-build.sh [--dry-run] DRIVER_WORKTREE OUT_DIR

Builds the complete render stack of one accel-virt worktree into OUT_DIR, so a verification deploys one thing:

  libav_executor.so   built for x86-64-v3, runs on this machine and on accel
  av-drivers/         guest drivers x64 and x32 as the VM installs them (package/, loaders/, provision.ps1)
  d3dlib/             DXVK and the patched vkd3d-proton, cached by the content of the patches
  qemu-modules/       the QEMU modules of this machine's QEMU build, with virtio-gpu-av and virtio-gpu-pci-av
                      compiled from the worktree's glue and device library (hybrid capable)
  stage/              what gpu-test.sh deploy-render wants besides the stack (QEMU binary, agent, vulkan loaders)
  manifest.json       driver sha, suite sha (this tree), md5 of the executor and the two modules

The worktree must be clean (LSW_STACK_ALLOW_DIRTY=1 builds it anyway and records it). The build holds
${XDG_RUNTIME_DIR}/lsw-build.lock and runs with CPUQuota=400%, nice and ionice, -j4.
EOT
}

dry=0
args=()
for a in "$@"; do
    case "$a" in
        --dry-run) dry=1 ;;
        -h|--help) usage; exit 0 ;;
        *) args+=("$a") ;;
    esac
done
[[ ${#args[@]} -eq 2 ]] || { usage >&2; exit 2; }
WT="$(cd "${args[0]}" && pwd)"
OUT="$(mkdir -p "${args[1]}" && cd "${args[1]}" && pwd)"

die() { printf 'stack-build: %s\n' "$*" >&2; exit 1; }
say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
step() { if [[ ${dry} -eq 1 ]]; then printf '[dry-run] %s\n' "$1"; return 1; fi; say "$1"; }

git -C "${WT}" rev-parse --git-dir >/dev/null 2>&1 || die "${WT} is not a git worktree"
DRIVER_SHA="$(git -C "${WT}" rev-parse HEAD)"
SUITE_SHA="$(git -C "${ROOT}" rev-parse HEAD)"
DRIVER_DIRTY=0
if [[ -n $(git -C "${WT}" status --porcelain --untracked-files=no | grep -v ' third_party/' || true) ]]; then
    DRIVER_DIRTY=1
    [[ ${LSW_STACK_ALLOW_DIRTY:-0} == 1 ]] || die "${WT} has uncommitted changes; commit them or set LSW_STACK_ALLOW_DIRTY=1"
fi

if [[ ${dry} -eq 0 && ${LSW_STACK_INNER:-0} != 1 ]]; then
    exec flock -w 7200 "${BUILD_LOCK}" systemd-run --user --scope -q -p CPUQuota=400% -p MemoryHigh=6G \
        nice -n 19 ionice -c3 env LSW_STACK_INNER=1 "$0" "$@"
fi

printf 'driver %s%s\nsuite  %s\nout    %s\nmarch  %s\n' "${DRIVER_SHA}" "$([[ ${DRIVER_DIRTY} -eq 1 ]] && echo ' (dirty)')" "${SUITE_SHA}" "${OUT}" "${MARCH}"

link_third_party() {
    local name
    for name in .cache dxc dxvk khronos opencl-loader qemu vcredist vkd3d-proton vulkan-runtime vulkan-samples windows-kit; do
        if [[ -d ${WT}/third_party/${name} && ! -L ${WT}/third_party/${name} && -z $(ls -A "${WT}/third_party/${name}") ]]; then
            rmdir "${WT}/third_party/${name}"
        fi
        [[ -e ${WT}/third_party/${name} ]] || ln -s "${MAIN_AV}/third_party/${name}" "${WT}/third_party/${name}"
    done
}

build_native() {
    local dir="${WT}/build-stack" launcher=()
    command -v ccache >/dev/null 2>&1 && launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
    cmake -S "${WT}" -B "${dir}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DAV_SANITIZE=OFF -DAV_CHECKED=OFF -DAV_QEMU=OFF \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_C_FLAGS="-march=${MARCH}" -DCMAKE_CXX_FLAGS="-march=${MARCH}" "${launcher[@]}" >/dev/null
    cmake --build "${dir}" -j4 --target av_executor av_device drivers
}

install_package() {
    rm -rf "${OUT}/av-drivers"
    cmake --install "${WT}/build-stack" --component drivers --prefix "${OUT}/av-drivers" >/dev/null
    [[ -d ${GUEST_ORIG}/loaders && -f ${GUEST_ORIG}/package/avgpu.inf ]] || die "${GUEST_ORIG} has no loaders/ and package/avgpu.inf"
    cp -a "${GUEST_ORIG}/loaders" "${OUT}/av-drivers/loaders"
    cp -f "${GUEST_ORIG}/package/avgpu.inf" "${OUT}/av-drivers/package/"
    (cd "${OUT}/av-drivers/package" && for f in $(ls | grep -v '^package.sha256$' | sort); do sha256sum "$f"; done > package.sha256)
}

build_d3dlib() {
    local key cache scratch sha
    for sub in dxvk vkd3d-proton; do
        sha="$(git -C "${WT}" ls-tree HEAD "third_party/${sub}" | awk '{print $3}')"
        [[ "$(git -C "${MAIN_AV}/third_party/${sub}" rev-parse HEAD)" == "${sha}" ]] ||
            die "${MAIN_AV}/third_party/${sub} is not at the ${sha} this driver pins"
        key+="${sha}"
    done
    key="$({ cat "${WT}"/tools/dxvk-patches/*.patch "${WT}"/tools/vkd3d-proton-patches/*.patch "${WT}"/tools/dxil-spirv-patches/*.patch \
        "${WT}/tools/build_d3d_runtime.sh"; printf "%s" "${key}"; } | md5sum | cut -c1-16)"
    cache="${XDG_CACHE_HOME:-${HOME}/.cache}/lsw-stack/d3dlib-${key}"
    if [[ ! -f ${cache}/libvkd3d-proton-d3d12.so ]]; then
        scratch="${WT}/build-d3d-scratch"
        rm -rf "${scratch}"
        mkdir -p "${scratch}/tools" "${scratch}/third_party"
        cp -a "${WT}"/tools/*-patches "${scratch}/tools/"
        grep -v 'submodule update' "${WT}/tools/build_d3d_runtime.sh" > "${scratch}/tools/build_d3d_runtime.sh"
        chmod +x "${scratch}/tools/build_d3d_runtime.sh"
        for sub in dxvk vkd3d-proton; do
            mkdir -p "${scratch}/third_party/${sub}"
            git -C "${MAIN_AV}/third_party/${sub}" archive HEAD | tar -x -C "${scratch}/third_party/${sub}"
            git -C "${MAIN_AV}/third_party/${sub}" submodule foreach --quiet --recursive \
                "mkdir -p '${scratch}/third_party/${sub}/'\"\$displaypath\" && git archive HEAD | tar -x -C '${scratch}/third_party/${sub}/'\"\$displaypath\""
        done
        for repo in third_party/vkd3d-proton/subprojects/dxil-spirv third_party/dxvk third_party/vkd3d-proton; do
            git -C "${scratch}/${repo}" init -q
            git -C "${scratch}/${repo}" add -A >/dev/null 2>&1
            git -C "${scratch}/${repo}" -c user.name=stack -c user.email=stack@localhost commit -qm base
        done
        CC="${CC:-ccache gcc}" CXX="${CXX:-ccache g++}" JOBS=4 "${scratch}/tools/build_d3d_runtime.sh" >/dev/null
        mkdir -p "${cache}.new"
        cp -aL "${scratch}/build-d3d/lib/." "${cache}.new/"
        rm -rf "${cache}"
        mv "${cache}.new" "${cache}"
    fi
    rm -rf "${OUT}/d3dlib"
    cp -aL "${cache}" "${OUT}/d3dlib"
}

build_modules() {
    local work obj c
    work="$(mktemp -d)"
    obj="$(mktemp -d)"
    mkdir -p "${work}/mods" "${obj}/inc/hw/virtio"
    install -m 644 "${WT}/src/device/qemu/virtio-gpu-av.h" "${obj}/inc/hw/virtio/virtio-gpu-av.h"
    (
        cd "${QEMU_BUILD}"
        ninja -t commands hw-display-virtio-gpu-av.so hw-display-virtio-gpu-pci-av.so | grep -E "virtio-gpu-av|virtio-gpu-pci-av" | sort -u |
            while IFS= read -r c; do
                c=${c//${MAIN_TREE}\/build\/accel-virt\/qemu\/libav_device.a/${WT}/build-stack/libav_device.a}
                c=${c//${MAIN_AV}\/src\/device\/abi/${WT}/src/device/abi}
                c=${c//..\/source\/hw\/display\/virtio-gpu-/${WT}/src/device/qemu/virtio-gpu-}
                c=${c//libhw-display-virtio-gpu-av.a.p\//${obj}/}
                c=${c//libhw-display-virtio-gpu-pci-av.a.p\//${obj}/}
                c=${c//-o hw-display-virtio-gpu-av.so/-o ${work}/mods/hw-display-virtio-gpu-av.so}
                c=${c//-o hw-display-virtio-gpu-pci-av.so/-o ${work}/mods/hw-display-virtio-gpu-pci-av.so}
                c=${c// -I..\/source/ -iquote ${obj}/inc -I..\/source}
                case $c in *" -c "*) eval "$c" ;; esac
                case $c in *"-shared"*) eval "$c" ;; esac
            done
    )
    [[ -f ${work}/mods/hw-display-virtio-gpu-av.so && -f ${work}/mods/hw-display-virtio-gpu-pci-av.so ]] || die "the QEMU modules were not built"
    rm -rf "${OUT}/qemu-modules"
    mkdir -p "${OUT}/qemu-modules"
    find "${QEMU_BUILD}" -maxdepth 1 -name '*.so' -exec cp -L {} "${OUT}/qemu-modules/" \;
    cp -f "${work}/mods"/*.so "${OUT}/qemu-modules/"
    rm -rf "${work}" "${obj}"
    [[ $(strings -a "${OUT}/qemu-modules/hw-display-virtio-gpu-av.so" | grep -c offer_hybrid) -gt 0 ]] || die "the built module has no offer_hybrid: the worktree's glue lacks hybrid"
}

stage_deploy_files() {
    local rel
    rm -rf "${OUT}/stage"
    for rel in build/accel-virt/qemu/qemu/qemu-system-x86_64 build/accel-virt/qemu/qemu/qemu-bundle/usr/local/share/qemu \
               build-agent/src/agent/lsw-guest-agent.exe build-agent/_deps/vulkanloader-build/loader/vulkan-1.dll \
               build-agent32/_deps/vulkanloader-build/loader/vulkan-1.dll; do
        [[ -e ${MAIN_TREE}/${rel} ]] || die "${MAIN_TREE}/${rel} is missing"
        mkdir -p "${OUT}/stage/$(dirname "${rel}")"
        cp -aL "${MAIN_TREE}/${rel}" "${OUT}/stage/${rel}"
    done
    mkdir -p "${OUT}/stage/external/accel-virt/third_party"
    cp -aL "${WT}/tools" "${OUT}/stage/external/accel-virt/tools"
    cp -aL "${MAIN_AV}/third_party/dxc" "${OUT}/stage/external/accel-virt/third_party/dxc"
}

step "link the shared third_party sources into ${WT}" && link_third_party
step "build av_executor (${MARCH}), av_device and the guest drivers x64+x32 in ${WT}/build-stack" && build_native
step "copy libav_executor.so and install the driver package" && { cp -f "${WT}/build-stack/libav_executor.so" "${OUT}/libav_executor.so"; install_package; }
step "d3dlib (cached by the patches' content)" && build_d3dlib
step "QEMU modules with the worktree's glue" && build_modules
step "stage the QEMU binary and agent files deploy-render ships" && stage_deploy_files
if step "write ${OUT}/manifest.json"; then
    python3 -B "${ROOT}/scripts/stack_manifest.py" make --stack "${OUT}" --driver-sha "${DRIVER_SHA}" --suite-sha "${SUITE_SHA}" \
        --extra stage/build/accel-virt/qemu/qemu/qemu-system-x86_64 $([[ ${DRIVER_DIRTY} -eq 1 ]] && echo --driver-dirty)
    cat "${OUT}/manifest.json"
fi
