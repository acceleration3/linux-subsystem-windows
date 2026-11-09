#include "render/d3d11/rig.hpp"

#include "render/d3d/convert.hpp"

namespace lsw::suites::render::d3d11 {

D3D11_RASTERIZER_DESC rdesc(D3D11_FILL_MODE fill, D3D11_CULL_MODE cull) {
    D3D11_RASTERIZER_DESC desc{};
    desc.FillMode = fill;
    desc.CullMode = cull;
    desc.DepthClipEnable = TRUE;
    return desc;
}

D3D11_BLEND_DESC bdesc() {
    D3D11_BLEND_DESC desc{};
    for (auto& target : desc.RenderTarget) {
        target.SrcBlend = D3D11_BLEND_ONE;
        target.DestBlend = D3D11_BLEND_ZERO;
        target.BlendOp = D3D11_BLEND_OP_ADD;
        target.SrcBlendAlpha = D3D11_BLEND_ONE;
        target.DestBlendAlpha = D3D11_BLEND_ZERO;
        target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    return desc;
}

D3D11_DEPTH_STENCIL_DESC ddesc(bool enable, D3D11_COMPARISON_FUNC func, bool write) {
    D3D11_DEPTH_STENCIL_DESC desc{};
    desc.DepthEnable = enable;
    desc.DepthFunc = func;
    desc.DepthWriteMask = write ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    desc.StencilReadMask = 0xff;
    desc.StencilWriteMask = 0xff;
    desc.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, D3D11_COMPARISON_ALWAYS};
    desc.BackFace = desc.FrontFace;
    return desc;
}

D3D11_SAMPLER_DESC sdesc(D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE address) {
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = filter;
    desc.AddressU = desc.AddressV = desc.AddressW = address;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    return desc;
}

D3D11_INPUT_ELEMENT_DESC elem(const char* name, DXGI_FORMAT format, UINT offset, UINT slot, UINT index, bool instance, UINT step) {
    return {name, index, format, slot, offset, instance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, step};
}

rr::CaseSpec spec(const char* group, const char* name) { return rr::CaseSpec(group, name); }

bool Rig::fail(const std::string& text) {
    error = text;
    return false;
}

bool Rig::ok(HRESULT result, const char* what) {
    if (FAILED(result)) {
        error = std::string(what) + ": " + D3d11Tools::hr(result);
        return false;
    }
    return true;
}

namespace {
std::string profile_of(Rig& rig, const char* stage, const char* profile) {
    return profile != nullptr ? std::string(profile) : D3d11Tools::profile(rig.kit.dev, stage);
}
}  // namespace

Com<ID3D11VertexShader> Rig::vs(const std::string& src, Bytes* code, const char* profile, const char* entry) {
    Bytes local;
    Bytes& bytes = code != nullptr ? *code : local;
    if (!D3d11Tools::compile(src, entry, profile_of(*this, "vs", profile), bytes, error)) {
        return {};
    }
    Com<ID3D11VertexShader> out;
    if (!ok(dev->CreateVertexShader(bytes.data(), bytes.size(), nullptr, out.put()), "CreateVertexShader")) {
        return {};
    }
    return out;
}

Com<ID3D11PixelShader> Rig::ps(const std::string& src, const char* profile, const char* entry) {
    Bytes bytes;
    if (!D3d11Tools::compile(src, entry, profile_of(*this, "ps", profile), bytes, error)) {
        return {};
    }
    Com<ID3D11PixelShader> out;
    if (!ok(dev->CreatePixelShader(bytes.data(), bytes.size(), nullptr, out.put()), "CreatePixelShader")) {
        return {};
    }
    return out;
}

Com<ID3D11GeometryShader> Rig::gs(const std::string& src, const char* profile, const char* entry) {
    Bytes bytes;
    if (!D3d11Tools::compile(src, entry, profile_of(*this, "gs", profile), bytes, error)) {
        return {};
    }
    Com<ID3D11GeometryShader> out;
    if (!ok(dev->CreateGeometryShader(bytes.data(), bytes.size(), nullptr, out.put()), "CreateGeometryShader")) {
        return {};
    }
    return out;
}

Com<ID3D11GeometryShader> Rig::gs_so(const std::string& src, const std::vector<D3D11_SO_DECLARATION_ENTRY>& decl,
                                     const std::vector<UINT>& strides, UINT rasterized, const char* entry) {
    Bytes bytes;
    if (!D3d11Tools::compile(src, entry, profile_of(*this, "gs", nullptr), bytes, error)) {
        return {};
    }
    Com<ID3D11GeometryShader> out;
    if (!ok(dev->CreateGeometryShaderWithStreamOutput(bytes.data(), bytes.size(), decl.data(), static_cast<UINT>(decl.size()), strides.data(),
                                                      static_cast<UINT>(strides.size()), rasterized, nullptr, out.put()),
            "CreateGeometryShaderWithStreamOutput")) {
        return {};
    }
    return out;
}

Com<ID3D11GeometryShader> Rig::vs_so(const std::string& src, const std::vector<D3D11_SO_DECLARATION_ENTRY>& decl, const std::vector<UINT>& strides,
                                     UINT rasterized, Bytes* code) {
    Bytes local;
    Bytes& bytes = code != nullptr ? *code : local;
    if (!D3d11Tools::compile(src, "main", profile_of(*this, "vs", nullptr), bytes, error)) {
        return {};
    }
    Com<ID3D11GeometryShader> out;
    if (!ok(dev->CreateGeometryShaderWithStreamOutput(bytes.data(), bytes.size(), decl.data(), static_cast<UINT>(decl.size()), strides.data(),
                                                      static_cast<UINT>(strides.size()), rasterized, nullptr, out.put()),
            "CreateGeometryShaderWithStreamOutput(VS)")) {
        return {};
    }
    return out;
}

Com<ID3D11HullShader> Rig::hs(const std::string& src, const char* entry) {
    Bytes bytes;
    if (!D3d11Tools::compile(src, entry, "hs_5_0", bytes, error)) {
        return {};
    }
    Com<ID3D11HullShader> out;
    if (!ok(dev->CreateHullShader(bytes.data(), bytes.size(), nullptr, out.put()), "CreateHullShader")) {
        return {};
    }
    return out;
}

Com<ID3D11DomainShader> Rig::ds(const std::string& src, const char* entry) {
    Bytes bytes;
    if (!D3d11Tools::compile(src, entry, "ds_5_0", bytes, error)) {
        return {};
    }
    Com<ID3D11DomainShader> out;
    if (!ok(dev->CreateDomainShader(bytes.data(), bytes.size(), nullptr, out.put()), "CreateDomainShader")) {
        return {};
    }
    return out;
}

Com<ID3D11ComputeShader> Rig::cs(const std::string& src, const char* profile, const char* entry) {
    Bytes bytes;
    if (!D3d11Tools::compile(src, entry, profile_of(*this, "cs", profile), bytes, error)) {
        return {};
    }
    Com<ID3D11ComputeShader> out;
    if (!ok(dev->CreateComputeShader(bytes.data(), bytes.size(), nullptr, out.put()), "CreateComputeShader")) {
        return {};
    }
    return out;
}

Com<ID3D11InputLayout> Rig::layout(const std::vector<D3D11_INPUT_ELEMENT_DESC>& elements, const Bytes& code) {
    Com<ID3D11InputLayout> out;
    if (!ok(dev->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), code.data(), code.size(), out.put()), "CreateInputLayout")) {
        return {};
    }
    return out;
}

