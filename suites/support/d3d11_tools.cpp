#include "support/d3d11_tools.hpp"

#include "core/text.hpp"
#include "support/dyn_library.hpp"
#include "support/format_codec.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>

namespace lsw::suites {

std::uint32_t TexelImage::pixel(UINT x, UINT y, UINT z) const {
    std::uint32_t value = 0;
    const std::size_t offset = (static_cast<std::size_t>(z) * rows + y) * row_bytes + static_cast<std::size_t>(x) * 4;
    if (offset + 4 <= data.size()) {
        std::memcpy(&value, data.data() + offset, 4);
    }
    return value;
}

std::string D3d11Tools::hr(HRESULT result) {
    return "0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
}

std::string D3d11Tools::profile(const D3d11Device& device, const char* stage) {
    const std::string name = stage;
    if (device.level >= D3D_FEATURE_LEVEL_11_0) {
        return name + "_5_0";
    }
    return name + (device.level == D3D_FEATURE_LEVEL_10_1 ? "_4_1" : "_4_0");
}

bool D3d11Tools::compile(const std::string& source, const char* entry, const std::string& target,
                         std::vector<std::uint8_t>& code, std::string& error, const D3D_SHADER_MACRO* macros) {
    using Compile = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*,
                                     const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);
    static DynLibrary library;
    static std::mutex mutex;
    Compile compile_function = nullptr;
    {
        std::lock_guard lock(mutex);
        if (!library.loaded() && !library.load("d3dcompiler_47.dll")) {
            error = "d3dcompiler_47.dll cannot be loaded";
            return false;
        }
        compile_function = library.symbol<Compile>("D3DCompile");
    }
    if (compile_function == nullptr) {
        error = "d3dcompiler_47.dll has no D3DCompile";
        return false;
    }
    ID3DBlob* blob = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT result = compile_function(source.data(), source.size(), "suite", macros, nullptr, entry,
                                            target.c_str(), 0, 0, &blob, &errors);
    if (FAILED(result)) {
        error = "compiling " + target + " failed: " + hr(result);
        if (errors != nullptr) {
            error += " " + std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
            errors->Release();
        }
        return false;
    }
    if (errors != nullptr) {
        errors->Release();
    }
    code.assign(static_cast<const std::uint8_t*>(blob->GetBufferPointer()),
                static_cast<const std::uint8_t*>(blob->GetBufferPointer()) + blob->GetBufferSize());
    blob->Release();
    return true;
}

namespace {

template <typename Shader, typename Create>
Com<Shader> make_shader(D3d11Device& device, const std::string& source, const char* stage, std::string& error,
                        const char* entry, const D3D_SHADER_MACRO* macros, Create create) {
    std::vector<std::uint8_t> code;
    if (!D3d11Tools::compile(source, entry, D3d11Tools::profile(device, stage), code, error, macros)) {
        return Com<Shader>();
    }
    Com<Shader> shader;
    const HRESULT result = create(code, shader.put());
    if (FAILED(result)) {
        error = std::string("creating the ") + stage + " shader failed: " + D3d11Tools::hr(result);
        return Com<Shader>();
    }
    return shader;
}

}  // namespace

Com<ID3D11VertexShader> D3d11Tools::vs(D3d11Device& device, const std::string& source, std::string& error,
                                       const char* entry, const D3D_SHADER_MACRO* macros) {
    return make_shader<ID3D11VertexShader>(device, source, "vs", error, entry, macros, [&](auto& code, auto** out) {
        return device.device->CreateVertexShader(code.data(), code.size(), nullptr, out);
    });
}

Com<ID3D11PixelShader> D3d11Tools::ps(D3d11Device& device, const std::string& source, std::string& error,
                                      const char* entry, const D3D_SHADER_MACRO* macros) {
    return make_shader<ID3D11PixelShader>(device, source, "ps", error, entry, macros, [&](auto& code, auto** out) {
        return device.device->CreatePixelShader(code.data(), code.size(), nullptr, out);
    });
}

Com<ID3D11GeometryShader> D3d11Tools::gs(D3d11Device& device, const std::string& source, std::string& error,
                                         const char* entry, const D3D_SHADER_MACRO* macros) {
    return make_shader<ID3D11GeometryShader>(device, source, "gs", error, entry, macros, [&](auto& code, auto** out) {
        return device.device->CreateGeometryShader(code.data(), code.size(), nullptr, out);
    });
}

