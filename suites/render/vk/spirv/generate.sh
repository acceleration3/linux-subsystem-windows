#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p gen
for src in *.vert *.frag *.comp *.geom *.tesc *.tese *.mesh *.task *.rgen *.rchit *.rmiss *.rahit *.rint *.rcall; do
    [[ -e "${src}" ]] || continue
    [[ gen/${src}.cpp -nt ${src} ]] && continue
    tmp=$(mktemp)
    env=vulkan1.1
    case "${src}" in
        *.v12.*) env=vulkan1.2 ;;
        *.v13.*) env=vulkan1.3 ;;
    esac
    glslangValidator -V --target-env "${env}" "${src}" -o "${tmp}" > /dev/null
    spirv-val --target-env "${env}" "${tmp}"
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
