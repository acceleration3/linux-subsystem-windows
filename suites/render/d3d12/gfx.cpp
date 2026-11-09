#include "render/d3d12/gfx.hpp"

#include <cfloat>
#include <cmath>
#include <cstring>

namespace lsw::suites::render::d3d12 {

bool fail_with(std::string& error, const std::string& text) {
    error = text;
    return false;
}

std::string lacks_options(Kit&, const char*, const std::function<bool(Kit&)>&) { return {}; }

D3D12_VIEWPORT viewport_of(UINT width, UINT height) { return {0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1}; }

D3D12_RECT rect_of(UINT width, UINT height) { return {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)}; }

rr::Mat4 camera(float aspect, float distance, float yaw, float pitch) {
    return rr::Mat4::perspective_d3d(1.0f, aspect, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -distance) * rr::Mat4::rotate_x(pitch) *
           rr::Mat4::rotate_y(yaw);
}

void set_cbv_root(ID3D12GraphicsCommandList* list, const rr::Mat4& mvp, const float p0[4], const float p1[4]) {
    list->SetGraphicsRoot32BitConstants(0, 16, mvp.m, 0);
    list->SetGraphicsRoot32BitConstants(0, 4, p0, 16);
    if (p1 != nullptr) {
        list->SetGraphicsRoot32BitConstants(0, 4, p1, 20);
    }
}

namespace {

void push(MeshData& mesh, float x, float y, float z, float nx, float ny, float nz, float u, float v) {
    mesh.vertices.push_back({{x, y, z}, {nx, ny, nz}, {u, v}});
}

void quad_indices(MeshData& mesh, std::uint32_t base, bool flip) {
    const std::uint32_t order[6] = {0, 1, 2, 0, 2, 3};
    for (std::uint32_t k : order) {
        mesh.indices.push_back(base + (flip ? (4 - k) % 4 : k));
    }
}

}  // namespace

MeshData Shapes::cube() {
    MeshData mesh;
    static const float faces[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},    {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}}, {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},    {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
    };
    static const float normals[6][3] = {{0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    static const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (int f = 0; f < 6; ++f) {
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int k = 0; k < 4; ++k) {
            push(mesh, faces[f][k][0], faces[f][k][1], faces[f][k][2], normals[f][0], normals[f][1], normals[f][2], uvs[k][0], uvs[k][1]);
        }
        quad_indices(mesh, base, false);
    }
    return mesh;
}

MeshData Shapes::sphere(unsigned stacks, unsigned slices, float radius) {
    MeshData mesh;
    for (unsigned i = 0; i <= stacks; ++i) {
        const float v = static_cast<float>(i) / static_cast<float>(stacks);
        const float phi = v * 3.14159265f;
        for (unsigned j = 0; j <= slices; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(slices);
            const float theta = u * 6.2831853f;
            const float x = std::sin(phi) * std::cos(theta);
            const float y = std::cos(phi);
            const float z = std::sin(phi) * std::sin(theta);
            push(mesh, x * radius, y * radius, z * radius, x, y, z, u, v);
        }
    }
    for (unsigned i = 0; i < stacks; ++i) {
        for (unsigned j = 0; j < slices; ++j) {
            const std::uint32_t a = i * (slices + 1) + j;
            const std::uint32_t b = a + slices + 1;
            const std::uint32_t tri[6] = {a, a + 1, b, a + 1, b + 1, b};
            mesh.indices.insert(mesh.indices.end(), tri, tri + 6);
        }
    }
    return mesh;
}

MeshData Shapes::torus(unsigned rings, unsigned sides, float major, float minor) {
    MeshData mesh;
    for (unsigned i = 0; i <= rings; ++i) {
        const float u = static_cast<float>(i) / static_cast<float>(rings);
        const float a = u * 6.2831853f;
        for (unsigned j = 0; j <= sides; ++j) {
            const float v = static_cast<float>(j) / static_cast<float>(sides);
            const float b = v * 6.2831853f;
            const float nx = std::cos(b) * std::cos(a);
            const float ny = std::sin(b);
            const float nz = std::cos(b) * std::sin(a);
            push(mesh, (major + minor * std::cos(b)) * std::cos(a), minor * ny, (major + minor * std::cos(b)) * std::sin(a), nx, ny, nz, u * 4.0f, v);
        }
    }
    for (unsigned i = 0; i < rings; ++i) {
        for (unsigned j = 0; j < sides; ++j) {
            const std::uint32_t a = i * (sides + 1) + j;
            const std::uint32_t b = a + sides + 1;
            const std::uint32_t tri[6] = {a, b, a + 1, a + 1, b, b + 1};
            mesh.indices.insert(mesh.indices.end(), tri, tri + 6);
        }
    }
    return mesh;
}

