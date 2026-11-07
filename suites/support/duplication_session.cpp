#include "support/duplication_session.hpp"

#include "support/d3d11_tools.hpp"
#include "support/target_adapter.hpp"

namespace lsw::suites {

DuplicationOpen DuplicationSession::open(const RECT& area, std::string& error) {
    AvAdapter target;
    if (!TargetAdapter::pick(target, error) || !device_.create_ex(target.luid, nullptr, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT, error)) {
        return DuplicationOpen::Failed;
    }
    IUnknown* opened = AvAdapters::open_adapter(target.luid);
    if (opened == nullptr) {
        error = "the target adapter cannot be opened by LUID";
        return DuplicationOpen::Failed;
    }
    Com<IDXGIAdapter1> adapter(static_cast<IDXGIAdapter1*>(opened));
    Com<IDXGIOutput> chosen;
    for (UINT i = 0;; ++i) {
        Com<IDXGIOutput> output;
        if (adapter->EnumOutputs(i, output.put()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_OUTPUT_DESC desc{};
        if (SUCCEEDED(output->GetDesc(&desc)) && area.left >= desc.DesktopCoordinates.left && area.left < desc.DesktopCoordinates.right && area.top >= desc.DesktopCoordinates.top &&
            area.top < desc.DesktopCoordinates.bottom) {
            output_ = desc.DesktopCoordinates;
            if (desc.Rotation != DXGI_MODE_ROTATION_IDENTITY && desc.Rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
                error = "the output is rotated, which this check does not undo";
                return DuplicationOpen::Unavailable;
            }
            chosen = std::move(output);
            break;
        }
    }
    if (!chosen) {
        error = "no output of the target adapter contains the window at " + std::to_string(area.left) + "," + std::to_string(area.top);
        return DuplicationOpen::Failed;
    }
    Com<IDXGIOutput5> output5 = chosen.as<IDXGIOutput5>();
    Com<IDXGIOutput1> output1 = chosen.as<IDXGIOutput1>();
    if (!output5 && !output1) {
        error = "the output has neither IDXGIOutput5 nor IDXGIOutput1";
        return DuplicationOpen::Unavailable;
    }
    const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    const HRESULT created = output5 ? output5->DuplicateOutput1(device_.device, 0, 1, formats, duplication_.put()) : output1->DuplicateOutput(device_.device, duplication_.put());
    output1_used_ = static_cast<bool>(output5);
    if (FAILED(created)) {
        error = std::string(output5 ? "DuplicateOutput1" : "DuplicateOutput") + " returned " + D3d11Tools::hr(created);
        if (created == DXGI_ERROR_UNSUPPORTED || created == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE || created == E_NOTIMPL) {
            return DuplicationOpen::Unavailable;
        }
        return DuplicationOpen::Failed;
    }
    return DuplicationOpen::Ready;
}

HRESULT DuplicationSession::acquire(UINT timeout_ms, DuplicationFrame& out, std::string& error) {
    out = DuplicationFrame{};
    Com<IDXGIResource> resource;
    const HRESULT acquired = duplication_->AcquireNextFrame(timeout_ms, &out.info, resource.put());
    if (FAILED(acquired)) {
        return acquired;
    }
    if (out.info.TotalMetadataBufferSize > 0) {
        std::vector<std::uint8_t> buffer(out.info.TotalMetadataBufferSize);
        UINT used = 0;
        if (SUCCEEDED(duplication_->GetFrameMoveRects(static_cast<UINT>(buffer.size()), reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(buffer.data()), &used))) {
            const DXGI_OUTDUPL_MOVE_RECT* moves = reinterpret_cast<const DXGI_OUTDUPL_MOVE_RECT*>(buffer.data());
            out.moves.assign(moves, moves + used / sizeof(DXGI_OUTDUPL_MOVE_RECT));
        }
        used = 0;
        if (SUCCEEDED(duplication_->GetFrameDirtyRects(static_cast<UINT>(buffer.size()), reinterpret_cast<RECT*>(buffer.data()), &used))) {
            const RECT* rects = reinterpret_cast<const RECT*>(buffer.data());
            out.dirty.assign(rects, rects + used / sizeof(RECT));
        }
    }
    if (out.info.PointerShapeBufferSize > 0) {
        out.shape_bytes.resize(out.info.PointerShapeBufferSize);
        UINT used = 0;
        out.has_shape = SUCCEEDED(duplication_->GetFramePointerShape(static_cast<UINT>(out.shape_bytes.size()), out.shape_bytes.data(), &used, &out.shape));
    }
    Com<ID3D11Texture2D> frame;
    if (resource) {
        resource->QueryInterface(__uuidof(ID3D11Texture2D), frame.put_void());
    }
    bool copied = false;
    if (frame) {
        D3D11_TEXTURE2D_DESC desc{};
        frame->GetDesc(&desc);
        if (!staging_) {
            staging_ = D3d11Tools::texture(device_, D3d11Tools::desc2d(desc.Width, desc.Height, desc.Format, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ));
        }
        if (staging_) {
            device_.context->CopyResource(staging_.get(), frame.get());
            TexelImage image;
            if (D3d11Tools::read_texture(device_, staging_.get(), 0, image, error)) {
                out.image.width = static_cast<int>(image.width);
                out.image.height = static_cast<int>(image.height);
                out.image.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
                bool any = false;
                for (UINT y = 0; y < image.height; ++y) {
                    for (UINT x = 0; x < image.width; ++x) {
                        const std::uint32_t value = image.pixel(x, y);
                        out.image.pixels[static_cast<std::size_t>(y) * image.width + x] = value;
                        any = any || (value & 0xFFFFFFu) != 0;
                    }
                }
                out.all_zero = !any;
                copied = true;
            }
        }
    }
    frame.reset();
    resource.reset();
    const HRESULT released = duplication_->ReleaseFrame();
    if (FAILED(released)) {
        error = "ReleaseFrame returned " + D3d11Tools::hr(released);
        return released;
    }
    if (!copied && error.empty()) {
        error = "the acquired frame resource could not be copied";
    }
    return acquired;
}

std::string DuplicationSession::zero_frame_finding(int frames, long long last_present_time) {
    return "F42: every duplicated frame is entirely zero (" + std::to_string(frames) + " frames acquired, last LastPresentTime " + std::to_string(last_present_time) + ")";
}

}  // namespace lsw::suites
