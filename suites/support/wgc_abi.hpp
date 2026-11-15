#pragma once

#include <windows.h>
#include <eventtoken.h>
#include <hstring.h>
#include <inspectable.h>

#include <cstdint>

namespace lsw::suites::wgc_abi {
struct SizeInt32 {
    std::int32_t Width;
    std::int32_t Height;
};

struct TimeSpan {
    std::int64_t Duration;
};

constexpr std::int32_t kPixelFormatBgra8 = 87;

MIDL_INTERFACE("814e42a9-f70f-4ad7-939b-fddcc6eb880d")
IGraphicsCaptureSession : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE StartCapture() = 0;
};

MIDL_INTERFACE("2c39ae40-7d2e-5044-804e-8b6799d4cf9e")
IGraphicsCaptureSession2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsCursorCaptureEnabled(unsigned char* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsCursorCaptureEnabled(unsigned char value) = 0;
};

MIDL_INTERFACE("f2cdd966-22ae-5ea1-9596-3a289344c3be")
IGraphicsCaptureSession3 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsBorderRequired(unsigned char* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_IsBorderRequired(unsigned char value) = 0;
};

MIDL_INTERFACE("67c0ea62-1f85-5061-925a-239be0ac09cb")
IGraphicsCaptureSession5 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_MinUpdateInterval(TimeSpan* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MinUpdateInterval(TimeSpan value) = 0;
};

MIDL_INTERFACE("2224a540-5974-49aa-b232-0882536f4cb5")
IGraphicsCaptureSessionStatics : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE IsSupported(unsigned char* result) = 0;
};

MIDL_INTERFACE("30d5a829-7fa4-4026-83bb-d75bae4ea99e")
IClosable : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

MIDL_INTERFACE("0bf4a146-13c1-4694-bee3-7abf15eaf586")
IDirect3DSurface : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Description(void* value) = 0;
};

MIDL_INTERFACE("a37624ab-8d5f-4650-9d3e-9eae3d9bc670")
IDirect3DDevice : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Trim() = 0;
};

MIDL_INTERFACE("a9b3d012-3df2-4ee3-b8d1-8695f457d3c1")
IDirect3DDxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};

MIDL_INTERFACE("3628e81b-3cac-4c60-b7f4-23ce0e0c3356")
IGraphicsCaptureItemInterop : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateForWindow(HWND window, REFIID iid, void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateForMonitor(HMONITOR monitor, REFIID iid, void** result) = 0;
};

MIDL_INTERFACE("fa50c623-38da-4b32-acf3-fa9734ad800e")
IDirect3D11CaptureFrame : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Surface(IDirect3DSurface** value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_SystemRelativeTime(TimeSpan* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ContentSize(SizeInt32* value) = 0;
};

MIDL_INTERFACE("e9c610c0-a68c-5bd9-8021-8589346eeee2")
IClosedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IInspectable* args) = 0;
};

MIDL_INTERFACE("51a947f7-79cf-5a3e-a3a5-1289cfa6dfe8")
IArrivedHandler : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Invoke(IInspectable* sender, IInspectable* args) = 0;
};

MIDL_INTERFACE("79c3f95b-31f7-4ec2-a464-632ef5d30760")
IGraphicsCaptureItem : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_DisplayName(HSTRING* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(SizeInt32* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_Closed(IClosedHandler* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_Closed(EventRegistrationToken token) = 0;
};

MIDL_INTERFACE("24eb6d22-1975-422e-82e7-780dbd8ddf24")
IDirect3D11CaptureFramePool : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE Recreate(IDirect3DDevice* device, std::int32_t format, std::int32_t buffers, SizeInt32 size) = 0;
    virtual HRESULT STDMETHODCALLTYPE TryGetNextFrame(IDirect3D11CaptureFrame** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_FrameArrived(IArrivedHandler* handler, EventRegistrationToken* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_FrameArrived(EventRegistrationToken token) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateCaptureSession(IGraphicsCaptureItem* item, IGraphicsCaptureSession** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DispatcherQueue(void** value) = 0;
};

MIDL_INTERFACE("589b103f-6bbc-5df5-a991-02e28b3b66d5")
IDirect3D11CaptureFramePoolStatics2 : public IInspectable {
    virtual HRESULT STDMETHODCALLTYPE CreateFreeThreaded(IDirect3DDevice* device, std::int32_t format, std::int32_t buffers, SizeInt32 size, IDirect3D11CaptureFramePool** result) = 0;
};
}  // namespace lsw::suites::wgc_abi

__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureSession, 0x814e42a9, 0xf70f, 0x4ad7, 0x93, 0x9b, 0xfd, 0xdc, 0xc6, 0xeb, 0x88, 0x0d)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureSession2, 0x2c39ae40, 0x7d2e, 0x5044, 0x80, 0x4e, 0x8b, 0x67, 0x99, 0xd4, 0xcf, 0x9e)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureSession3, 0xf2cdd966, 0x22ae, 0x5ea1, 0x95, 0x96, 0x3a, 0x28, 0x93, 0x44, 0xc3, 0xbe)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureSession5, 0x67c0ea62, 0x1f85, 0x5061, 0x92, 0x5a, 0x23, 0x9b, 0xe0, 0xac, 0x09, 0xcb)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureSessionStatics, 0x2224a540, 0x5974, 0x49aa, 0xb2, 0x32, 0x08, 0x82, 0x53, 0x6f, 0x4c, 0xb5)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IClosable, 0x30d5a829, 0x7fa4, 0x4026, 0x83, 0xbb, 0xd7, 0x5b, 0xae, 0x4e, 0xa9, 0x9e)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IDirect3DSurface, 0x0bf4a146, 0x13c1, 0x4694, 0xbe, 0xe3, 0x7a, 0xbf, 0x15, 0xea, 0xf5, 0x86)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IDirect3DDevice, 0xa37624ab, 0x8d5f, 0x4650, 0x9d, 0x3e, 0x9e, 0xae, 0x3d, 0x9b, 0xc6, 0x70)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IDirect3DDxgiInterfaceAccess, 0xa9b3d012, 0x3df2, 0x4ee3, 0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureItemInterop, 0x3628e81b, 0x3cac, 0x4c60, 0xb7, 0xf4, 0x23, 0xce, 0x0e, 0x0c, 0x33, 0x56)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IDirect3D11CaptureFrame, 0xfa50c623, 0x38da, 0x4b32, 0xac, 0xf3, 0xfa, 0x97, 0x34, 0xad, 0x80, 0x0e)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IClosedHandler, 0xe9c610c0, 0xa68c, 0x5bd9, 0x80, 0x21, 0x85, 0x89, 0x34, 0x6e, 0xee, 0xe2)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IArrivedHandler, 0x51a947f7, 0x79cf, 0x5a3e, 0xa3, 0xa5, 0x12, 0x89, 0xcf, 0xa6, 0xdf, 0xe8)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IGraphicsCaptureItem, 0x79c3f95b, 0x31f7, 0x4ec2, 0xa4, 0x64, 0x63, 0x2e, 0xf5, 0xd3, 0x07, 0x60)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IDirect3D11CaptureFramePool, 0x24eb6d22, 0x1975, 0x422e, 0x82, 0xe7, 0x78, 0x0d, 0xbd, 0x8d, 0xdf, 0x24)
__CRT_UUID_DECL(lsw::suites::wgc_abi::IDirect3D11CaptureFramePoolStatics2, 0x589b103f, 0x6bbc, 0x5df5, 0xa9, 0x91, 0x02, 0xe2, 0x8b, 0x3b, 0x66, 0xd5)
