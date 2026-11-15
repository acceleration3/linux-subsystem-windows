#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
usage: scripts/verify-package.sh arch|deb|rpm DIST_DIR

Run as root in a clean container of the package's distribution. Installs the
package from DIST_DIR beside the distribution's own QEMU and libvirt, then
checks that the packaged QEMU answers what libvirt asks before it defines a VM.
EOF
    exit 2
}

fail() { echo "!!! verify-package: $*" >&2; exit 1; }
say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }

(( $# == 2 )) || usage
TARGET="$1"
DIST="$(cd "$2" && pwd)"
(( EUID == 0 )) || fail "run as root in a throwaway container"

QEMU=/usr/bin/lsw-qemu-system-x86_64
TREE=/usr/lib/lsw/accel-virt/qemu

say "Installing the ${TARGET} package beside the distribution's QEMU and libvirt"
case "${TARGET}" in
    arch)
        tarball="$(compgen -G "${DIST}/lsw-*-x86_64.tar.zst" | head -1)"
        [[ -n "${tarball}" ]] || fail "no tarball in ${DIST}"
        mapfile -t depends < "${tarball%.tar.zst}.depends"
        pacman -Syu --noconfirm --needed "${depends[@]}" libvirt qemu-desktop util-linux
        tar -C / --zstd -xf "${tarball}"
        ;;
    deb)
        package="$(compgen -G "${DIST}/lsw_*_amd64.deb" | head -1)"
        [[ -n "${package}" ]] || fail "no .deb in ${DIST}"
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get install -y "${package}" libvirt-clients qemu-system-x86 qemu-system-gui qemu-system-modules-spice qemu-system-modules-opengl util-linux
        ;;
    rpm)
        package="$(compgen -G "${DIST}/lsw-*.x86_64.rpm" | head -1)"
        [[ -n "${package}" ]] || fail "no .rpm in ${DIST}"
        dnf -y install "${package}" libvirt-client libvirt-daemon-driver-qemu qemu-system-x86 qemu-device-display-virtio-gpu qemu-device-display-qxl qemu-ui-gtk util-linux
        ;;
    *) usage ;;
esac

say "Layout"
[[ -x "${QEMU}" ]] || fail "${QEMU} is missing or not executable"
[[ -x "${TREE}/qemu-system-x86_64" ]] || fail "${TREE}/qemu-system-x86_64 is missing or not executable"
[[ -f "${TREE}/libav_executor.so" ]] || fail "${TREE}/libav_executor.so is missing"
[[ -f "${TREE}/hw-display-virtio-gpu-pci-av.so" ]] || fail "the accel-virt GPU module is missing from ${TREE}"
system_modules="$(find /usr/lib /usr/lib64 -maxdepth 3 -type d -name qemu -not -path "${TREE}*" 2>/dev/null | tr '\n' ' ')"
echo "    the distribution's QEMU modules: ${system_modules:-none found}"

say "Shared libraries"
while read -r elf; do
    missing="$(ldd "${elf}" 2>/dev/null | grep 'not found' || true)"
    [[ -z "${missing}" ]] || fail "${elf} does not resolve: ${missing}"
done < <(find /usr/bin/lsw-manager "${TREE}" /usr/lib/lsw/remotewin -maxdepth 1 -type f -perm -u+x)

say "The capability probe libvirt runs"
printf '%s\n' '{"execute":"qmp_capabilities"}' '{"execute":"qom-list-types"}' '{"execute":"query-machines"}' '{"execute":"quit"}' \
    | "${QEMU}" -S -no-user-config -nodefaults -nographic -machine none -qmp stdio >/dev/null \
    || fail "${QEMU} does not survive qom-list-types"

say "The accel-virt GPU device"
QEMU_MODULE_DIR="${TREE}" "${QEMU}" -machine none -display none -device virtio-gpu-av-pci,help | grep -q 'executor=' \
    || fail "virtio-gpu-av-pci does not load from ${TREE}"

say "libvirt's own probe, from a user's session"
id lswcheck >/dev/null 2>&1 || useradd -m lswcheck
runtime="$(mktemp -d)"
chown lswcheck "${runtime}"
chmod 700 "${runtime}"
if ! capabilities="$(runuser -u lswcheck -- env XDG_RUNTIME_DIR="${runtime}" HOME=/home/lswcheck \
        virsh -c qemu:///session domcapabilities --emulatorbin "${QEMU}" --virttype qemu 2>&1)"; then
    echo "${capabilities}" >&2
    fail "libvirt cannot probe ${QEMU}"
fi
grep -q "<path>${QEMU}</path>" <<< "${capabilities}" || fail "libvirt's capabilities do not name ${QEMU}"

say "The ${TARGET} package passes"