Com<ID3D11Buffer> Rig::buf(UINT size, UINT bind, const void* data, D3D11_USAGE usage, UINT cpu, UINT misc, UINT stride) {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = size;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    desc.MiscFlags = misc;
    desc.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = data;
    Com<ID3D11Buffer> out;
    if (!ok(dev->CreateBuffer(&desc, data != nullptr ? &init : nullptr, out.put()), "CreateBuffer")) {
        return {};
    }
    return out;
}

Com<ID3D11Buffer> Rig::cbuf(const void* data, UINT size) {
    const UINT padded = (size + 15) & ~15u;
    Bytes copy(padded, 0);
    std::memcpy(copy.data(), data, size);
    return buf(padded, D3D11_BIND_CONSTANT_BUFFER, copy.data());
}

Com<ID3D11Texture2D> Rig::tex(UINT w, UINT h, DXGI_FORMAT format, UINT bind, UINT mips, UINT array, UINT samples, const D3D11_SUBRESOURCE_DATA* init,
                              UINT misc) {
    D3D11_TEXTURE2D_DESC desc = D3d11Tools::desc2d(w, h, format, bind, D3D11_USAGE_DEFAULT, 0, mips, array, samples);
    desc.MiscFlags = misc;
    Com<ID3D11Texture2D> out;
    if (!ok(dev->CreateTexture2D(&desc, init, out.put()), "CreateTexture2D")) {
        return {};
    }
    return out;
}