Com<ID3D11HullShader> D3d11Tools::hs(D3d11Device& device, const std::string& source, std::string& error,
                                     const char* entry, const D3D_SHADER_MACRO* macros) {
    return make_shader<ID3D11HullShader>(device, source, "hs", error, entry, macros, [&](auto& code, auto** out) {
        return device.device->CreateHullShader(code.data(), code.size(), nullptr, out);
    });
}

Com<ID3D11DomainShader> D3d11Tools::ds(D3d11Device& device, const std::string& source, std::string& error,
                                       const char* entry, const D3D_SHADER_MACRO* macros) {
    return make_shader<ID3D11DomainShader>(device, source, "ds", error, entry, macros, [&](auto& code, auto** out) {
        return device.device->CreateDomainShader(code.data(), code.size(), nullptr, out);
    });
}

Com<ID3D11ComputeShader> D3d11Tools::cs(D3d11Device& device, const std::string& source, std::string& error,
                                        const char* entry, const D3D_SHADER_MACRO* macros) {
    return make_shader<ID3D11ComputeShader>(device, source, "cs", error, entry, macros, [&](auto& code, auto** out) {
        return device.device->CreateComputeShader(code.data(), code.size(), nullptr, out);
    });
}

std::vector<std::uint8_t> D3d11Tools::vs_code(D3d11Device& device, const std::string& source, std::string& error,
                                              const char* entry) {
    std::vector<std::uint8_t> code;
    if (!compile(source, entry, profile(device, "vs"), code, error)) {
        code.clear();
    }
    return code;
}

Com<ID3D11Buffer> D3d11Tools::buffer(D3d11Device& device, UINT size, D3D11_USAGE usage, UINT bind, UINT cpu,
                                     UINT misc, UINT stride, const void* initial, HRESULT* result) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = size;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    desc.MiscFlags = misc;
    desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = initial;
    Com<ID3D11Buffer> out;
    const HRESULT created = device.device->CreateBuffer(&desc, initial != nullptr ? &data : nullptr, out.put());
    if (result != nullptr) {
        *result = created;
    }
    return out;
}

D3D11_TEXTURE2D_DESC D3d11Tools::desc2d(UINT width, UINT height, DXGI_FORMAT format, UINT bind, D3D11_USAGE usage,
                                        UINT cpu, UINT mips, UINT array_size, UINT samples) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mips;
    desc.ArraySize = array_size;
    desc.Format = format;
    desc.SampleDesc.Count = samples;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    return desc;
}

Com<ID3D11Texture2D> D3d11Tools::texture(D3d11Device& device, const D3D11_TEXTURE2D_DESC& desc,
                                         const D3D11_SUBRESOURCE_DATA* initial, HRESULT* result) {
    Com<ID3D11Texture2D> out;
    const HRESULT created = device.device->CreateTexture2D(&desc, initial, out.put());
    if (result != nullptr) {
        *result = created;
    }
    return out;
}

Com<ID3D11Texture2D> D3d11Tools::target(D3d11Device& device, UINT width, UINT height, DXGI_FORMAT format,
                                        UINT extra_bind) {
    return texture(device, desc2d(width, height, format, D3D11_BIND_RENDER_TARGET | extra_bind));
}

Com<ID3D11RenderTargetView> D3d11Tools::rtv(D3d11Device& device, ID3D11Resource* resource, DXGI_FORMAT format) {
    Com<ID3D11RenderTargetView> out;
    if (format == DXGI_FORMAT_UNKNOWN) {
        device.device->CreateRenderTargetView(resource, nullptr, out.put());
        return out;
    }
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    device.device->CreateRenderTargetView(resource, &desc, out.put());
    return out;
}

Com<ID3D11ShaderResourceView> D3d11Tools::srv(D3d11Device& device, ID3D11Resource* resource, DXGI_FORMAT format) {
    Com<ID3D11ShaderResourceView> out;
    if (format == DXGI_FORMAT_UNKNOWN) {
        device.device->CreateShaderResourceView(resource, nullptr, out.put());
        return out;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipLevels = 1;
    device.device->CreateShaderResourceView(resource, &desc, out.put());
    return out;
}

Com<ID3D11UnorderedAccessView> D3d11Tools::uav(D3d11Device& device, ID3D11Resource* resource, DXGI_FORMAT format) {
    Com<ID3D11UnorderedAccessView> out;
    if (format == DXGI_FORMAT_UNKNOWN) {
        device.device->CreateUnorderedAccessView(resource, nullptr, out.put());
        return out;
    }
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    device.device->CreateUnorderedAccessView(resource, &desc, out.put());
    return out;
}

Com<ID3D11UnorderedAccessView> D3d11Tools::raw_uav(D3d11Device& device, ID3D11Buffer* buffer, UINT flags) {
    D3D11_BUFFER_DESC bd{};
    buffer->GetDesc(&bd);
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.NumElements = bd.ByteWidth / 4;
    desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW | flags;
    Com<ID3D11UnorderedAccessView> out;
    device.device->CreateUnorderedAccessView(buffer, &desc, out.put());
    return out;
}

Com<ID3D11UnorderedAccessView> D3d11Tools::structured_uav(D3d11Device& device, ID3D11Buffer* buffer, UINT elements,
                                                          UINT flags) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    desc.Buffer.NumElements = elements;
    desc.Buffer.Flags = flags;
    Com<ID3D11UnorderedAccessView> out;
    device.device->CreateUnorderedAccessView(buffer, &desc, out.put());
    return out;
}