MeshData Shapes::grid(unsigned cells, float extent) {
    MeshData mesh;
    for (unsigned i = 0; i <= cells; ++i) {
        for (unsigned j = 0; j <= cells; ++j) {
            const float u = static_cast<float>(j) / static_cast<float>(cells);
            const float v = static_cast<float>(i) / static_cast<float>(cells);
            const float x = (u - 0.5f) * 2.0f * extent;
            const float z = (v - 0.5f) * 2.0f * extent;
            const float h = 0.25f * std::sin(x * 3.0f) * std::cos(z * 2.0f);
            push(mesh, x, h, z, -0.25f * 3.0f * std::cos(x * 3.0f) * std::cos(z * 2.0f), 1.0f, 0.25f * 2.0f * std::sin(x * 3.0f) * std::sin(z * 2.0f), u, v);
        }
    }
    for (unsigned i = 0; i < cells; ++i) {
        for (unsigned j = 0; j < cells; ++j) {
            const std::uint32_t a = i * (cells + 1) + j;
            const std::uint32_t b = a + cells + 1;
            const std::uint32_t tri[6] = {a, b, a + 1, a + 1, b, b + 1};
            mesh.indices.insert(mesh.indices.end(), tri, tri + 6);
        }
    }
    return mesh;
}

MeshData Shapes::quad(float x0, float y0, float x1, float y1, float z) {
    MeshData mesh;
    push(mesh, x0, y1, z, 0, 0, 1, 0, 0);
    push(mesh, x0, y0, z, 0, 0, 1, 0, 1);
    push(mesh, x1, y0, z, 0, 0, 1, 1, 1);
    push(mesh, x1, y1, z, 0, 0, 1, 1, 0);
    quad_indices(mesh, 0, false);
    return mesh;
}

std::vector<D3D12_INPUT_ELEMENT_DESC> Shapes::layout() {
    return {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
}

bool Geometry::create(Kit& kit, const MeshData& mesh, bool wide_indices, std::string& error) {
    vertex = kit.make_upload(mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex), error);
    if (vertex == nullptr) {
        return false;
    }
    vertex_count = static_cast<UINT>(mesh.vertices.size());
    break_index = kit.broken("index_format");
    break_topology = kit.broken("topology");
    vbv = {vertex->GetGPUVirtualAddress(), static_cast<UINT>(mesh.vertices.size() * sizeof(Vertex)), sizeof(Vertex)};
    index_count = static_cast<UINT>(mesh.indices.size());
    if (wide_indices) {
        index = kit.make_upload(mesh.indices.data(), mesh.indices.size() * 4, error);
        ibv.Format = DXGI_FORMAT_R32_UINT;
        ibv.SizeInBytes = static_cast<UINT>(mesh.indices.size() * 4);
    } else {
        std::vector<std::uint16_t> narrow(mesh.indices.begin(), mesh.indices.end());
        index = kit.make_upload(narrow.data(), narrow.size() * 2, error);
        ibv.Format = DXGI_FORMAT_R16_UINT;
        ibv.SizeInBytes = static_cast<UINT>(narrow.size() * 2);
    }
    if (index == nullptr) {
        return false;
    }
    ibv.BufferLocation = index->GetGPUVirtualAddress();
    return true;
}

bool Geometry::create_vertices(Kit& kit, const void* data, UINT count, UINT stride, std::string& error) {
    vertex = kit.make_upload(data, static_cast<UINT64>(count) * stride, error);
    if (vertex == nullptr) {
        return false;
    }
    vertex_count = count;
    vbv = {vertex->GetGPUVirtualAddress(), count * stride, stride};
    return true;
}