Com<ID3D11Texture2D> Rig::tex_data(UINT w, UINT h, DXGI_FORMAT format, const Bytes& pixels, UINT bind) {
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = static_cast<UINT>(pixels.size() / h);
    return tex(w, h, format, bind, 1, 1, 1, &init);
}

Com<ID3D11Texture2D> Rig::tex_chain(UINT w, UINT h, const Bytes& base, DXGI_FORMAT format, UINT array, UINT misc) {
    const unsigned levels = rr::Pattern::level_count(w, h);
    const std::vector<Bytes> chain = rr::Pattern::mip_chain(base, w, h, levels);
    std::vector<D3D11_SUBRESOURCE_DATA> data;
    for (UINT a = 0; a < array; ++a) {
        for (unsigned i = 0; i < levels; ++i) {
            D3D11_SUBRESOURCE_DATA item{};
            item.pSysMem = chain[i].data();
            item.SysMemPitch = std::max(1u, w >> i) * 4;
            data.push_back(item);
        }
    }
    return tex(w, h, format, D3D11_BIND_SHADER_RESOURCE, levels, array, 1, data.data(), misc);
}

Com<ID3D11ShaderResourceView> Rig::srv(ID3D11Resource* resource, const D3D11_SHADER_RESOURCE_VIEW_DESC* desc) {
    Com<ID3D11ShaderResourceView> out;
    if (!ok(dev->CreateShaderResourceView(resource, desc, out.put()), "CreateShaderResourceView")) {
        return {};
    }
    return out;
}

Com<ID3D11ShaderResourceView> Rig::srv_buffer(ID3D11Buffer* buffer, DXGI_FORMAT format, UINT first, UINT count) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = first;
    desc.Buffer.NumElements = count;
    return srv(buffer, &desc);
}

Com<ID3D11RenderTargetView> Rig::rtv(ID3D11Resource* resource, const D3D11_RENDER_TARGET_VIEW_DESC* desc) {
    Com<ID3D11RenderTargetView> out;
    if (!ok(dev->CreateRenderTargetView(resource, desc, out.put()), "CreateRenderTargetView")) {
        return {};
    }
    return out;
}

Com<ID3D11UnorderedAccessView> Rig::uav(ID3D11Resource* resource, const D3D11_UNORDERED_ACCESS_VIEW_DESC* desc) {
    Com<ID3D11UnorderedAccessView> out;
    if (!ok(dev->CreateUnorderedAccessView(resource, desc, out.put()), "CreateUnorderedAccessView")) {
        return {};
    }
    return out;
}

