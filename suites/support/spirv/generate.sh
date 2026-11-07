#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
out=../vulkan_shaders.cpp
{
    echo '#include "support/vulkan_shaders.hpp"'
    echo
    echo '#include <iterator>'
    echo
    echo 'namespace lsw::suites {'
    echo
    echo 'namespace {'
    for pair in ops.comp:kOps subgroup.comp:kSubgroup limits.comp:kLimits bindings.comp:kBindings memmodel.comp:kMemoryModel \
                rect.vert:kRectVert point.vert:kPointVert rect.frag:kRectFrag rect2.frag:kRect2Frag rect.geom:kRectGeom \
                rect.tesc:kTessControl rect.tese:kTessEval; do
        src=${pair%%:*}
        name=${pair##*:}
        tmp=$(mktemp)
        glslangValidator -V --target-env vulkan1.1 "${src}" -o "${tmp}" > /dev/null
        spirv-val --target-env vulkan1.1 "${tmp}"
        echo
        echo "const std::uint32_t ${name}[] = {"
        od -An -v -t x4 -w16 "${tmp}" | sed -E 's/ ([0-9a-f]{8})/ 0x\1u,/g; s/^ +/    /'
        echo '};'
        rm -f "${tmp}"
    done
    echo
    echo '}  // namespace'
    echo
    echo 'std::vector<std::uint32_t> VulkanShaders::words(VulkanShader shader) {'
    echo '    switch (shader) {'
    for pair in Ops:kOps Subgroup:kSubgroup Limits:kLimits Bindings:kBindings MemoryModel:kMemoryModel RectVert:kRectVert \
                PointVert:kPointVert RectFrag:kRectFrag Rect2Frag:kRect2Frag RectGeom:kRectGeom TessControl:kTessControl \
                TessEval:kTessEval; do
        echo "        case VulkanShader::${pair%%:*}:"
        echo "            return std::vector<std::uint32_t>(std::begin(${pair##*:}), std::end(${pair##*:}));"
    done
    echo '    }'
    echo '    return {};'
    echo '}'
    echo
    echo '}  // namespace lsw::suites'
} > "${out}"