Com<ID3D11ShaderResourceView> D3d11Tools::raw_srv(D3d11Device& device, ID3D11Buffer* buffer) {
    D3D11_BUFFER_DESC bd{};
    buffer->GetDesc(&bd);
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    desc.BufferEx.NumElements = bd.ByteWidth / 4;
    desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    Com<ID3D11ShaderResourceView> out;
    device.device->CreateShaderResourceView(buffer, &desc, out.put());
    return out;
}

Com<ID3D11ShaderResourceView> D3d11Tools::structured_srv(D3d11Device& device, ID3D11Buffer* buffer, UINT first,
                                                         UINT elements) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = first;
    desc.Buffer.NumElements = elements;
    Com<ID3D11ShaderResourceView> out;
    device.device->CreateShaderResourceView(buffer, &desc, out.put());
    return out;
}

bool D3d11Tools::read_buffer(D3d11Device& device, ID3D11Buffer* buffer, std::vector<std::uint8_t>& out,
                             std::string& error) {
    D3D11_BUFFER_DESC desc{};
    buffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    desc.StructureByteStride = 0;
    Com<ID3D11Buffer> staging;
    HRESULT result = device.device->CreateBuffer(&desc, nullptr, staging.put());
    if (FAILED(result)) {
        error = "staging buffer creation failed: " + hr(result);
        return false;
    }
    device.context->CopyResource(staging.get(), buffer);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map of the staging buffer failed: " + hr(result);
        return false;
    }
    out.assign(static_cast<const std::uint8_t*>(mapped.pData), static_cast<const std::uint8_t*>(mapped.pData) + desc.ByteWidth);
    device.context->Unmap(staging.get(), 0);
    return true;
}

std::vector<std::uint32_t> D3d11Tools::read_words(D3d11Device& device, ID3D11Buffer* buffer, std::string& error) {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint32_t> words;
    if (read_buffer(device, buffer, bytes, error)) {
        words.resize(bytes.size() / 4);
        std::memcpy(words.data(), bytes.data(), words.size() * 4);
    }
    return words;
}

bool D3d11Tools::read_texture(D3d11Device& device, ID3D11Texture2D* texture, UINT subresource, TexelImage& out,
                              std::string& error) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    if (desc.SampleDesc.Count > 1) {
        error = "a multisampled texture cannot be read; resolve it first";
        return false;
    }
    const UINT mip = subresource % desc.MipLevels;
    const UINT width = std::max(1u, desc.Width >> mip);
    const UINT height = std::max(1u, desc.Height >> mip);
    const bool block = FormatCodec::is_block(desc.Format);
    const int element = FormatCodec::element_bytes(desc.Format);
    if (element == 0) {
        error = "no element size known for the format";
        return false;
    }
    out.width = width;
    out.height = height;
    out.depth = 1;
    out.rows = block ? (height + 3) / 4 : height;
    out.row_bytes = block ? ((width + 3) / 4) * static_cast<UINT>(element) : width * static_cast<UINT>(element);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Com<ID3D11Texture2D> staging;
    HRESULT result = device.device->CreateTexture2D(&desc, nullptr, staging.put());
    if (FAILED(result)) {
        error = "staging texture creation failed: " + hr(result);
        return false;
    }
    device.context->CopyResource(staging.get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.context->Map(staging.get(), subresource, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map of the staging texture failed: " + hr(result);
        return false;
    }
    out.data = block_bytes(mapped.pData, out.row_bytes, out.rows, mapped.RowPitch);
    device.context->Unmap(staging.get(), subresource);
    return true;
}