Com<ID3D11DepthStencilView> Rig::dsv(ID3D11Resource* resource, const D3D11_DEPTH_STENCIL_VIEW_DESC* desc) {
    Com<ID3D11DepthStencilView> out;
    if (!ok(dev->CreateDepthStencilView(resource, desc, out.put()), "CreateDepthStencilView")) {
        return {};
    }
    return out;
}

Com<ID3D11SamplerState> Rig::smp(const D3D11_SAMPLER_DESC& desc) {
    Com<ID3D11SamplerState> out;
    if (!ok(dev->CreateSamplerState(&desc, out.put()), "CreateSamplerState")) {
        return {};
    }
    return out;
}

Com<ID3D11BlendState> Rig::blend(const D3D11_BLEND_DESC& desc) {
    Com<ID3D11BlendState> out;
    if (!ok(dev->CreateBlendState(&desc, out.put()), "CreateBlendState")) {
        return {};
    }
    return out;
}

Com<ID3D11RasterizerState> Rig::rast(const D3D11_RASTERIZER_DESC& desc) {
    Com<ID3D11RasterizerState> out;
    if (!ok(dev->CreateRasterizerState(&desc, out.put()), "CreateRasterizerState")) {
        return {};
    }
    return out;
}

Com<ID3D11DepthStencilState> Rig::dss(const D3D11_DEPTH_STENCIL_DESC& desc) {
    Com<ID3D11DepthStencilState> out;
    if (!ok(dev->CreateDepthStencilState(&desc, out.put()), "CreateDepthStencilState")) {
        return {};
    }
    return out;
}

bool Rig::color(Target& out, UINT w, UINT h, DXGI_FORMAT format, UINT samples) {
    return kit.make_target(out, w, h, format, samples, error);
}

bool Rig::depth(Depth& out, UINT w, UINT h, DXGI_FORMAT format, UINT samples) {
    HRESULT result = S_OK;
    DXGI_FORMAT texture_format = format;
    if (format == DXGI_FORMAT_D32_FLOAT) {
        texture_format = DXGI_FORMAT_R32_TYPELESS;
    } else if (format == DXGI_FORMAT_D16_UNORM) {
        texture_format = DXGI_FORMAT_R16_TYPELESS;
    } else if (format == DXGI_FORMAT_D24_UNORM_S8_UINT) {
        texture_format = DXGI_FORMAT_R24G8_TYPELESS;
    } else if (format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT) {
        texture_format = DXGI_FORMAT_R32G8X24_TYPELESS;
    }
    out.texture = D3d11Tools::texture(kit.dev, D3d11Tools::desc2d(w, h, texture_format, D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
                                                                  D3D11_USAGE_DEFAULT, 0, 1, 1, samples), nullptr, &result);
    if (!out.texture) {
        return ok(result, "depth texture");
    }
    D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = samples > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMS : D3D11_DSV_DIMENSION_TEXTURE2D;
    out.view = dsv(out.texture.get(), &desc);
    out.format = texture_format;
    out.width = w;
    out.height = h;
    return static_cast<bool>(out.view);
}

void Rig::bind(Target& target, Depth* depth_target, const float* clear, bool clear_depth) {
    static const float kClear[4] = {0.05f, 0.06f, 0.1f, 1.0f};
    ID3D11RenderTargetView* view = target.view.get();
    ctx->OMSetRenderTargets(1, &view, depth_target != nullptr ? depth_target->view.get() : nullptr);
    ctx->ClearRenderTargetView(view, clear != nullptr ? clear : kClear);
    if (depth_target != nullptr && clear_depth) {
        UINT flags = D3D11_CLEAR_DEPTH;
        if (depth_target->format == DXGI_FORMAT_R24G8_TYPELESS || depth_target->format == DXGI_FORMAT_R32G8X24_TYPELESS) {
            flags |= D3D11_CLEAR_STENCIL;
        }
        ctx->ClearDepthStencilView(depth_target->view.get(), flags, 1.0f, 0);
    }
    viewport(0, 0, static_cast<float>(target.width), static_cast<float>(target.height));
    if (!default_rs_) {
        default_rs_ = rast(rdesc());
    }
    ctx->RSSetState(default_rs_.get());
}

