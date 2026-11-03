#include "lsw/agent/win/image_codec.hpp"

#include "lsw/agent/log.hpp"
#include "lsw/agent/win/com.hpp"
#include "lsw/agent/win/com_ptr.hpp"
#include "lsw/agent/win/error_text.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
// clang-format on

#include <cstring>
#include <stdexcept>

namespace lsw::agent::win {

namespace {
constexpr std::size_t kFileHeaderBytes = 14;
constexpr std::size_t kInfoHeaderBytes = 40;
constexpr DWORD kBiBitfields = 3;

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw std::runtime_error(std::string(what) + ": " + ErrorText::hresult(hr));
    }
}

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

void put_le32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xff));
    }
}

ComPtr<IWICImagingFactory> make_factory() {
    ComPtr<IWICImagingFactory> factory;
    check(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&factory)),
          "WIC factory");
    return factory;
}

ComPtr<IWICBitmapSource> first_frame_bgra(IWICImagingFactory* factory, std::uint8_t* data,
                                          std::size_t size) {
    ComPtr<IWICStream> stream;
    check(factory->CreateStream(&stream), "WIC stream");
    check(stream->InitializeFromMemory(data, static_cast<DWORD>(size)), "WIC memory");
    ComPtr<IWICBitmapDecoder> decoder;
    check(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand,
                                           &decoder),
          "WIC decoder");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "WIC frame");
    ComPtr<IWICBitmapSource> converted;
    check(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &converted),
          "WIC convert");
    return converted;
}

std::vector<std::uint8_t> encode_png(IWICImagingFactory* factory, IWICBitmapSource* source) {
    ComPtr<IStream> stream;
    check(::CreateStreamOnHGlobal(nullptr, TRUE, &stream), "PNG stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
    check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG init");
    ComPtr<IWICBitmapFrameEncode> frame;
    check(encoder->CreateNewFrame(&frame, nullptr), "PNG frame");
    check(frame->Initialize(nullptr), "PNG frame init");
    UINT width = 0;
    UINT height = 0;
    check(source->GetSize(&width, &height), "PNG size");
    check(frame->SetSize(width, height), "PNG set size");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    check(frame->SetPixelFormat(&format), "PNG format");
    check(frame->WriteSource(source, nullptr), "PNG write");
    check(frame->Commit(), "PNG commit");
    check(encoder->Commit(), "PNG finish");
    STATSTG stat{};
    check(stream->Stat(&stat, STATFLAG_NONAME), "PNG stat");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    check(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "PNG seek");
    ULONG got = 0;
    check(stream->Read(out.data(), static_cast<ULONG>(out.size()), &got), "PNG read");
    out.resize(got);
    return out;
}
}

std::vector<std::uint8_t> ImageCodec::dib_to_png(const std::uint8_t* dib, std::size_t size) {
    if (size < kInfoHeaderBytes || size > kMaxDibBytes) {
        return {};
    }
    try {
        const Apartment apartment;
        const std::uint32_t header = le32(dib);
        const std::uint32_t bits =
            static_cast<std::uint32_t>(dib[14]) | (static_cast<std::uint32_t>(dib[15]) << 8);
        const std::uint32_t compression = le32(dib + 16);
        const std::uint32_t colors_used = le32(dib + 32);
        std::uint64_t palette =
            bits <= 8 ? (colors_used != 0 ? colors_used : (1U << bits)) : colors_used;
        palette *= 4;
        if (compression == kBiBitfields && header == kInfoHeaderBytes) {
            palette += 12;
        }
        std::vector<std::uint8_t> bmp;
        bmp.reserve(kFileHeaderBytes + size);
        bmp.push_back('B');
        bmp.push_back('M');
        put_le32(bmp, static_cast<std::uint32_t>(kFileHeaderBytes + size));
        put_le32(bmp, 0);
        put_le32(bmp, static_cast<std::uint32_t>(kFileHeaderBytes + header + palette));
        bmp.insert(bmp.end(), dib, dib + size);
        const auto factory = make_factory();
        const auto source = first_frame_bgra(factory.Get(), bmp.data(), bmp.size());
        return encode_png(factory.Get(), source.Get());
    } catch (const std::exception& e) {
        LSW_AGENT_DEBUG("clipboard: DIB to PNG failed: {}", e.what());
        return {};
    }
}

std::vector<std::uint8_t> ImageCodec::png_to_dib(const std::vector<std::uint8_t>& png) {
    try {
        const Apartment apartment;
        const auto factory = make_factory();
        std::vector<std::uint8_t> copy = png;
        const auto source = first_frame_bgra(factory.Get(), copy.data(), copy.size());
        UINT width = 0;
        UINT height = 0;
        check(source->GetSize(&width, &height), "DIB size");
        const std::uint64_t stride = static_cast<std::uint64_t>(width) * 4;
        const std::uint64_t pixels = stride * height;
        if (width == 0 || height == 0 || pixels + kInfoHeaderBytes > kMaxDibBytes) {
            return {};
        }
        std::vector<std::uint8_t> top_down(static_cast<std::size_t>(pixels));
        check(source->CopyPixels(nullptr, static_cast<UINT>(stride),
                                 static_cast<UINT>(top_down.size()), top_down.data()),
              "DIB pixels");
        std::vector<std::uint8_t> dib;
        dib.reserve(kInfoHeaderBytes + top_down.size());
        put_le32(dib, kInfoHeaderBytes);
        put_le32(dib, width);
        put_le32(dib, height);
        dib.push_back(1);
        dib.push_back(0);
        dib.push_back(32);
        dib.push_back(0);
        put_le32(dib, 0);
        put_le32(dib, static_cast<std::uint32_t>(top_down.size()));
        put_le32(dib, 0);
        put_le32(dib, 0);
        put_le32(dib, 0);
        put_le32(dib, 0);
        for (UINT row = height; row > 0; --row) {
            const auto* line = top_down.data() + static_cast<std::size_t>(row - 1) * stride;
            dib.insert(dib.end(), line, line + stride);
        }
        return dib;
    } catch (const std::exception& e) {
        LSW_AGENT_DEBUG("clipboard: PNG to DIB failed: {}", e.what());
        return {};
    }
}

}
