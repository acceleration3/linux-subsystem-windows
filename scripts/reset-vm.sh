#!/usr/bin/env bash
# Wipe the LSW VM so the wizard runs again from scratch.
#
# Removes:
#   * the libvirt domain (and its NVRAM)
#   * the qcow2 disk image
#   * the LSW payload ISO
#   * the wizard's "first run" flag
#
# Preserves:
#   * the downloaded Windows ISO (~/.local/share/lsw/isos/windows.iso)
#   * the cached virtio-win ISO

set -u

VM_NAME="lsw"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/lsw"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/lsw"
URI="qemu:///session"

echo "Stopping + undefining libvirt domain '${VM_NAME}'..."
virsh -c "${URI}" destroy "${VM_NAME}" 2>/dev/null || true
virsh -c "${URI}" undefine --nvram "${VM_NAME}" 2>/dev/null || true

# Remove entire lsw folder
echo "Removing entire lsw folder..."
rm -rf "${DATA}"

echo "Clearing wizard 'first run' flag..."
rm -fv "${CONFIG}/manager.ini"

echo "Done. Re-run ./build/src/manager/lsw-manager to start the wizard."