void Geometry::bind(ID3D12GraphicsCommandList* list, D3D_PRIMITIVE_TOPOLOGY topology) const {
    if (break_topology) {
        topology = topology == D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST ? D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
    list->IASetPrimitiveTopology(topology);
    list->IASetVertexBuffers(0, 1, &vbv);
    if (index != nullptr) {
        D3D12_INDEX_BUFFER_VIEW view = ibv;
        if (break_index) {
            view.Format = view.Format == DXGI_FORMAT_R16_UINT ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        }
        list->IASetIndexBuffer(&view);
    }
}

void Geometry::draw(ID3D12GraphicsCommandList* list, UINT instances, UINT first_instance) const {
    list->DrawIndexedInstanced(index_count, instances, 0, 0, first_instance);
}

void Geometry::draw_vertices(ID3D12GraphicsCommandList* list, UINT instances) const { list->DrawInstanced(vertex_count, instances, 0, 0); }

DepthFormats DepthFormats::of(DXGI_FORMAT dsv) {
    switch (dsv) {
        case DXGI_FORMAT_D16_UNORM:
            return {DXGI_FORMAT_R16_TYPELESS, dsv, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_UNKNOWN, false};
        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            return {DXGI_FORMAT_R24G8_TYPELESS, dsv, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_X24_TYPELESS_G8_UINT, true};
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
            return {DXGI_FORMAT_R32G8X24_TYPELESS, dsv, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_X32_TYPELESS_G8X24_UINT, true};
        default:
            return {DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_UNKNOWN, false};
    }
}

bool Surface::color(Kit& kit, UINT w, UINT h, DXGI_FORMAT fmt, std::string& error, UINT sample_count, UINT mip_count, UINT array_size,
                    D3D12_RESOURCE_FLAGS extra, DXGI_FORMAT rtv_format) {
    D3D12_CLEAR_VALUE clear{};
    clear.Format = rtv_format != DXGI_FORMAT_UNKNOWN ? rtv_format : fmt;
    HRESULT result = S_OK;
    D3D12_RESOURCE_DESC desc =
        D3d12Tools::texture_desc(w, h, fmt, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | extra, mip_count, array_size, sample_count);
    resource = D3d12Tools::texture(kit.rig, desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, &result);
    if (resource == nullptr) {
        return fail_with(error, "render target creation: " + D3d12Tools::hr(result));
    }
    heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false, &result);
    if (heap == nullptr) {
        return fail_with(error, "RTV heap creation: " + D3d12Tools::hr(result));
    }
    view = heap->GetCPUDescriptorHandleForHeapStart();
    view_format = rtv_format != DXGI_FORMAT_UNKNOWN ? rtv_format : fmt;
    if (rtv_format != DXGI_FORMAT_UNKNOWN || array_size > 1 || mip_count > 1) {
        D3D12_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = view_format;
        if (sample_count > 1) {
            rtv.ViewDimension = array_size > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DMS;
            rtv.Texture2DMSArray.ArraySize = array_size;
        } else if (array_size > 1) {
            rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtv.Texture2DArray.ArraySize = array_size;
        } else {
            rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        }
        kit.device()->CreateRenderTargetView(resource, &rtv, view);
    } else {
        kit.device()->CreateRenderTargetView(resource, nullptr, view);
    }
    format = fmt;
    width = w;
    height = h;
    samples = sample_count;
    mips = mip_count;
    array = array_size;
    is_depth = false;
    state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    return true;
}

bool Surface::depth(Kit& kit, UINT w, UINT h, DXGI_FORMAT dsv_format, std::string& error, UINT sample_count, UINT array_size,
                    D3D12_RESOURCE_FLAGS extra) {
    const DepthFormats formats = DepthFormats::of(dsv_format);
    D3D12_CLEAR_VALUE clear{};
    clear.Format = formats.dsv;
    clear.DepthStencil.Depth = 1.0f;
    HRESULT result = S_OK;
    D3D12_RESOURCE_DESC desc = D3d12Tools::texture_desc(w, h, formats.resource, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | extra, 1, array_size, sample_count);
    resource = D3d12Tools::texture(kit.rig, desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, &result);
    if (resource == nullptr) {
        return fail_with(error, "depth texture creation: " + D3d12Tools::hr(result));
    }
    heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, &result);
    readonly_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, &result);
    if (heap == nullptr || readonly_heap == nullptr) {
        return fail_with(error, "DSV heap creation: " + D3d12Tools::hr(result));
    }
    view = heap->GetCPUDescriptorHandleForHeapStart();
    readonly_view = readonly_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DEPTH_STENCIL_VIEW_DESC dsv{};
    dsv.Format = formats.dsv;
    dsv.ViewDimension = sample_count > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    kit.device()->CreateDepthStencilView(resource, &dsv, view);
    dsv.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | (formats.has_stencil ? D3D12_DSV_FLAG_READ_ONLY_STENCIL : D3D12_DSV_FLAG_NONE);
    kit.device()->CreateDepthStencilView(resource, &dsv, readonly_view);
    format = formats.dsv;
    view_format = formats.dsv;
    width = w;
    height = h;
    samples = sample_count;
    array = array_size;
    is_depth = true;
    state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    return true;
}

void Surface::to(ID3D12GraphicsCommandList* list, D3D12_RESOURCE_STATES next) {
    if (state == next) {
        return;
    }
    D3d12Tools::move(list, resource, state, next);
    state = next;
}