void Rig::viewport(float x, float y, float w, float h, float min_depth, float max_depth) {
    D3D11_VIEWPORT vp{x, y, w, h, min_depth, max_depth};
    ctx->RSSetViewports(1, &vp);
}

void Rig::draw_fs(ID3D11PixelShader* pixel) {
    if (!fs_vs_) {
        fs_vs_ = vs(Fs::kVs);
    }
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->VSSetShader(fs_vs_.get(), nullptr, 0);
    ctx->PSSetShader(pixel, nullptr, 0);
    ctx->Draw(3, 0);
}

void Rig::unbind_srvs() {
    ID3D11ShaderResourceView* none[16] = {};
    ctx->PSSetShaderResources(0, 16, none);
    ctx->VSSetShaderResources(0, 16, none);
}

void Rig::reset() { ctx->ClearState(); }

bool Rig::shot(Cap& out, Target& target, const char* label) {
    rr::Image image;
    if (!kit.read_color(target, image, error)) {
        return false;
    }
    out.add(label, std::move(image));
    return true;
}

bool Rig::read_texels(ID3D11Texture2D* texture, unsigned subresource, TexelImage& out) {
    return D3d11Tools::read_texture(kit.dev, texture, subresource, out, error);
}

bool Rig::read_image(ID3D11Texture2D* texture, DXGI_FORMAT format, unsigned subresource, rr::Image& out) {
    TexelImage texels;
    if (!read_texels(texture, subresource, texels)) {
        return false;
    }
    rr::PixelKind kind;
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM: kind = rr::PixelKind::Rgba8Unorm; break;
        case DXGI_FORMAT_B8G8R8A8_UNORM: kind = rr::PixelKind::Bgra8Unorm; break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: kind = rr::PixelKind::Rgba16Float; break;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: kind = rr::PixelKind::Rgba32Float; break;
        case DXGI_FORMAT_R32_FLOAT: kind = rr::PixelKind::R32Float; break;
        case DXGI_FORMAT_R32G32B32A32_UINT: kind = rr::PixelKind::Rgba32Uint; break;
        case DXGI_FORMAT_R8_UINT: kind = rr::PixelKind::R8Uint; break;
        case DXGI_FORMAT_R8_UNORM: kind = rr::PixelKind::R8Unorm; break;
        case DXGI_FORMAT_R8G8_UNORM: kind = rr::PixelKind::Rg8Unorm; break;
        case DXGI_FORMAT_R16_FLOAT: kind = rr::PixelKind::R16Float; break;
        case DXGI_FORMAT_R16G16_FLOAT: kind = rr::PixelKind::Rg16Float; break;
        case DXGI_FORMAT_R16_UNORM: kind = rr::PixelKind::R16Unorm; break;
        case DXGI_FORMAT_R16_UINT: kind = rr::PixelKind::R16Uint; break;
        case DXGI_FORMAT_R32G32_FLOAT: kind = rr::PixelKind::Rg32Float; break;
        case DXGI_FORMAT_R16G16B16A16_UNORM: kind = rr::PixelKind::Rgba16Unorm; break;
        case DXGI_FORMAT_R8G8B8A8_UINT: kind = rr::PixelKind::Rgba8Uint; break;
        case DXGI_FORMAT_R16G16B16A16_UINT: kind = rr::PixelKind::Rgba16Uint; break;
        default: return fail("no image kind for the readback format");
    }
    rr::Image image(texels.width, texels.height, kind);
    if (texels.data.size() != image.bytes().size()) {
        return fail("readback size does not match the image kind");
    }
    std::memcpy(image.bytes().data(), texels.data.data(), image.bytes().size());
    out = std::move(image);
    return true;
}

