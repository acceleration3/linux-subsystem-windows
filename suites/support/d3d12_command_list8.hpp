#pragma once

#include <d3d12.h>

namespace lsw::suites {
MIDL_INTERFACE("ee936ef9-599d-4d28-938e-23c4ad05ce51")
ID3D12GraphicsCommandList8 : public ID3D12GraphicsCommandList7 {
    virtual void STDMETHODCALLTYPE OMSetFrontAndBackStencilRef(UINT front_stencil_ref, UINT back_stencil_ref) = 0;
};
}  // namespace lsw::suites

#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(lsw::suites::ID3D12GraphicsCommandList8, 0xee936ef9, 0x599d, 0x4d28, 0x93, 0x8e, 0x23, 0xc4, 0xad, 0x05, 0xce, 0x51)
#endif