void Surface::bind_target(ID3D12GraphicsCommandList* list, const Surface* depth_surface, bool read_only_depth) {
    to(list, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (depth_surface != nullptr) {
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = read_only_depth ? depth_surface->readonly_view : depth_surface->view;
        list->OMSetRenderTargets(1, &view, FALSE, &dsv);
    } else {
        list->OMSetRenderTargets(1, &view, FALSE, nullptr);
    }
}

bool Binder::create(Kit& kit, std::string& error) {
    owner = &kit;
    D3D12_DESCRIPTOR_RANGE srv_range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kSrvSlots, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE uav_range{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kUavSlots, 0, 0, kSrvSlots};
    D3D12_DESCRIPTOR_RANGE sampler_range{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, kSamplerSlots, 0, 0, 0};
    D3d12Root root;
    root.constants(32, 0).cbv(1).table({srv_range, uav_range}).table({sampler_range});
    for (UINT i = 0; i < 4; ++i) {
        D3D12_STATIC_SAMPLER_DESC fixed{};
        fixed.Filter = (i & 1) != 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        fixed.AddressU = fixed.AddressV = fixed.AddressW = i < 2 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        fixed.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        fixed.MaxLOD = FLT_MAX;
        fixed.ShaderRegister = i;
        fixed.RegisterSpace = 1;
        fixed.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        root.sampler(fixed);
    }
    root.flags(D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT);
    signature = root.build(kit.rig, error);
    if (signature == nullptr) {
        return false;
    }
    HRESULT result = S_OK;
    heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kSrvSlots + kUavSlots, true, &result);
    sampler_heap = D3d12Tools::descriptor_heap(kit.rig, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerSlots, true, &result);
    if (heap == nullptr || sampler_heap == nullptr) {
        return fail_with(error, "descriptor heap creation: " + D3d12Tools::hr(result));
    }
    increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    sampler_increment = kit.device()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_SHADER_RESOURCE_VIEW_DESC null_srv{};
    null_srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_srv.Texture2D.MipLevels = 1;
    for (UINT i = 0; i < kSrvSlots; ++i) {
        kit.device()->CreateShaderResourceView(nullptr, &null_srv, D3d12Tools::cpu_at(heap, i, increment));
    }
    D3D12_UNORDERED_ACCESS_VIEW_DESC null_uav{};
    null_uav.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    for (UINT i = 0; i < kUavSlots; ++i) {
        kit.device()->CreateUnorderedAccessView(nullptr, nullptr, &null_uav, D3d12Tools::cpu_at(heap, kSrvSlots + i, increment));
    }
    for (UINT i = 0; i < kSamplerSlots; ++i) {
        sampler(i, sampler_desc((i & 1) != 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                                i < 2 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP : D3D12_TEXTURE_ADDRESS_MODE_CLAMP));
    }
    return true;
}

D3D12_SAMPLER_DESC Binder::sampler_desc(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE mode) {
    D3D12_SAMPLER_DESC desc{};
    desc.Filter = filter;
    desc.AddressU = desc.AddressV = desc.AddressW = mode;
    desc.MaxAnisotropy = 1;
    desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    desc.MaxLOD = FLT_MAX;
    return desc;
}

void Binder::srv(UINT slot, ID3D12Resource* resource, const D3D12_SHADER_RESOURCE_VIEW_DESC* desc) {
    if (desc != nullptr && owner->under_test) {
        D3D12_SHADER_RESOURCE_VIEW_DESC changed = *desc;
        if (owner->broken("srv_srgb")) {
            if (changed.Format == DXGI_FORMAT_R8G8B8A8_UNORM) {
                changed.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            } else if (changed.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
                changed.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            }
        }
        if (owner->broken("srv_mips") && changed.ViewDimension == D3D12_SRV_DIMENSION_TEXTURE2D) {
            changed.Texture2D.MostDetailedMip = 0;
            changed.Texture2D.ResourceMinLODClamp = 0.0f;
        }
        if (owner->broken("srv_swizzle")) {
            changed.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        }
        owner->device()->CreateShaderResourceView(resource, &changed, D3d12Tools::cpu_at(heap, slot, increment));
        return;
    }
    owner->device()->CreateShaderResourceView(resource, desc, D3d12Tools::cpu_at(heap, slot, increment));
}

void Binder::srv_texture(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT mips) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Texture2D.MipLevels = mips;
    srv(slot, resource, &desc);
}

void Binder::srv_buffer(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT first, UINT count, UINT stride, bool raw) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = raw ? DXGI_FORMAT_R32_TYPELESS : format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Buffer.FirstElement = first;
    desc.Buffer.NumElements = count;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.Flags = raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
    srv(slot, resource, &desc);
}

void Binder::uav(UINT slot, ID3D12Resource* resource, const D3D12_UNORDERED_ACCESS_VIEW_DESC* desc) {
    owner->device()->CreateUnorderedAccessView(resource, nullptr, desc, D3d12Tools::cpu_at(heap, kSrvSlots + slot, increment));
}

