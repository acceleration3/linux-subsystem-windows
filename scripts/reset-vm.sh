#!/usr/bin/env bash

set -u

VM_NAME="lsw"
DATA="${XDG_DATA_HOME:-$HOME/.local/share}/lsw"
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}/lsw"
URI="qemu:///session"

echo "Stopping + undefining libvirt domain '${VM_NAME}'..."
virsh -c "${URI}" destroy "${VM_NAME}" 2>/dev/null || true
virsh -c "${URI}" undefine --nvram "${VM_NAME}" 2>/dev/null || true

echo "Removing entire lsw folder..."
rm -rf "${DATA}"

echo "Clearing wizard 'first run' flag..."
rm -fv "${CONFIG}/manager.ini"

echo "Done. Re-run ./build/src/host/lsw-manager to start the wizard."