bool D3d11Tools::read_volume(D3d11Device& device, ID3D11Texture3D* texture, UINT subresource, TexelImage& out,
                             std::string& error) {
    D3D11_TEXTURE3D_DESC desc{};
    texture->GetDesc(&desc);
    const UINT mip = subresource;
    const UINT width = std::max(1u, desc.Width >> mip);
    const UINT height = std::max(1u, desc.Height >> mip);
    const UINT depth = std::max(1u, desc.Depth >> mip);
    const bool block = FormatCodec::is_block(desc.Format);
    const int element = FormatCodec::element_bytes(desc.Format);
    out.width = width;
    out.height = height;
    out.depth = depth;
    out.rows = block ? (height + 3) / 4 : height;
    out.row_bytes = block ? ((width + 3) / 4) * static_cast<UINT>(element) : width * static_cast<UINT>(element);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    Com<ID3D11Texture3D> staging;
    HRESULT result = device.device->CreateTexture3D(&desc, nullptr, staging.put());
    if (FAILED(result)) {
        error = "staging volume creation failed: " + hr(result);
        return false;
    }
    device.context->CopyResource(staging.get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = device.context->Map(staging.get(), subresource, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) {
        error = "Map of the staging volume failed: " + hr(result);
        return false;
    }
    out.data.clear();
    for (UINT z = 0; z < depth; ++z) {
        const std::vector<std::uint8_t> slice = block_bytes(static_cast<const std::uint8_t*>(mapped.pData) +
                                                                static_cast<std::size_t>(z) * mapped.DepthPitch,
                                                            out.row_bytes, out.rows, mapped.RowPitch);
        out.data.insert(out.data.end(), slice.begin(), slice.end());
    }
    device.context->Unmap(staging.get(), subresource);
    return true;
}

bool D3d11Tools::read_pixels(D3d11Device& device, ID3D11Texture2D* texture, std::vector<std::uint8_t>& out,
                             std::string& error) {
    TexelImage image;
    if (!read_texture(device, texture, 0, image, error)) {
        return false;
    }
    out = std::move(image.data);
    return true;
}

std::vector<std::uint8_t> D3d11Tools::block_bytes(const void* data, UINT row_bytes, UINT rows, UINT pitch) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(row_bytes) * rows);
    for (UINT row = 0; row < rows; ++row) {
        std::memcpy(out.data() + static_cast<std::size_t>(row) * row_bytes,
                    static_cast<const std::uint8_t*>(data) + static_cast<std::size_t>(row) * pitch, row_bytes);
    }
    return out;
}

void D3d11Tools::set_viewport(D3d11Device& device, UINT width, UINT height) {
    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    device.context->RSSetViewports(1, &viewport);
}

void D3d11Tools::clear(D3d11Device& device, ID3D11RenderTargetView* view, float r, float g, float b, float a) {
    const float color[4] = {r, g, b, a};
    device.context->ClearRenderTargetView(view, color);
}

bool D3d11Tools::draw_fullscreen(D3d11Device& device, ID3D11PixelShader* pixel_shader, ID3D11RenderTargetView* view,
                                 UINT width, UINT height, std::string& error) {
    Com<ID3D11VertexShader> vertex = vs(device, kFullscreenVs, error);
    if (!vertex) {
        return false;
    }
    ID3D11RenderTargetView* views[1] = {view};
    device.context->OMSetRenderTargets(1, views, nullptr);
    set_viewport(device, width, height);
    device.context->IASetInputLayout(nullptr);
    device.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    device.context->VSSetShader(vertex.get(), nullptr, 0);
    device.context->PSSetShader(pixel_shader, nullptr, 0);
    device.context->Draw(3, 0);
    return true;
}

void D3d11Tools::reset_state(D3d11Device& device) {
    device.context->ClearState();
    device.context->Flush();
}

bool D3d11Tools::wait_event(D3d11Device& device, ID3D11DeviceContext* context, std::string& error) {
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    Com<ID3D11Query> query;
    if (FAILED(device.device->CreateQuery(&desc, query.put()))) {
        error = "CreateQuery(EVENT) failed";
        return false;
    }
    context->End(query.get());
    context->Flush();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    BOOL done = FALSE;
    HRESULT result = S_FALSE;
    while ((result = context->GetData(query.get(), &done, sizeof(done), 0)) == S_FALSE) {
        if (std::chrono::steady_clock::now() > deadline) {
            error = "the event query did not complete in 60 s";
            return false;
        }
        ::SwitchToThread();
    }
    if (FAILED(result)) {
        error = "GetData(EVENT) failed: " + hr(result);
        return false;
    }
    return true;
}

}  // namespace lsw::suites