void Binder::uav_texture(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT mip) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    desc.Texture2D.MipSlice = mip;
    uav(slot, resource, &desc);
}

void Binder::uav_buffer(UINT slot, ID3D12Resource* resource, DXGI_FORMAT format, UINT first, UINT count, UINT stride, bool raw) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = raw ? DXGI_FORMAT_R32_TYPELESS : format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    desc.Buffer.FirstElement = first;
    desc.Buffer.NumElements = count;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.Flags = raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
    uav(slot, resource, &desc);
}

void Binder::sampler(UINT slot, const D3D12_SAMPLER_DESC& desc) {
    D3D12_SAMPLER_DESC changed = desc;
    if (owner->under_test) {
        if (owner->broken("sampler_filter")) {
            const bool point = (changed.Filter & 0x15) == 0;
            changed.Filter = point ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            changed.MaxAnisotropy = 1;
        }
        if (owner->broken("sampler_address")) {
            for (D3D12_TEXTURE_ADDRESS_MODE* mode : {&changed.AddressU, &changed.AddressV, &changed.AddressW}) {
                *mode = *mode == D3D12_TEXTURE_ADDRESS_MODE_WRAP ? D3D12_TEXTURE_ADDRESS_MODE_CLAMP : D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            }
        }
        if (owner->broken("sampler_lod")) {
            changed.MipLODBias = 0.0f;
            changed.MinLOD = 0.0f;
            changed.MaxLOD = FLT_MAX;
        }
        if (owner->broken("sampler_border")) {
            changed.BorderColor[0] = changed.BorderColor[1] = changed.BorderColor[2] = changed.BorderColor[3] = 0.5f;
        }
    }
    owner->device()->CreateSampler(&changed, D3d12Tools::cpu_at(sampler_heap, slot, sampler_increment));
}

