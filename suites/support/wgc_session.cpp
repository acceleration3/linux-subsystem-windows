#include "support/wgc_session.hpp"

#include "support/com_ptr.hpp"
#include "support/d3d11_tools.hpp"
#include "support/dyn_library.hpp"
#include "support/wgc_abi.hpp"

#include <dwmapi.h>

#include <atomic>
#include <cstdio>

namespace lsw::suites {

namespace {

using namespace wgc_abi;

constexpr GUID kAgileObject = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};

using RoInitializeFn = HRESULT(WINAPI*)(int);
using RoGetActivationFactoryFn = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
using WindowsCreateStringFn = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
using WindowsDeleteStringFn = HRESULT(WINAPI*)(HSTRING);
using CreateDeviceFn = HRESULT(WINAPI*)(IDXGIDevice*, IInspectable**);

struct Runtime {
    DynLibrary combase;
    DynLibrary d3d11;
    RoInitializeFn initialize = nullptr;
    RoGetActivationFactoryFn factory = nullptr;
    WindowsCreateStringFn create_string = nullptr;
    WindowsDeleteStringFn delete_string = nullptr;
    CreateDeviceFn create_device = nullptr;

    [[nodiscard]] bool ready() {
        if (!combase.loaded() && combase.load("combase.dll")) {
            initialize = combase.symbol<RoInitializeFn>("RoInitialize");
            factory = combase.symbol<RoGetActivationFactoryFn>("RoGetActivationFactory");
            create_string = combase.symbol<WindowsCreateStringFn>("WindowsCreateString");
            delete_string = combase.symbol<WindowsDeleteStringFn>("WindowsDeleteString");
        }
        if (!d3d11.loaded() && d3d11.load("d3d11.dll")) {
            create_device = d3d11.symbol<CreateDeviceFn>("CreateDirect3D11DeviceFromDXGIDevice");
        }
        return initialize != nullptr && factory != nullptr && create_string != nullptr && delete_string != nullptr && create_device != nullptr;
    }

    [[nodiscard]] HRESULT activate(const wchar_t* name, REFIID iid, void** out) const {
        HSTRING text = nullptr;
        HRESULT result = create_string(name, static_cast<UINT32>(wcslen(name)), &text);
        if (SUCCEEDED(result)) {
            result = factory(text, iid, out);
            delete_string(text);
        }
        return result;
    }
};

Runtime& runtime() {
    static Runtime instance;
    return instance;
}

bool unavailable_code(HRESULT result) {
    return result == REGDB_E_CLASSNOTREG || result == E_NOINTERFACE || result == E_NOTIMPL || result == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED) ||
           result == HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND) || result == HRESULT_FROM_WIN32(ERROR_CALL_NOT_IMPLEMENTED);
}

std::string code(HRESULT result) {
    char text[16];
    snprintf(text, sizeof(text), "0x%08lX", static_cast<unsigned long>(result));
    return text;
}

template <typename Base>
class Delegate : public Base {
public:
    virtual ~Delegate() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (riid == IID_IUnknown || riid == kAgileObject || riid == __uuidof(Base)) {
            *object = static_cast<Base*>(this);
            this->AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(++count_); }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = static_cast<ULONG>(--count_);
        if (left == 0) {
            delete this;
        }
        return left;
    }

private:
    std::atomic<long> count_{1};
};

class ClosedHandler final : public Delegate<IClosedHandler> {
public:
    explicit ClosedHandler(std::atomic<bool>* flag) : flag_(flag) {}

    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IInspectable*) override {
        *flag_ = true;
        return S_OK;
    }

private:
    std::atomic<bool>* flag_;
};

class ArrivedHandler final : public Delegate<IArrivedHandler> {
public:
    explicit ArrivedHandler(HANDLE event) : event_(event) {}

    HRESULT STDMETHODCALLTYPE Invoke(IInspectable*, IInspectable*) override {
        ::SetEvent(event_);
        return S_OK;
    }

private:
    HANDLE event_;
};

template <typename T>
void close_object(const Com<T>& object) {
    if (!object) {
        return;
    }
    Com<IClosable> closable = object.template as<IClosable>();
    if (closable) {
        closable->Close();
    }
}

}  // namespace

struct WgcSession::Impl {
    D3d11Device* device = nullptr;
    Com<IInspectable> direct_device;
    Com<IDirect3DDevice> winrt_device;
    Com<IGraphicsCaptureItem> item;
    Com<IDirect3D11CaptureFramePool> pool;
    Com<IGraphicsCaptureSession> session;
    Com<ID3D11Texture2D> staging;
    EventRegistrationToken closed_token{};
    EventRegistrationToken arrived_token{};
    std::atomic<bool> closed{false};
    HANDLE arrived = nullptr;
    bool closed_registered = false;
    bool arrived_registered = false;
    bool shut = false;
};

