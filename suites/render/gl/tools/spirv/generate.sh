#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="${here}/../../spirv_data.inc"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT
glslangValidator -G -S vert "${here}/mesh.vert" -o "${tmp}/vert.spv" >/dev/null
glslangValidator -G -S frag "${here}/mesh.frag" -o "${tmp}/frag.spv" >/dev/null
spirv-val --target-env opengl4.5 "${tmp}/vert.spv"
spirv-val --target-env opengl4.5 "${tmp}/frag.spv"
{
    echo "alignas(4) static const unsigned char kSpirvVertex[] = {"
    od -An -v -tx1 "${tmp}/vert.spv" | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g'
    echo "};"
    echo "alignas(4) static const unsigned char kSpirvFragment[] = {"
    od -An -v -tx1 "${tmp}/frag.spv" | sed 's/ \([0-9a-f][0-9a-f]\)/0x\1,/g'
    echo "};"
} > "${out}"