void Binder::bind_graphics(ID3D12GraphicsCommandList* list) const {
    ID3D12DescriptorHeap* heaps[2] = {heap, sampler_heap};
    list->SetGraphicsRootSignature(signature);
    list->SetDescriptorHeaps(2, heaps);
    list->SetGraphicsRootDescriptorTable(2, heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRootDescriptorTable(3, sampler_heap->GetGPUDescriptorHandleForHeapStart());
}

void Binder::bind_compute(ID3D12GraphicsCommandList* list) const {
    ID3D12DescriptorHeap* heaps[2] = {heap, sampler_heap};
    list->SetComputeRootSignature(signature);
    list->SetDescriptorHeaps(2, heaps);
    list->SetComputeRootDescriptorTable(2, heap->GetGPUDescriptorHandleForHeapStart());
    list->SetComputeRootDescriptorTable(3, sampler_heap->GetGPUDescriptorHandleForHeapStart());
}

D3D12_GPU_DESCRIPTOR_HANDLE Binder::srv_gpu(UINT slot) const { return D3d12Tools::gpu_at(heap, slot, increment); }

Pso::Pso(Kit& kit_ref, ID3D12RootSignature* signature) : kit(kit_ref) {
    desc = D3d12Tools::graphics_desc(signature, {}, {}, DXGI_FORMAT_R8G8B8A8_UNORM);
    desc.VS = {};
    desc.PS = {};
    elements = Shapes::layout();
}

Pso& Pso::vs(const std::string& source) {
    if (failure.empty() && !kit.compile(source, 'v', vs_code, failure)) {
    }
    return *this;
}

Pso& Pso::ps(const std::string& source) {
    if (failure.empty() && !kit.compile(source, 'p', ps_code, failure)) {
    }
    return *this;
}

Pso& Pso::stage(const std::string& source, const char* entry, const char* stage_name) {
    Kit::Bytes* target = nullptr;
    const std::string name = stage_name;
    if (name == "hs") {
        target = &hs_code;
    } else if (name == "ds") {
        target = &ds_code;
    } else if (name == "gs") {
        target = &gs_code;
    } else if (name == "vs") {
        target = &vs_code;
    } else {
        target = &ps_code;
    }
    if (failure.empty() && !kit.compile_as(source, entry, stage_name, *target, failure)) {
    }
    return *this;
}

Pso& Pso::layout(const std::vector<D3D12_INPUT_ELEMENT_DESC>& list) {
    elements = list;
    return *this;
}

Pso& Pso::targets(std::initializer_list<DXGI_FORMAT> formats) {
    desc.NumRenderTargets = static_cast<UINT>(formats.size());
    UINT i = 0;
    for (DXGI_FORMAT format : formats) {
        desc.RTVFormats[i] = format;
        desc.BlendState.RenderTarget[i] = desc.BlendState.RenderTarget[0];
        ++i;
    }
    for (; i < 8; ++i) {
        desc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN;
    }
    return *this;
}

Pso& Pso::depth(DXGI_FORMAT format) {
    desc.DSVFormat = format;
    desc.DepthStencilState.DepthEnable = format != DXGI_FORMAT_UNKNOWN;
    desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    return *this;
}

Pso& Pso::samples(UINT count) {
    desc.SampleDesc.Count = count;
    return *this;
}

Pso& Pso::topology(D3D12_PRIMITIVE_TOPOLOGY_TYPE type) {
    desc.PrimitiveTopologyType = type;
    return *this;
}

ID3D12PipelineState* Pso::build(std::string& error) {
    if (!failure.empty()) {
        error = failure;
        return nullptr;
    }
    desc.VS = D3d12Tools::bytecode(vs_code);
    desc.PS = ps_code.empty() ? D3D12_SHADER_BYTECODE{} : D3d12Tools::bytecode(ps_code);
    if (!hs_code.empty()) {
        desc.HS = D3d12Tools::bytecode(hs_code);
    }
    if (!ds_code.empty()) {
        desc.DS = D3d12Tools::bytecode(ds_code);
    }
    if (!gs_code.empty()) {
        desc.GS = D3d12Tools::bytecode(gs_code);
    }
    desc.InputLayout = {elements.empty() ? nullptr : elements.data(), static_cast<UINT>(elements.size())};
    break_state();
    return D3d12Tools::graphics_pso(kit.rig, desc, error);
}

namespace {
D3D12_BLEND opposite_factor(D3D12_BLEND b) {
    switch (b) {
        case D3D12_BLEND_ONE: return D3D12_BLEND_ZERO;
        case D3D12_BLEND_ZERO: return D3D12_BLEND_ONE;
        case D3D12_BLEND_SRC_ALPHA: return D3D12_BLEND_INV_SRC_ALPHA;
        case D3D12_BLEND_INV_SRC_ALPHA: return D3D12_BLEND_SRC_ALPHA;
        case D3D12_BLEND_SRC_COLOR: return D3D12_BLEND_INV_SRC_COLOR;
        case D3D12_BLEND_INV_SRC_COLOR: return D3D12_BLEND_SRC_COLOR;
        case D3D12_BLEND_DEST_ALPHA: return D3D12_BLEND_INV_DEST_ALPHA;
        case D3D12_BLEND_INV_DEST_ALPHA: return D3D12_BLEND_DEST_ALPHA;
        case D3D12_BLEND_DEST_COLOR: return D3D12_BLEND_INV_DEST_COLOR;
        case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_DEST_COLOR;
        default: return D3D12_BLEND_ONE;
    }
}

D3D12_BLEND_OP opposite_op(D3D12_BLEND_OP op) {
    switch (op) {
        case D3D12_BLEND_OP_ADD: return D3D12_BLEND_OP_SUBTRACT;
        case D3D12_BLEND_OP_SUBTRACT: return D3D12_BLEND_OP_ADD;
        case D3D12_BLEND_OP_REV_SUBTRACT: return D3D12_BLEND_OP_ADD;
        case D3D12_BLEND_OP_MIN: return D3D12_BLEND_OP_MAX;
        default: return D3D12_BLEND_OP_MIN;
    }
}

D3D12_COMPARISON_FUNC opposite_func(D3D12_COMPARISON_FUNC f) {
    switch (f) {
        case D3D12_COMPARISON_FUNC_LESS: return D3D12_COMPARISON_FUNC_GREATER;
        case D3D12_COMPARISON_FUNC_LESS_EQUAL: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
        case D3D12_COMPARISON_FUNC_GREATER: return D3D12_COMPARISON_FUNC_LESS;
        case D3D12_COMPARISON_FUNC_GREATER_EQUAL: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
        case D3D12_COMPARISON_FUNC_EQUAL: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
        case D3D12_COMPARISON_FUNC_NOT_EQUAL: return D3D12_COMPARISON_FUNC_EQUAL;
        case D3D12_COMPARISON_FUNC_NEVER: return D3D12_COMPARISON_FUNC_ALWAYS;
        default: return D3D12_COMPARISON_FUNC_NEVER;
    }
}
}  // namespace

void Pso::break_state() {
    if (!kit.under_test) {
        return;
    }
    for (UINT i = 0; i < 8; ++i) {
        D3D12_RENDER_TARGET_BLEND_DESC& rt = desc.BlendState.RenderTarget[i];
        if (rt.BlendEnable && kit.broken("blend_factor")) {
            std::swap(rt.SrcBlend, rt.DestBlend);
            rt.SrcBlendAlpha = opposite_factor(rt.SrcBlendAlpha);
        }
        if (rt.BlendEnable && kit.broken("blend_op")) {
            rt.BlendOp = opposite_op(rt.BlendOp);
            rt.BlendOpAlpha = opposite_op(rt.BlendOpAlpha);
        }
        if (kit.broken("write_mask")) {
            rt.RenderTargetWriteMask = rt.RenderTargetWriteMask == 0xF ? 0x7 : 0xF;
        }
        if (rt.LogicOpEnable && kit.broken("logic_op")) {
            rt.LogicOp = D3D12_LOGIC_OP_COPY;
        }
    }
    if (kit.broken("alpha_to_coverage")) {
        desc.BlendState.AlphaToCoverageEnable = FALSE;
    }
    D3D12_DEPTH_STENCIL_DESC& ds = desc.DepthStencilState;
    if (ds.DepthEnable && kit.broken("depth_func")) {
        ds.DepthFunc = opposite_func(ds.DepthFunc);
    }
    if (ds.DepthEnable && kit.broken("depth_write")) {
        ds.DepthWriteMask = ds.DepthWriteMask == D3D12_DEPTH_WRITE_MASK_ALL ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
    }
    if (ds.StencilEnable && kit.broken("stencil_op")) {
        for (D3D12_DEPTH_STENCILOP_DESC* face : {&ds.FrontFace, &ds.BackFace}) {
            face->StencilFailOp = face->StencilDepthFailOp = face->StencilPassOp = D3D12_STENCIL_OP_KEEP;
        }
    }
    if (ds.StencilEnable && kit.broken("stencil_func")) {
        for (D3D12_DEPTH_STENCILOP_DESC* face : {&ds.FrontFace, &ds.BackFace}) {
            face->StencilFunc = opposite_func(face->StencilFunc);
        }
    }
    if (ds.StencilEnable && kit.broken("stencil_masks")) {
        ds.StencilReadMask = 0xff;
        ds.StencilWriteMask = 0xff;
    }
    D3D12_RASTERIZER_DESC& rs = desc.RasterizerState;
    if (kit.broken("cull_mode")) {
        rs.CullMode = rs.CullMode == D3D12_CULL_MODE_NONE ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
    }
    if (kit.broken("winding")) {
        rs.FrontCounterClockwise = !rs.FrontCounterClockwise;
    }
    if (kit.broken("fill_mode")) {
        rs.FillMode = rs.FillMode == D3D12_FILL_MODE_SOLID ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    }
    if (kit.broken("depth_bias")) {
        rs.DepthBias = 0;
        rs.SlopeScaledDepthBias = 0.0f;
        rs.DepthBiasClamp = 0.0f;
    }
    if (kit.broken("depth_clip")) {
        rs.DepthClipEnable = !rs.DepthClipEnable;
    }
}

bool Canvas::open(Kit& kit, UINT w, UINT h, DXGI_FORMAT format, std::string& error, DXGI_FORMAT depth_format, UINT samples) {
    owner = &kit;
    width = w;
    height = h;
    if (!color.color(kit, w, h, format, error, samples) || !binder.create(kit, error)) {
        return false;
    }
    if (depth_format != DXGI_FORMAT_UNKNOWN) {
        has_depth = true;
        return depth.depth(kit, w, h, depth_format, error, samples);
    }
    return true;
}

void Canvas::set_viewport(ID3D12GraphicsCommandList* list) const {
    const D3D12_VIEWPORT vp = viewport_of(width, height);
    const D3D12_RECT scissor = rect_of(width, height);
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &scissor);
}