WgcSession::WgcSession() : impl_(std::make_unique<Impl>()) {
    impl_->arrived = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

WgcSession::~WgcSession() {
    std::string ignored;
    static_cast<void>(close(ignored));
    ::CloseHandle(impl_->arrived);
}

POINT WgcSession::client_offset(HWND window) {
    const RECT bounds = frame_bounds(window);
    POINT origin{0, 0};
    ::ClientToScreen(window, &origin);
    return {origin.x - bounds.left, origin.y - bounds.top};
}

RECT WgcSession::frame_bounds(HWND window) {
    RECT bounds{};
    if (FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds, sizeof(bounds)))) {
        ::GetWindowRect(window, &bounds);
    }
    return bounds;
}

WgcOpen WgcSession::open(HWND window, D3d11Device& device, std::string& error) {
    Runtime& rt = runtime();
    if (!rt.ready()) {
        error = "combase.dll or d3d11.dll lacks the WinRT activation or CreateDirect3D11DeviceFromDXGIDevice entry points";
        return WgcOpen::Unavailable;
    }
    const HRESULT initialized = rt.initialize(1);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        error = "RoInitialize returned " + code(initialized);
        return WgcOpen::Failed;
    }
    impl_->device = &device;
    Com<IGraphicsCaptureSessionStatics> statics;
    HRESULT result = rt.activate(L"Windows.Graphics.Capture.GraphicsCaptureSession", __uuidof(IGraphicsCaptureSessionStatics), statics.put_void());
    if (FAILED(result)) {
        error = "the GraphicsCaptureSession factory returned " + code(result);
        return unavailable_code(result) ? WgcOpen::Unavailable : WgcOpen::Failed;
    }
    unsigned char supported = 0;
    if (FAILED(statics->IsSupported(&supported)) || supported == 0) {
        error = "GraphicsCaptureSession.IsSupported() is false";
        return WgcOpen::Unavailable;
    }
    Com<IGraphicsCaptureItemInterop> interop;
    result = rt.activate(L"Windows.Graphics.Capture.GraphicsCaptureItem", __uuidof(IGraphicsCaptureItemInterop), interop.put_void());
    if (FAILED(result)) {
        error = "the GraphicsCaptureItem interop factory returned " + code(result);
        return unavailable_code(result) ? WgcOpen::Unavailable : WgcOpen::Failed;
    }
    result = interop->CreateForWindow(window, __uuidof(IGraphicsCaptureItem), impl_->item.put_void());
    if (FAILED(result)) {
        error = "IGraphicsCaptureItemInterop::CreateForWindow returned " + code(result);
        return unavailable_code(result) ? WgcOpen::Unavailable : WgcOpen::Failed;
    }
    Com<IDXGIDevice> dxgi;
    device.device->QueryInterface(__uuidof(IDXGIDevice), dxgi.put_void());
    result = rt.create_device(dxgi.get(), impl_->direct_device.put());
    if (FAILED(result)) {
        error = "CreateDirect3D11DeviceFromDXGIDevice returned " + code(result);
        return unavailable_code(result) ? WgcOpen::Unavailable : WgcOpen::Failed;
    }
    impl_->winrt_device = impl_->direct_device.as<IDirect3DDevice>();
    Com<IDirect3D11CaptureFramePoolStatics2> pools;
    result = rt.activate(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", __uuidof(IDirect3D11CaptureFramePoolStatics2), pools.put_void());
    if (FAILED(result) || !impl_->winrt_device) {
        error = "the Direct3D11CaptureFramePool statics returned " + code(result);
        return unavailable_code(result) ? WgcOpen::Unavailable : WgcOpen::Failed;
    }
    SizeInt32 size{};
    result = impl_->item->get_Size(&size);
    if (FAILED(result)) {
        error = "GraphicsCaptureItem.Size returned " + code(result);
        return WgcOpen::Failed;
    }
    result = pools->CreateFreeThreaded(impl_->winrt_device.get(), kPixelFormatBgra8, 2, size, impl_->pool.put());
    if (FAILED(result)) {
        error = "CreateFreeThreaded returned " + code(result);
        return WgcOpen::Failed;
    }
    ArrivedHandler* arrived = new ArrivedHandler(impl_->arrived);
    result = impl_->pool->add_FrameArrived(arrived, &impl_->arrived_token);
    arrived->Release();
    impl_->arrived_registered = SUCCEEDED(result);
    ClosedHandler* closed = new ClosedHandler(&impl_->closed);
    result = impl_->item->add_Closed(closed, &impl_->closed_token);
    closed->Release();
    if (FAILED(result)) {
        error = "GraphicsCaptureItem.Closed registration returned " + code(result);
        return WgcOpen::Failed;
    }
    impl_->closed_registered = true;
    result = impl_->pool->CreateCaptureSession(impl_->item.get(), impl_->session.put());
    if (FAILED(result)) {
        error = "CreateCaptureSession returned " + code(result);
        return unavailable_code(result) ? WgcOpen::Unavailable : WgcOpen::Failed;
    }
    result = impl_->session->StartCapture();
    if (FAILED(result)) {
        error = "StartCapture returned " + code(result);
        return WgcOpen::Failed;
    }
    return WgcOpen::Ready;
}

