#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p gen
for src in *.vert *.frag *.comp *.geom *.tesc *.tese; do
    [[ -e "${src}" ]] || continue
    tmp=$(mktemp)
    glslangValidator -V --target-env vulkan1.1 "${src}" -o "${tmp}" > /dev/null
    spirv-val --target-env vulkan1.1 "${tmp}"
    out="gen/${src}.cpp"
    {
        echo '#include "render/vk/shader_registry.hpp"'
        echo
        echo '#include <iterator>'
        echo
        echo 'namespace {'
        echo
        echo 'const std::uint32_t kWords[] = {'
        od -An -v -t x4 -w16 "${tmp}" | sed -E 's/ ([0-9a-f]{8})/ 0x\1u,/g; s/^ +/    /'
        echo '};'
        echo
        echo "const lsw::render::vk::ShaderRegistrar kRegistrar(\"${src}\", kWords, std::size(kWords));"
        echo
        echo '}  // namespace'
    } > "${out}"
    rm -f "${tmp}"
done