void Canvas::begin(ID3D12GraphicsCommandList* list, const float clear[4], bool clear_depth, float depth_value) {
    if (has_depth) {
        depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    color.bind_target(list, has_depth ? &depth : nullptr);
    list->ClearRenderTargetView(color.view, clear, 0, nullptr);
    if (has_depth && clear_depth) {
        list->ClearDepthStencilView(depth.view, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth_value, 0, 0, nullptr);
    }
    set_viewport(list);
    binder.bind_graphics(list);
}

void Canvas::begin_keep(ID3D12GraphicsCommandList* list) {
    if (has_depth) {
        depth.to(list, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    color.bind_target(list, has_depth ? &depth : nullptr);
    set_viewport(list);
    binder.bind_graphics(list);
}

bool Canvas::read(Kit& kit, rr::Image& out, std::string& error) {
    return kit.read_image(color.resource, color.state, 0, color.format, out, error);
}

bool Canvas::read_depth(Kit& kit, rr::Image& out, std::string& error) {
    const DepthFormats formats = DepthFormats::of(depth.format);
    return kit.read_image(depth.resource, depth.state, 0, formats.resource == DXGI_FORMAT_R32_TYPELESS ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_TYPELESS,
                          out, error);
}

bool Stage::open(Kit& k, std::string& error, DXGI_FORMAT fmt, DXGI_FORMAT depth, UINT size, UINT samples) {
    kit = &k;
    if (k.broken("sample_count") && samples > 1) {
        samples = samples > 2 ? samples / 2 : 1;
    }
    if (k.broken("depth_format")) {
        if (depth == DXGI_FORMAT_D16_UNORM || depth == DXGI_FORMAT_D24_UNORM_S8_UINT) {
            depth = depth == DXGI_FORMAT_D16_UNORM ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        } else if (depth == DXGI_FORMAT_D32_FLOAT) {
            depth = DXGI_FORMAT_D16_UNORM;
        }
    }
    format = fmt;
    depth_format = depth;
    sample_count = samples;
    if (!canvas.open(k, size, size, fmt, error, depth, samples) || !sphere.create(k, Shapes::sphere(24, 32, 0.8f), true, error) ||
        !torus.create(k, Shapes::torus(32, 16, 0.9f, 0.3f), true, error) || !cube.create(k, Shapes::cube(), false, error) ||
        !plane.create(k, Shapes::quad(-1.2f, -1.2f, 1.2f, 1.2f), false, error)) {
        return false;
    }
    Pso backdrop_builder(k, canvas.binder.signature);
    backdrop_builder.vs(Hlsl12::kFullVs).ps(Hlsl12::kBackdropPs).layout({}).targets({fmt}).samples(samples);
    if (depth != DXGI_FORMAT_UNKNOWN) {
        backdrop_builder.desc.DSVFormat = DepthFormats::of(depth).dsv;
    }
    backdrop_pso = backdrop_builder.build(error);
    return backdrop_pso != nullptr;
}

Pso Stage::pso(const char* pixel) {
    Pso builder(*kit, canvas.binder.signature);
    builder.vs(Hlsl12::with_root(Hlsl12::kLitVs)).ps(Hlsl12::with_root(pixel != nullptr ? pixel : Hlsl12::kLitPs)).targets({format}).samples(sample_count);
    if (depth_format != DXGI_FORMAT_UNKNOWN) {
        builder.depth(DepthFormats::of(depth_format).dsv);
    }
    return builder;
}

void Stage::backdrop(ID3D12GraphicsCommandList* list) {
    list->SetPipelineState(backdrop_pso);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

rr::Mat4 Stage::view_projection() const {
    return rr::Mat4::perspective_d3d(1.0f, 1.0f, 0.5f, 40.0f) * rr::Mat4::translate(0, 0, -3.0f);
}

void Stage::mesh(ID3D12GraphicsCommandList* list, const Geometry& geometry, const rr::Mat4& model, float r, float g, float b, float a) {
    const float color[4] = {r, g, b, a};
    set_cbv_root(list, view_projection() * model, color);
    geometry.bind(list);
    geometry.draw(list);
}

bool Stage::finish(rr::CaptureSet& out, std::string& error, const char* label) {
    rr::Image image;
    if (!canvas.read(*kit, image, error)) {
        return false;
    }
    out.add(label, std::move(image));
    return true;
}

namespace {

class FnScene : public D3d12SingleScene {
public:
    FnScene(Cases::DrawFn draw_fn, Cases::NeedFn need_fn) : fn(std::move(draw_fn)), need(std::move(need_fn)) {}
    [[nodiscard]] std::string unsupported(Kit& kit) override { return need ? need(kit) : std::string(); }
    bool draw(Kit& kit, rr::CaptureSet& out, std::string& error) override { return fn(kit, out, error); }

private:
    Cases::DrawFn fn;
    Cases::NeedFn need;
};

}  // namespace

void Cases::single(rr::CaseSpec spec, DrawFn draw, NeedFn need) {
    spec.tag("single");
    rr::CaseTable<Kit>::instance().add(std::move(spec), [draw = std::move(draw), need = std::move(need)] {
        return std::unique_ptr<rr::Scene<Kit>>(new FnScene(draw, need));
    });
}

void Cases::frames(rr::CaseSpec spec, std::function<void(Steps&)> build, NeedFn need) {
    rr::CaseTable<Kit>::instance().add(std::move(spec), [build = std::move(build), need = std::move(need)] {
        auto scene = std::make_unique<Steps>();
        scene->need = need;
        build(*scene);
        return std::unique_ptr<rr::Scene<Kit>>(std::move(scene));
    });
}

}  // namespace lsw::suites::render::d3d12