bool WgcSession::next(WgcFrame& out, int timeout_ms, std::string& error) {
    error.clear();
    const long long deadline = static_cast<long long>(::GetTickCount64()) + timeout_ms;
    for (;;) {
        Com<IDirect3D11CaptureFrame> frame;
        const HRESULT got = impl_->pool->TryGetNextFrame(frame.put());
        if (FAILED(got)) {
            error = "TryGetNextFrame returned " + code(got);
            return false;
        }
        if (frame) {
            SizeInt32 size{};
            TimeSpan time{};
            frame->get_ContentSize(&size);
            frame->get_SystemRelativeTime(&time);
            Com<IDirect3DSurface> surface;
            if (FAILED(frame->get_Surface(surface.put()))) {
                error = "Direct3D11CaptureFrame.Surface failed";
                return false;
            }
            Com<IDirect3DDxgiInterfaceAccess> access;
            surface->QueryInterface(__uuidof(IDirect3DDxgiInterfaceAccess), access.put_void());
            Com<ID3D11Texture2D> texture;
            if (!access || FAILED(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()))) {
                error = "the frame surface has no ID3D11Texture2D";
                return false;
            }
            D3D11_TEXTURE2D_DESC desc{};
            texture->GetDesc(&desc);
            if (!impl_->staging) {
                impl_->staging = D3d11Tools::texture(*impl_->device, D3d11Tools::desc2d(desc.Width, desc.Height, desc.Format, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
            } else {
                D3D11_TEXTURE2D_DESC have{};
                impl_->staging->GetDesc(&have);
                if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format) {
                    impl_->staging = D3d11Tools::texture(*impl_->device, D3d11Tools::desc2d(desc.Width, desc.Height, desc.Format, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
                }
            }
            if (!impl_->staging) {
                error = "the staging texture cannot be created";
                return false;
            }
            impl_->device->context->CopyResource(impl_->staging.get(), texture.get());
            TexelImage image;
            if (!D3d11Tools::read_texture(*impl_->device, impl_->staging.get(), 0, image, error)) {
                return false;
            }
            out.image.width = static_cast<int>(image.width);
            out.image.height = static_cast<int>(image.height);
            out.image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
            for (UINT y = 0; y < image.height; ++y) {
                for (UINT x = 0; x < image.width; ++x) {
                    out.image.pixels[static_cast<std::size_t>(y) * image.width + x] = image.pixel(x, y);
                }
            }
            out.content_width = size.Width;
            out.content_height = size.Height;
            out.system_time = time.Duration;
            texture.reset();
            surface.reset();
            close_object(frame);
            return true;
        }
        const long long left = deadline - static_cast<long long>(::GetTickCount64());
        if (left <= 0) {
            return false;
        }
        ::WaitForSingleObject(impl_->arrived, static_cast<DWORD>(std::min<long long>(left, 20)));
    }
}

bool WgcSession::latest(WgcFrame& out, int timeout_ms, std::string& error) {
    if (!next(out, timeout_ms, error)) {
        return false;
    }
    WgcFrame newer;
    std::string ignored;
    while (next(newer, 0, ignored)) {
        out = std::move(newer);
    }
    return true;
}

bool WgcSession::recreate(int width, int height, std::string& error) {
    SizeInt32 size{width, height};
    const HRESULT result = impl_->pool->Recreate(impl_->winrt_device.get(), kPixelFormatBgra8, 2, size);
    if (FAILED(result)) {
        error = "Direct3D11CaptureFramePool.Recreate returned " + code(result);
        return false;
    }
    return true;
}

bool WgcSession::wait_closed(int timeout_ms) {
    const long long deadline = static_cast<long long>(::GetTickCount64()) + timeout_ms;
    while (!impl_->closed.load() && static_cast<long long>(::GetTickCount64()) < deadline) {
        ::Sleep(10);
    }
    return impl_->closed.load();
}

bool WgcSession::close(std::string& error) {
    if (impl_->shut) {
        return true;
    }
    impl_->shut = true;
    bool ok = true;
    if (impl_->pool && impl_->arrived_registered) {
        impl_->pool->remove_FrameArrived(impl_->arrived_token);
    }
    if (impl_->item && impl_->closed_registered) {
        impl_->item->remove_Closed(impl_->closed_token);
    }
    if (impl_->session) {
        Com<IClosable> closable = impl_->session.as<IClosable>();
        if (closable) {
            const HRESULT result = closable->Close();
            if (FAILED(result)) {
                error = "closing the capture session returned " + code(result);
                ok = false;
            }
        }
    }
    if (impl_->pool) {
        Com<IClosable> closable = impl_->pool.as<IClosable>();
        if (closable) {
            const HRESULT result = closable->Close();
            if (FAILED(result)) {
                error = "closing the frame pool returned " + code(result);
                ok = false;
            }
        }
    }
    impl_->session.reset();
    impl_->pool.reset();
    impl_->item.reset();
    impl_->staging.reset();
    impl_->winrt_device.reset();
    impl_->direct_device.reset();
    return ok;
}

}  // namespace lsw::suites
