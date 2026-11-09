#pragma once

#include "render/d3d12/textures.hpp"

namespace lsw::suites::render::d3d12 {

class Cs {
public:
    [[nodiscard]] bool open(Kit& kit, const std::string& body, std::string& error, const std::vector<const wchar_t*>& args = {}) {
        if (!binder.create(kit, error)) {
            return false;
        }
        Kit::Bytes code;
        if (!kit.compile_as(Hlsl12::with_root(body.c_str()), "main", "cs", code, error, args)) {
            return false;
        }
        pso = D3d12Tools::compute_pso(kit.rig, binder.signature, code, error);
        return pso != nullptr;
    }

    void bind(ID3D12GraphicsCommandList* list) const {
        binder.bind_compute(list);
        list->SetPipelineState(pso);
    }

    Binder binder;
    ID3D12PipelineState* pso = nullptr;
};

}  // namespace lsw::suites::render::d3d12