bool Rig::depth_image(Depth& depth, rr::Image& out) {
    TexelImage texels;
    if (!read_texels(depth.texture.get(), 0, texels)) {
        return false;
    }
    rr::Image image(texels.width, texels.height, rr::PixelKind::Depth32Float);
    const std::size_t step = texels.row_bytes / texels.width;
    for (UINT y = 0; y < texels.height; ++y) {
        for (UINT x = 0; x < texels.width; ++x) {
            const std::uint8_t* t = texels.data.data() + (static_cast<std::size_t>(y) * texels.width + x) * step;
            float value = 0;
            if (depth.format == DXGI_FORMAT_R16_TYPELESS) {
                std::uint16_t v;
                std::memcpy(&v, t, 2);
                value = static_cast<float>(v) / 65535.0f;
            } else if (depth.format == DXGI_FORMAT_R24G8_TYPELESS) {
                std::uint32_t v;
                std::memcpy(&v, t, 4);
                value = static_cast<float>(v & 0xffffffu) / 16777215.0f;
            } else {
                std::memcpy(&value, t, 4);
            }
            std::memcpy(image.row(y) + x * 4, &value, 4);
        }
    }
    out = std::move(image);
    return true;
}

std::string Rig::debug_messages() {
    ID3D11InfoQueue* raw = nullptr;
    dev->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&raw));
    Com<ID3D11InfoQueue> queue(raw);
    std::string text;
    if (!queue) {
        return text;
    }
    const UINT64 count = queue->GetNumStoredMessages();
    int shown = 0;
    for (UINT64 i = 0; i < count && shown < 4; ++i) {
        SIZE_T size = 0;
        queue->GetMessage(i, nullptr, &size);
        std::vector<char> storage(size);
        D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        if (FAILED(queue->GetMessage(i, message, &size))) {
            continue;
        }
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
            text += std::string(message->pDescription, message->DescriptionByteLength > 0 ? message->DescriptionByteLength - 1 : 0) + " | ";
            ++shown;
        }
    }
    queue->ClearStoredMessages();
    return text;
}

bool Rig::read_bytes(ID3D11Buffer* buffer, Bytes& out) { return D3d11Tools::read_buffer(kit.dev, buffer, out, error); }

std::string FnScene::unsupported(Kit& kit) {
    if (!needs_) {
        return {};
    }
    Rig rig(kit);
    return needs_(rig);
}

bool FnScene::draw(Kit& kit, rr::CaptureSet& out, std::string& error) {
    Rig rig(kit);
    if (!body_(rig, out)) {
        error = rig.error.empty() ? "the scene failed" : rig.error;
        return false;
    }
    if (kit.debug) {
        const std::string messages = rig.debug_messages();
        if (!messages.empty()) {
            error = "debug layer: " + messages;
            return false;
        }
    }
    return true;
}

Add::Add(rr::CaseSpec spec, Body body, Needs needs) {
    spec.source_hash(LSW_RENDER_SOURCE_HASH);
    rr::CaseTable<Kit>::instance().add(std::move(spec), [body = std::move(body), needs = std::move(needs)] {
        return std::unique_ptr<rr::Scene<Kit>>(new FnScene(body, needs));
    });
}

std::string need_format(Rig& rig, DXGI_FORMAT format, UINT bits, const char* what) {
    UINT support = 0;
    if (FAILED(rig.dev->CheckFormatSupport(format, &support)) || (support & bits) != bits) {
        return std::string("format support for ") + what;
    }
    return {};
}

std::string need_msaa(Rig& rig, DXGI_FORMAT format, UINT samples) {
    UINT levels = 0;
    if (FAILED(rig.dev->CheckMultisampleQualityLevels(format, samples, &levels)) || levels == 0) {
        return std::to_string(samples) + "x multisampling for the format";
    }
    return {};
}

std::string need_fl(Rig& rig, D3D_FEATURE_LEVEL level) {
    return rig.kit.dev.level >= level ? std::string() : std::string("the required feature level");
}

}  // namespace lsw::suites::render::d3d11
