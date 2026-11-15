#include "sanity/interop/d3dcl_checks.hpp"

#include "sanity/cl/kit.hpp"
#include "sanity/interop/kit.hpp"
#include "support/api_policy.hpp"
#include "support/cl_interop.hpp"
#include "support/d3d10_device.hpp"
#include "support/d3d10_tools.hpp"
#include "support/d3d11_device.hpp"
#include "support/d3d11_tools.hpp"
#include "support/d3d9_rig.hpp"
#include "support/d3d9_tools.hpp"

#include <algorithm>
#include <cstring>
#include <functional>

namespace lsw::suites::sanity::interop {

namespace {

namespace clkit = lsw::suites::sanity::cl;

using AcquireFn = std::function<cl_int(cl_command_queue, cl_uint, const cl_mem*, cl_uint, const cl_event*, cl_event*)>;

enum class Open { Ready, Unsupported, Failed };

struct Item {
    std::string label;
    bool image = false;
    std::size_t bytes = 0;
    std::size_t region[3] = {0, 0, 0};
    std::function<bool(const std::vector<std::uint8_t>&, std::string&)> write;
    std::function<bool(std::vector<std::uint8_t>&, std::string&)> read;
    std::function<cl_mem(cl_context, cl_int&)> make;
};

struct Side {
    AvAdapter adapter;
    ClRig cl;
    ClInterop interop;
    ClDeviceList list;
    std::string detail;

    Open open(const char* extension) {
        std::string error;
        ClApi api;
        if (!ClRig::list_devices(api, list, error)) {
            detail = "OpenCL: " + error;
            return Open::Failed;
        }
        const ClDeviceRef* pick = &list.refs.front();
        for (const ClDeviceRef& ref : list.refs) {
            if (ref.adapter.luid_key() == adapter.luid_key()) {
                pick = &ref;
            }
        }
        ClRigOptions options;
        options.context = false;
        if (!cl.open_ref(*pick, options, error)) {
            detail = pick->label() + ": " + error;
            return Open::Failed;
        }
        interop.load(cl);
        if (!cl.has_extension(extension)) {
            detail = std::string(extension) + " is not advertised by " + pick->label();
            return Open::Unsupported;
        }
        return Open::Ready;
    }

    bool adapter_of(cl_device_id device) const {
        for (const ClDeviceRef& ref : list.refs) {
            if (ref.device == device) {
                return ref.adapter.luid_key() == adapter.luid_key();
            }
        }
        return false;
    }

    bool context(const cl_context_properties* props, cl_device_id device, std::string& error) {
        cl_int code = CL_SUCCESS;
        cl.device = device;
        cl.context = cl.api.CreateContext(props, 1, &device, nullptr, nullptr, &code);
        if (cl.context == nullptr || code != CL_SUCCESS) {
            error = "clCreateContext with the D3D device returned " + ClTools::error_name(code);
            cl.context = nullptr;
            return false;
        }
        cl.queue = cl.create_queue(0, code);
        if (cl.queue == nullptr) {
            error = "the command queue of the sharing context cannot be created: " + ClTools::error_name(code);
            return false;
        }
        return true;
    }
};

Outcome decide(const Side& side, Open state) {
    return state == Open::Unsupported ? Context::skip(side.detail) : Outcome::fail(side.detail);
}

void exercise(Context& context, Problems& problems, Side& side, const AcquireFn& acquire, const AcquireFn& release, Item& item, int seed) {
    ClRig& cl = side.cl;
    const std::vector<std::uint8_t> first = clkit::Kit::bytes(Kit::seed(20, seed), item.bytes);
    std::string error;
    if (!item.write(first, error)) {
        problems.add(item.label + ": " + error);
        return;
    }
    cl_int code = CL_SUCCESS;
    ClOwned<cl_mem> mem;
    mem.adopt(cl, item.make(cl.context, code));
    if (!mem) {
        problems.add(item.label + ": creating the OpenCL object returned " + ClTools::error_name(code));
        return;
    }
    cl_mem handle = mem.get();
    code = acquire(cl.queue, 1, &handle, 0, nullptr, nullptr);
    if (code != CL_SUCCESS) {
        problems.add(item.label + ": the acquire returned " + ClTools::error_name(code));
        return;
    }
    const std::size_t origin[3] = {0, 0, 0};
    std::vector<std::uint8_t> seen(item.bytes);
    code = item.image ? cl.api.EnqueueReadImage(cl.queue, mem.get(), CL_TRUE, origin, item.region, 0, 0, seen.data(), 0, nullptr, nullptr)
                      : cl.api.EnqueueReadBuffer(cl.queue, mem.get(), CL_TRUE, 0, item.bytes, seen.data(), 0, nullptr, nullptr);
    if (code != CL_SUCCESS || seen != first) {
        problems.add(item.label + ": the CL read of what Direct3D wrote " + (code != CL_SUCCESS ? "returned " + ClTools::error_name(code) : std::string("differs")));
        if (code == CL_SUCCESS) {
            clkit::Kit::same_bytes(context, problems, "d3d_to_cl_" + Kit::sanitize(item.label), first.data(), seen.data(), item.bytes);
        }
    }
    const std::vector<std::uint8_t> second = clkit::Kit::bytes(Kit::seed(21, seed), item.bytes);
    const cl_int written = item.image ? cl.api.EnqueueWriteImage(cl.queue, mem.get(), CL_TRUE, origin, item.region, 0, 0, second.data(), 0, nullptr, nullptr)
                                      : cl.api.EnqueueWriteBuffer(cl.queue, mem.get(), CL_TRUE, 0, item.bytes, second.data(), 0, nullptr, nullptr);
    const cl_int released = release(cl.queue, 1, &handle, 0, nullptr, nullptr);
    cl.api.Finish(cl.queue);
    if (written != CL_SUCCESS || released != CL_SUCCESS) {
        problems.add(item.label + ": the CL write returned " + ClTools::error_name(written) + " and the release " + ClTools::error_name(released));
        return;
    }
    std::vector<std::uint8_t> back;
    if (!item.read(back, error)) {
        problems.add(item.label + ": " + error);
    } else if (back != second) {
        problems.add(item.label + ": Direct3D does not read what CL wrote after the release");
        clkit::Kit::same_bytes(context, problems, "cl_to_d3d_" + Kit::sanitize(item.label), second.data(), back.data(), std::min(second.size(), back.size()));
    }
}

std::string entry_points(const std::initializer_list<std::pair<const char*, bool>>& points) {
    std::string missing;
    for (const auto& [name, present] : points) {
        if (!present) {
            missing += (missing.empty() ? "" : ", ") + std::string(name);
        }
    }
    return missing;
}

}  // namespace

Outcome D3dClChecks::d3d11_sharing(Context& context) {
    if (const std::string why = Kit::missing({"d3d11", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    D3d11Device d3d;
    Side side;
    std::string error;
    if (!TargetAdapter::pick(side.adapter, error) || !d3d.create(side.adapter.luid, error)) {
        return Outcome::fail(error);
    }
    const Open state = side.open("cl_khr_d3d11_sharing");
    if (state != Open::Ready) {
        return decide(side, state);
    }
    ClInterop& api = side.interop;
    const std::string missing = entry_points({{"clGetDeviceIDsFromD3D11KHR", api.GetDeviceIDsFromD3D11 != nullptr}, {"clCreateFromD3D11BufferKHR", api.CreateFromD3D11Buffer != nullptr},
                                              {"clCreateFromD3D11Texture2DKHR", api.CreateFromD3D11Texture2D != nullptr}, {"clCreateFromD3D11Texture3DKHR", api.CreateFromD3D11Texture3D != nullptr},
                                              {"clEnqueueAcquireD3D11ObjectsKHR", api.AcquireD3D11Objects != nullptr}, {"clEnqueueReleaseD3D11ObjectsKHR", api.ReleaseD3D11Objects != nullptr}});
    if (!missing.empty()) {
        return Outcome::fail("cl_khr_d3d11_sharing is advertised but these entry points do not resolve: " + missing);
    }
    Problems problems;
    cl_device_id device = nullptr;
    cl_uint count = 0;
    for (const cl_d3d11_device_set_khr set : {static_cast<cl_d3d11_device_set_khr>(CL_PREFERRED_DEVICES_FOR_D3D11_KHR), static_cast<cl_d3d11_device_set_khr>(CL_ALL_DEVICES_FOR_D3D11_KHR)}) {
        device = nullptr;
        count = 0;
        const cl_int code = api.GetDeviceIDsFromD3D11(side.cl.platform, CL_D3D11_DEVICE_KHR, d3d.device, set, 1, &device, &count);
        if (code != CL_SUCCESS || count == 0 || device == nullptr) {
            problems.add("clGetDeviceIDsFromD3D11KHR (" + std::string(set == CL_PREFERRED_DEVICES_FOR_D3D11_KHR ? "preferred" : "all") + ") returned " + ClTools::error_name(code) + " with " + std::to_string(count) + " device(s)");
            return problems.outcome();
        }
        if (!side.adapter_of(device)) {
            problems.add("the device from clGetDeviceIDsFromD3D11KHR does not belong to the adapter of the Direct3D 11 device");
        }
    }
    const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_D3D11_DEVICE_KHR,
                                           reinterpret_cast<cl_context_properties>(d3d.device), 0};
    if (!side.context(props, device, error)) {
        problems.add(error);
        return problems.outcome();
    }
    auto settle = [&](std::string& e) {
        d3d.context->Flush();
        return D3d11Tools::wait_event(d3d, d3d.context, e);
    };
    const AcquireFn acquire = [&](cl_command_queue q, cl_uint n, const cl_mem* m, cl_uint w, const cl_event* l, cl_event* e) { return api.AcquireD3D11Objects(q, n, m, w, l, e); };
    const AcquireFn release = [&](cl_command_queue q, cl_uint n, const cl_mem* m, cl_uint w, const cl_event* l, cl_event* e) { return api.ReleaseD3D11Objects(q, n, m, w, l, e); };
    {
        Com<ID3D11Buffer> buffer = D3d11Tools::buffer(d3d, 4096, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, nullptr);
        Item item;
        item.label = "buffer";
        item.bytes = 4096;
        item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
            d3d.context->UpdateSubresource(buffer.get(), 0, nullptr, data.data(), 0, 0);
            return settle(e);
        };
        item.read = [&](std::vector<std::uint8_t>& out, std::string& e) { return D3d11Tools::read_buffer(d3d, buffer.get(), out, e); };
        item.make = [&](cl_context c, cl_int& code) { return api.CreateFromD3D11Buffer(c, CL_MEM_READ_WRITE, buffer.get(), &code); };
        if (!buffer) {
            problems.add("the Direct3D 11 buffer cannot be created");
        } else {
            exercise(context, problems, side, acquire, release, item, 1);
        }
    }
    int seed = 2;
    for (DXGI_FORMAT format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM}) {
        constexpr UINT kW = 32;
        constexpr UINT kH = 16;
        Com<ID3D11Texture2D> texture = D3d11Tools::texture(d3d, D3d11Tools::desc2d(kW, kH, format, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET));
        Item item;
        item.label = format == DXGI_FORMAT_R8G8B8A8_UNORM ? "2d texture R8G8B8A8" : "2d texture B8G8R8A8";
        item.image = true;
        item.bytes = kW * kH * 4u;
        item.region[0] = kW;
        item.region[1] = kH;
        item.region[2] = 1;
        item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
            d3d.context->UpdateSubresource(texture.get(), 0, nullptr, data.data(), kW * 4u, 0);
            return settle(e);
        };
        item.read = [&](std::vector<std::uint8_t>& out, std::string& e) {
            TexelImage image;
            if (!D3d11Tools::read_texture(d3d, texture.get(), 0, image, e)) {
                return false;
            }
            out = image.data;
            return true;
        };
        item.make = [&](cl_context c, cl_int& code) { return api.CreateFromD3D11Texture2D(c, CL_MEM_READ_WRITE, texture.get(), 0, &code); };
        if (!texture) {
            problems.add(item.label + " cannot be created");
        } else {
            exercise(context, problems, side, acquire, release, item, seed++);
        }
    }
    {
        constexpr UINT kEdge = 8;
        constexpr UINT kDepth = 4;
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = kEdge;
        desc.Height = kEdge;
        desc.Depth = kDepth;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        Com<ID3D11Texture3D> volume;
        d3d.device->CreateTexture3D(&desc, nullptr, volume.put());
        Item item;
        item.label = "3d texture";
        item.image = true;
        item.bytes = kEdge * kEdge * kDepth * 4u;
        item.region[0] = kEdge;
        item.region[1] = kEdge;
        item.region[2] = kDepth;
        item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
            d3d.context->UpdateSubresource(volume.get(), 0, nullptr, data.data(), kEdge * 4u, kEdge * kEdge * 4u);
            return settle(e);
        };
        item.read = [&](std::vector<std::uint8_t>& out, std::string& e) {
            TexelImage image;
            if (!D3d11Tools::read_volume(d3d, volume.get(), 0, image, e)) {
                return false;
            }
            out = image.data;
            return true;
        };
        item.make = [&](cl_context c, cl_int& code) { return api.CreateFromD3D11Texture3D(c, CL_MEM_READ_WRITE, volume.get(), 0, &code); };
        if (!volume) {
            problems.add("the 3d texture cannot be created");
        } else {
            exercise(context, problems, side, acquire, release, item, seed++);
        }
    }
    side.cl.drain();
    std::string settle_error;
    static_cast<void>(settle(settle_error));
    return problems.outcome();
}

Outcome D3dClChecks::d3d10_sharing(Context& context) {
    if (const std::string why = Kit::missing({"opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    D3d10Device d3d;
    Side side;
    std::string error;
    if (!TargetAdapter::pick(side.adapter, error) || !d3d.create(side.adapter.luid, 0, error)) {
        return Outcome::fail(error);
    }
    const Open state = side.open("cl_khr_d3d10_sharing");
    if (state != Open::Ready) {
        return decide(side, state);
    }
    ClInterop& api = side.interop;
    const std::string missing = entry_points({{"clGetDeviceIDsFromD3D10KHR", api.GetDeviceIDsFromD3D10 != nullptr}, {"clCreateFromD3D10BufferKHR", api.CreateFromD3D10Buffer != nullptr},
                                              {"clCreateFromD3D10Texture2DKHR", api.CreateFromD3D10Texture2D != nullptr}, {"clCreateFromD3D10Texture3DKHR", api.CreateFromD3D10Texture3D != nullptr},
                                              {"clEnqueueAcquireD3D10ObjectsKHR", api.AcquireD3D10Objects != nullptr}, {"clEnqueueReleaseD3D10ObjectsKHR", api.ReleaseD3D10Objects != nullptr}});
    if (!missing.empty()) {
        return Outcome::fail("cl_khr_d3d10_sharing is advertised but these entry points do not resolve: " + missing);
    }
    Problems problems;
    cl_device_id device = nullptr;
    cl_uint count = 0;
    cl_int code = api.GetDeviceIDsFromD3D10(side.cl.platform, CL_D3D10_DEVICE_KHR, d3d.device, CL_PREFERRED_DEVICES_FOR_D3D10_KHR, 1, &device, &count);
    if (code != CL_SUCCESS || count == 0 || device == nullptr) {
        return Outcome::fail("clGetDeviceIDsFromD3D10KHR returned " + ClTools::error_name(code) + " with " + std::to_string(count) + " device(s)");
    }
    if (!side.adapter_of(device)) {
        problems.add("the device from clGetDeviceIDsFromD3D10KHR does not belong to the adapter of the Direct3D 10 device");
    }
    const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_D3D10_DEVICE_KHR,
                                           reinterpret_cast<cl_context_properties>(d3d.device), 0};
    if (!side.context(props, device, error)) {
        problems.add(error);
        return problems.outcome();
    }
    auto settle = [&](std::string& e) { return d3d.wait_idle(e); };
    const AcquireFn acquire = [&](cl_command_queue q, cl_uint n, const cl_mem* m, cl_uint w, const cl_event* l, cl_event* e) { return api.AcquireD3D10Objects(q, n, m, w, l, e); };
    const AcquireFn release = [&](cl_command_queue q, cl_uint n, const cl_mem* m, cl_uint w, const cl_event* l, cl_event* e) { return api.ReleaseD3D10Objects(q, n, m, w, l, e); };
    auto read_staged = [&](ID3D10Resource* source, ID3D10Resource* staging, bool volume, UINT width, UINT height, UINT depth, std::vector<std::uint8_t>& out, std::string& e) {
        d3d.device->CopyResource(staging, source);
        D3D10_MAPPED_TEXTURE2D mapped2{};
        D3D10_MAPPED_TEXTURE3D mapped3{};
        HRESULT result = S_OK;
        if (volume) {
            result = static_cast<ID3D10Texture3D*>(staging)->Map(0, D3D10_MAP_READ, 0, &mapped3);
        } else {
            result = static_cast<ID3D10Texture2D*>(staging)->Map(0, D3D10_MAP_READ, 0, &mapped2);
        }
        if (FAILED(result)) {
            e = "Map of the staging texture failed: " + D3d10Tools::hr(result);
            return false;
        }
        out.resize(static_cast<std::size_t>(width) * height * depth * 4u);
        for (UINT z = 0; z < depth; ++z) {
            for (UINT y = 0; y < height; ++y) {
                const std::uint8_t* row = volume ? static_cast<const std::uint8_t*>(mapped3.pData) + static_cast<std::size_t>(z) * mapped3.DepthPitch + static_cast<std::size_t>(y) * mapped3.RowPitch
                                                 : static_cast<const std::uint8_t*>(mapped2.pData) + static_cast<std::size_t>(y) * mapped2.RowPitch;
                std::memcpy(&out[(static_cast<std::size_t>(z) * height + y) * width * 4u], row, static_cast<std::size_t>(width) * 4u);
            }
        }
        if (volume) {
            static_cast<ID3D10Texture3D*>(staging)->Unmap(0);
        } else {
            static_cast<ID3D10Texture2D*>(staging)->Unmap(0);
        }
        return true;
    };
    {
        Com<ID3D10Buffer> buffer = D3d10Tools::buffer(d3d, 4096, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, nullptr);
        Item item;
        item.label = "buffer";
        item.bytes = 4096;
        item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
            d3d.device->UpdateSubresource(buffer.get(), 0, nullptr, data.data(), 0, 0);
            return settle(e);
        };
        item.read = [&](std::vector<std::uint8_t>& out, std::string& e) { return D3d10Tools::read_buffer(d3d, buffer.get(), out, e); };
        item.make = [&](cl_context c, cl_int& made) { return api.CreateFromD3D10Buffer(c, CL_MEM_READ_WRITE, buffer.get(), &made); };
        if (!buffer) {
            problems.add("the Direct3D 10 buffer cannot be created");
        } else {
            exercise(context, problems, side, acquire, release, item, 1);
        }
    }
    {
        constexpr UINT kW = 32;
        constexpr UINT kH = 16;
        Com<ID3D10Texture2D> texture = D3d10Tools::texture(d3d, D3d10Tools::desc2d(kW, kH, DXGI_FORMAT_R8G8B8A8_UNORM, D3D10_BIND_SHADER_RESOURCE | D3D10_BIND_RENDER_TARGET));
        D3D10_TEXTURE2D_DESC staging_desc = D3d10Tools::desc2d(kW, kH, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D10_USAGE_STAGING, D3D10_CPU_ACCESS_READ);
        Com<ID3D10Texture2D> staging = D3d10Tools::texture(d3d, staging_desc);
        Item item;
        item.label = "2d texture";
        item.image = true;
        item.bytes = kW * kH * 4u;
        item.region[0] = kW;
        item.region[1] = kH;
        item.region[2] = 1;
        item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
            d3d.device->UpdateSubresource(texture.get(), 0, nullptr, data.data(), kW * 4u, 0);
            return settle(e);
        };
        item.read = [&](std::vector<std::uint8_t>& out, std::string& e) { return read_staged(texture.get(), staging.get(), false, kW, kH, 1, out, e); };
        item.make = [&](cl_context c, cl_int& made) { return api.CreateFromD3D10Texture2D(c, CL_MEM_READ_WRITE, texture.get(), 0, &made); };
        if (!texture || !staging) {
            problems.add("the Direct3D 10 2d texture cannot be created");
        } else {
            exercise(context, problems, side, acquire, release, item, 2);
        }
    }
    {
        constexpr UINT kEdge = 8;
        constexpr UINT kDepth = 4;
        D3D10_TEXTURE3D_DESC desc{};
        desc.Width = kEdge;
        desc.Height = kEdge;
        desc.Depth = kDepth;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Usage = D3D10_USAGE_DEFAULT;
        desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
        Com<ID3D10Texture3D> volume;
        d3d.device->CreateTexture3D(&desc, nullptr, volume.put());
        D3D10_TEXTURE3D_DESC staging_desc = desc;
        staging_desc.Usage = D3D10_USAGE_STAGING;
        staging_desc.BindFlags = 0;
        staging_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
        Com<ID3D10Texture3D> staging;
        d3d.device->CreateTexture3D(&staging_desc, nullptr, staging.put());
        Item item;
        item.label = "3d texture";
        item.image = true;
        item.bytes = kEdge * kEdge * kDepth * 4u;
        item.region[0] = kEdge;
        item.region[1] = kEdge;
        item.region[2] = kDepth;
        item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
            d3d.device->UpdateSubresource(volume.get(), 0, nullptr, data.data(), kEdge * 4u, kEdge * kEdge * 4u);
            return settle(e);
        };
        item.read = [&](std::vector<std::uint8_t>& out, std::string& e) { return read_staged(volume.get(), staging.get(), true, kEdge, kEdge, kDepth, out, e); };
        item.make = [&](cl_context c, cl_int& made) { return api.CreateFromD3D10Texture3D(c, CL_MEM_READ_WRITE, volume.get(), 0, &made); };
        if (!volume || !staging) {
            problems.add("the Direct3D 10 3d texture cannot be created");
        } else {
            exercise(context, problems, side, acquire, release, item, 3);
        }
    }
    side.cl.drain();
    std::string settle_error;
    static_cast<void>(settle(settle_error));
    return problems.outcome();
}

Outcome D3dClChecks::dx9_media_sharing(Context& context) {
    if (const std::string why = Kit::missing({"d3d9", "opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    D3d9Rig rig;
    Side side;
    std::string error;
    D3d9Settings settings;
    settings.depth = false;
    if (!rig.open(true, error)) {
        return Context::skip("Direct3D 9Ex is unavailable: " + error);
    }
    if (!rig.create(settings, error)) {
        return Outcome::fail(error);
    }
    side.adapter = rig.adapter;
    const Open state = side.open("cl_khr_dx9_media_sharing");
    if (state != Open::Ready) {
        return decide(side, state);
    }
    ClInterop& api = side.interop;
    const std::string missing = entry_points({{"clGetDeviceIDsFromDX9MediaAdapterKHR", api.GetDeviceIDsFromDX9 != nullptr}, {"clCreateFromDX9MediaSurfaceKHR", api.CreateFromDX9Surface != nullptr},
                                              {"clEnqueueAcquireDX9MediaSurfacesKHR", api.AcquireDX9Surfaces != nullptr}, {"clEnqueueReleaseDX9MediaSurfacesKHR", api.ReleaseDX9Surfaces != nullptr}});
    if (!missing.empty()) {
        return Outcome::fail("cl_khr_dx9_media_sharing is advertised but these entry points do not resolve: " + missing);
    }
    Problems problems;
    IDirect3DDevice9Ex* device9 = rig.device_ex.get();
    cl_dx9_media_adapter_type_khr type = CL_ADAPTER_D3D9EX_KHR;
    void* adapters[1] = {device9};
    cl_device_id device = nullptr;
    cl_uint count = 0;
    cl_int code = api.GetDeviceIDsFromDX9(side.cl.platform, 1, &type, adapters, CL_PREFERRED_DEVICES_FOR_DX9_MEDIA_ADAPTER_KHR, 1, &device, &count);
    if (code != CL_SUCCESS || count == 0 || device == nullptr) {
        return Outcome::fail("clGetDeviceIDsFromDX9MediaAdapterKHR returned " + ClTools::error_name(code) + " with " + std::to_string(count) + " device(s)");
    }
    if (!side.adapter_of(device)) {
        problems.add("the device from clGetDeviceIDsFromDX9MediaAdapterKHR does not belong to the adapter of the Direct3D 9Ex device");
    }
    const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_ADAPTER_D3D9EX_KHR,
                                           reinterpret_cast<cl_context_properties>(device9), 0};
    if (!side.context(props, device, error)) {
        problems.add(error);
        return problems.outcome();
    }
    const AcquireFn acquire = [&](cl_command_queue q, cl_uint n, const cl_mem* m, cl_uint w, const cl_event* l, cl_event* e) { return api.AcquireDX9Surfaces(q, n, m, w, l, e); };
    const AcquireFn release = [&](cl_command_queue q, cl_uint n, const cl_mem* m, cl_uint w, const cl_event* l, cl_event* e) { return api.ReleaseDX9Surfaces(q, n, m, w, l, e); };
    {
        constexpr UINT kW = 32;
        constexpr UINT kH = 16;
        HANDLE shared = nullptr;
        Com<IDirect3DSurface9> surface;
        const HRESULT made = device9->CreateRenderTarget(kW, kH, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, surface.put(), &shared);
        if (FAILED(made)) {
            problems.add("a lockable shared render target cannot be created on the Direct3D 9Ex device: " + D3d9Tools::hr(made));
        } else {
            if (shared == nullptr) {
                problems.add("CreateRenderTarget with a shared handle request succeeded without a handle");
            }
            cl_dx9_surface_info_khr info{surface.get(), shared};
            Item item;
            item.label = "A8R8G8B8 render target";
            item.image = true;
            item.bytes = kW * kH * 4u;
            item.region[0] = kW;
            item.region[1] = kH;
            item.region[2] = 1;
            item.write = [&](const std::vector<std::uint8_t>& data, std::string& e) {
                if (!D3d9Tools::wait_event(device9, e)) {
                    return false;
                }
                D3DLOCKED_RECT locked{};
                if (FAILED(surface->LockRect(&locked, nullptr, 0))) {
                    e = "LockRect of the surface failed";
                    return false;
                }
                for (UINT y = 0; y < kH; ++y) {
                    std::memcpy(static_cast<std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), &data[static_cast<std::size_t>(y) * kW * 4u], kW * 4u);
                }
                surface->UnlockRect();
                return D3d9Tools::wait_event(device9, e);
            };
            item.read = [&](std::vector<std::uint8_t>& out, std::string& e) {
                D3DLOCKED_RECT locked{};
                if (!D3d9Tools::wait_event(device9, e) || FAILED(surface->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
                    e = e.empty() ? "LockRect of the surface failed" : e;
                    return false;
                }
                out.resize(kW * kH * 4u);
                for (UINT y = 0; y < kH; ++y) {
                    std::memcpy(&out[static_cast<std::size_t>(y) * kW * 4u], static_cast<const std::uint8_t*>(locked.pBits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), kW * 4u);
                }
                surface->UnlockRect();
                return true;
            };
            item.make = [&](cl_context c, cl_int& result) { return api.CreateFromDX9Surface(c, CL_MEM_READ_WRITE, type, &info, 0, &result); };
            exercise(context, problems, side, acquire, release, item, 1);
        }
    }
    const D3DFORMAT nv12 = static_cast<D3DFORMAT>(MAKEFOURCC('N', 'V', '1', '2'));
    if (SUCCEEDED(rig.d3d->CheckDeviceFormat(rig.ordinal, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_SURFACE, nv12))) {
        constexpr UINT kW = 32;
        constexpr UINT kH = 16;
        HANDLE shared = nullptr;
        Com<IDirect3DSurface9> surface;
        const HRESULT made = device9->CreateOffscreenPlainSurface(kW, kH, nv12, D3DPOOL_DEFAULT, surface.put(), &shared);
        D3DLOCKED_RECT locked{};
        if (FAILED(made) || FAILED(surface->LockRect(&locked, nullptr, 0))) {
            context.note("an NV12 surface is reported by CheckDeviceFormat but cannot be created or locked here (" + D3d9Tools::hr(made) + "), so the planes are not exercised");
        } else {
            std::vector<std::uint8_t> luma = clkit::Kit::bytes(Kit::seed(30), kW * kH);
            std::vector<std::uint8_t> chroma = clkit::Kit::bytes(Kit::seed(31), kW * kH / 2);
            auto* base = static_cast<std::uint8_t*>(locked.pBits);
            for (UINT y = 0; y < kH; ++y) {
                std::memcpy(base + static_cast<std::size_t>(y) * static_cast<std::size_t>(locked.Pitch), &luma[static_cast<std::size_t>(y) * kW], kW);
            }
            for (UINT y = 0; y < kH / 2; ++y) {
                std::memcpy(base + static_cast<std::size_t>(kH + y) * static_cast<std::size_t>(locked.Pitch), &chroma[static_cast<std::size_t>(y) * kW], kW);
            }
            surface->UnlockRect();
            std::string wait_error;
            static_cast<void>(D3d9Tools::wait_event(device9, wait_error));
            cl_dx9_surface_info_khr info{surface.get(), shared};
            ClOwned<cl_mem> planes[2];
            for (int plane = 0; plane < 2; ++plane) {
                cl_int result = CL_SUCCESS;
                planes[plane].adopt(side.cl, api.CreateFromDX9Surface(side.cl.context, CL_MEM_READ_ONLY, type, &info, static_cast<cl_uint>(plane), &result));
                if (!planes[plane]) {
                    problems.add("clCreateFromDX9MediaSurfaceKHR for NV12 plane " + std::to_string(plane) + " returned " + ClTools::error_name(result));
                }
            }
            if (planes[0] && planes[1]) {
                cl_mem handles[2] = {planes[0].get(), planes[1].get()};
                code = api.AcquireDX9Surfaces(side.cl.queue, 2, handles, 0, nullptr, nullptr);
                if (code != CL_SUCCESS) {
                    problems.add("acquiring the NV12 planes returned " + ClTools::error_name(code));
                } else {
                    const std::size_t origin[3] = {0, 0, 0};
                    const std::size_t luma_region[3] = {kW, kH, 1};
                    const std::size_t chroma_region[3] = {kW / 2, kH / 2, 1};
                    std::vector<std::uint8_t> seen_luma(luma.size());
                    std::vector<std::uint8_t> seen_chroma(chroma.size());
                    const cl_int a = side.cl.api.EnqueueReadImage(side.cl.queue, planes[0].get(), CL_TRUE, origin, luma_region, 0, 0, seen_luma.data(), 0, nullptr, nullptr);
                    const cl_int b = side.cl.api.EnqueueReadImage(side.cl.queue, planes[1].get(), CL_TRUE, origin, chroma_region, 0, 0, seen_chroma.data(), 0, nullptr, nullptr);
                    if (a != CL_SUCCESS || b != CL_SUCCESS || seen_luma != luma || seen_chroma != chroma) {
                        problems.add("the NV12 planes read by CL differ from what Direct3D 9Ex wrote (" + ClTools::error_name(a) + ", " + ClTools::error_name(b) + ")");
                    }
                    code = api.ReleaseDX9Surfaces(side.cl.queue, 2, handles, 0, nullptr, nullptr);
                    side.cl.api.Finish(side.cl.queue);
                    if (code != CL_SUCCESS) {
                        problems.add("releasing the NV12 planes returned " + ClTools::error_name(code));
                    }
                }
            }
        }
    }
    side.cl.drain();
    std::string settle_error;
    static_cast<void>(D3d9Tools::wait_event(device9, settle_error));
    return problems.outcome();
}

Outcome D3dClChecks::acquire_order_caps(Context& context) {
    if (const std::string why = Kit::missing({"opencl"}); !why.empty()) {
        return Context::skip(why);
    }
    Problems problems;
    struct Family {
        const char* name;
        const char* extension;
        cl_int already;
        cl_int not_acquired;
        cl_int not_shared;
    };
    int judged = 0;
    if (!ApiPolicy::is_skipped("d3d11")) {
        const Family family{"d3d11", "cl_khr_d3d11_sharing", CL_D3D11_RESOURCE_ALREADY_ACQUIRED_KHR, CL_D3D11_RESOURCE_NOT_ACQUIRED_KHR, CL_INVALID_MEM_OBJECT};
        D3d11Device d3d;
        Side side;
        std::string error;
        if (!TargetAdapter::pick(side.adapter, error) || !d3d.create(side.adapter.luid, error)) {
            return Outcome::fail(error);
        }
        const Open state = side.open(family.extension);
        if (state == Open::Failed) {
            return Outcome::fail(side.detail);
        }
        context.measure(std::string(family.name) + ".advertised", state == Open::Ready);
        ClInterop& api = side.interop;
        if (state == Open::Ready) {
            cl_device_id device = nullptr;
            cl_uint count = 0;
            const cl_int found = api.GetDeviceIDsFromD3D11 != nullptr ? api.GetDeviceIDsFromD3D11(side.cl.platform, CL_D3D11_DEVICE_KHR, d3d.device, CL_PREFERRED_DEVICES_FOR_D3D11_KHR, 1, &device, &count) : CL_INVALID_VALUE;
            const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_D3D11_DEVICE_KHR,
                                                   reinterpret_cast<cl_context_properties>(d3d.device), 0};
            Com<ID3D11Buffer> buffer = D3d11Tools::buffer(d3d, 1024, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, nullptr);
            if (found != CL_SUCCESS || count == 0 || !side.context(props, device, error) || !buffer || api.CreateFromD3D11Buffer == nullptr) {
                problems.add("d3d11: the sharing context cannot be set up (" + ClTools::error_name(found) + "): " + error);
            } else {
                ++judged;
                cl_int code = CL_SUCCESS;
                ClOwned<cl_mem> shared;
                shared.adopt(side.cl, api.CreateFromD3D11Buffer(side.cl.context, CL_MEM_READ_WRITE, buffer.get(), &code));
                cl_int plain_code = CL_SUCCESS;
                ClOwned<cl_mem> plain;
                plain.adopt(side.cl, side.cl.api.CreateBuffer(side.cl.context, CL_MEM_READ_WRITE, 64, nullptr, &plain_code));
                if (!shared || !plain) {
                    problems.add("d3d11: creating the shared buffer returned " + ClTools::error_name(code));
                } else {
                    cl_mem handle = shared.get();
                    cl_mem plain_handle = plain.get();
                    const cl_command_queue queue = side.cl.queue;
                    clkit::Kit::expect(problems, "d3d11: clEnqueueAcquireD3D11ObjectsKHR of the shared buffer", api.AcquireD3D11Objects(queue, 1, &handle, 0, nullptr, nullptr), {CL_SUCCESS});
                    clkit::Kit::expect(problems, "d3d11: a second clEnqueueAcquireD3D11ObjectsKHR", api.AcquireD3D11Objects(queue, 1, &handle, 0, nullptr, nullptr), {family.already});
                    clkit::Kit::expect(problems, "d3d11: clEnqueueReleaseD3D11ObjectsKHR of the acquired buffer", api.ReleaseD3D11Objects(queue, 1, &handle, 0, nullptr, nullptr), {CL_SUCCESS});
                    clkit::Kit::expect(problems, "d3d11: a second clEnqueueReleaseD3D11ObjectsKHR", api.ReleaseD3D11Objects(queue, 1, &handle, 0, nullptr, nullptr), {family.not_acquired});
                    clkit::Kit::expect(problems, "d3d11: clEnqueueAcquireD3D11ObjectsKHR of a plain buffer", api.AcquireD3D11Objects(queue, 1, &plain_handle, 0, nullptr, nullptr), {family.not_shared});
                    side.cl.api.Finish(queue);
                }
            }
        } else if (state == Open::Unsupported) {
            cl_context_properties plain[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), 0};
            cl_int code = CL_SUCCESS;
            side.cl.context = side.cl.api.CreateContext(plain, 1, &side.cl.device, nullptr, nullptr, &code);
            if (side.cl.context == nullptr) {
                problems.add("d3d11: a plain context cannot be created: " + ClTools::error_name(code));
            } else {
                ++judged;
                Com<ID3D11Buffer> buffer = D3d11Tools::buffer(d3d, 1024, D3D11_USAGE_DEFAULT, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0, nullptr);
                bool from_platform = false;
                static_cast<void>(ClInterop::find(side.cl, "clCreateFromD3D11BufferKHR", &from_platform));
                if (api.CreateFromD3D11Buffer != nullptr && buffer) {
                    cl_mem made = api.CreateFromD3D11Buffer(side.cl.context, CL_MEM_READ_WRITE, buffer.get(), &code);
                    if (made != nullptr) {
                        side.cl.api.ReleaseMemObject(made);
                        problems.add("d3d11: clCreateFromD3D11BufferKHR succeeds in a context without D3D11 sharing although cl_khr_d3d11_sharing is not advertised");
                    }
                }
                const cl_context_properties shared_props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_D3D11_DEVICE_KHR,
                                                              reinterpret_cast<cl_context_properties>(d3d.device), 0};
                cl_context made_context = side.cl.api.CreateContext(shared_props, 1, &side.cl.device, nullptr, nullptr, &code);
                if (made_context != nullptr) {
                    side.cl.api.ReleaseContext(made_context);
                    problems.add("d3d11: a context with CL_CONTEXT_D3D11_DEVICE_KHR is created although cl_khr_d3d11_sharing is not advertised");
                }
            }
        }
        side.cl.drain();
    }
    {
        D3d10Device d3d;
        Side side;
        std::string error;
        if (!TargetAdapter::pick(side.adapter, error) || !d3d.create(side.adapter.luid, 0, error)) {
            return Outcome::fail(error);
        }
        const Open state = side.open("cl_khr_d3d10_sharing");
        if (state == Open::Failed) {
            return Outcome::fail(side.detail);
        }
        context.measure("d3d10.advertised", state == Open::Ready);
        ClInterop& api = side.interop;
        if (state == Open::Ready) {
            cl_device_id device = nullptr;
            cl_uint count = 0;
            const cl_int found = api.GetDeviceIDsFromD3D10 != nullptr ? api.GetDeviceIDsFromD3D10(side.cl.platform, CL_D3D10_DEVICE_KHR, d3d.device, CL_PREFERRED_DEVICES_FOR_D3D10_KHR, 1, &device, &count) : CL_INVALID_VALUE;
            const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_D3D10_DEVICE_KHR,
                                                   reinterpret_cast<cl_context_properties>(d3d.device), 0};
            Com<ID3D10Buffer> buffer = D3d10Tools::buffer(d3d, 1024, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, nullptr);
            if (found != CL_SUCCESS || count == 0 || !side.context(props, device, error) || !buffer || api.CreateFromD3D10Buffer == nullptr) {
                problems.add("d3d10: the sharing context cannot be set up (" + ClTools::error_name(found) + "): " + error);
            } else {
                ++judged;
                cl_int code = CL_SUCCESS;
                ClOwned<cl_mem> shared;
                shared.adopt(side.cl, api.CreateFromD3D10Buffer(side.cl.context, CL_MEM_READ_WRITE, buffer.get(), &code));
                cl_int plain_code = CL_SUCCESS;
                ClOwned<cl_mem> plain;
                plain.adopt(side.cl, side.cl.api.CreateBuffer(side.cl.context, CL_MEM_READ_WRITE, 64, nullptr, &plain_code));
                if (!shared || !plain) {
                    problems.add("d3d10: creating the shared buffer returned " + ClTools::error_name(code));
                } else {
                    cl_mem handle = shared.get();
                    cl_mem plain_handle = plain.get();
                    const cl_command_queue queue = side.cl.queue;
                    clkit::Kit::expect(problems, "d3d10: clEnqueueAcquireD3D10ObjectsKHR of the shared buffer", api.AcquireD3D10Objects(queue, 1, &handle, 0, nullptr, nullptr), {CL_SUCCESS});
                    clkit::Kit::expect(problems, "d3d10: a second clEnqueueAcquireD3D10ObjectsKHR", api.AcquireD3D10Objects(queue, 1, &handle, 0, nullptr, nullptr), {CL_D3D10_RESOURCE_ALREADY_ACQUIRED_KHR});
                    clkit::Kit::expect(problems, "d3d10: clEnqueueReleaseD3D10ObjectsKHR of the acquired buffer", api.ReleaseD3D10Objects(queue, 1, &handle, 0, nullptr, nullptr), {CL_SUCCESS});
                    clkit::Kit::expect(problems, "d3d10: a second clEnqueueReleaseD3D10ObjectsKHR", api.ReleaseD3D10Objects(queue, 1, &handle, 0, nullptr, nullptr), {CL_D3D10_RESOURCE_NOT_ACQUIRED_KHR});
                    clkit::Kit::expect(problems, "d3d10: clEnqueueAcquireD3D10ObjectsKHR of a plain buffer", api.AcquireD3D10Objects(queue, 1, &plain_handle, 0, nullptr, nullptr), {CL_INVALID_MEM_OBJECT});
                    side.cl.api.Finish(queue);
                }
            }
        } else {
            cl_context_properties plain[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), 0};
            cl_int code = CL_SUCCESS;
            side.cl.context = side.cl.api.CreateContext(plain, 1, &side.cl.device, nullptr, nullptr, &code);
            if (side.cl.context == nullptr) {
                problems.add("d3d10: a plain context cannot be created: " + ClTools::error_name(code));
            } else {
                ++judged;
                Com<ID3D10Buffer> buffer = D3d10Tools::buffer(d3d, 1024, D3D10_USAGE_DEFAULT, D3D10_BIND_VERTEX_BUFFER, 0, 0, nullptr);
                if (api.CreateFromD3D10Buffer != nullptr && buffer) {
                    cl_mem made = api.CreateFromD3D10Buffer(side.cl.context, CL_MEM_READ_WRITE, buffer.get(), &code);
                    if (made != nullptr) {
                        side.cl.api.ReleaseMemObject(made);
                        problems.add("d3d10: clCreateFromD3D10BufferKHR succeeds in a context without D3D10 sharing although cl_khr_d3d10_sharing is not advertised");
                    }
                }
                const cl_context_properties shared_props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_D3D10_DEVICE_KHR,
                                                              reinterpret_cast<cl_context_properties>(d3d.device), 0};
                cl_context made_context = side.cl.api.CreateContext(shared_props, 1, &side.cl.device, nullptr, nullptr, &code);
                if (made_context != nullptr) {
                    side.cl.api.ReleaseContext(made_context);
                    problems.add("d3d10: a context with CL_CONTEXT_D3D10_DEVICE_KHR is created although cl_khr_d3d10_sharing is not advertised");
                }
            }
        }
        side.cl.drain();
    }
    if (!ApiPolicy::is_skipped("d3d9")) {
        D3d9Rig rig;
        Side side;
        std::string error;
        D3d9Settings settings;
        settings.depth = false;
        if (!rig.open(true, error) || !rig.create(settings, error)) {
            context.note("dx9 media sharing is not judged: " + error);
        } else {
            side.adapter = rig.adapter;
            const Open state = side.open("cl_khr_dx9_media_sharing");
            if (state == Open::Failed) {
                return Outcome::fail(side.detail);
            }
            context.measure("dx9_media.advertised", state == Open::Ready);
            ClInterop& api = side.interop;
            IDirect3DDevice9Ex* device9 = rig.device_ex.get();
            cl_dx9_media_adapter_type_khr type = CL_ADAPTER_D3D9EX_KHR;
            if (state == Open::Ready) {
                void* adapters[1] = {device9};
                cl_device_id device = nullptr;
                cl_uint count = 0;
                const cl_int found = api.GetDeviceIDsFromDX9 != nullptr ? api.GetDeviceIDsFromDX9(side.cl.platform, 1, &type, adapters, CL_PREFERRED_DEVICES_FOR_DX9_MEDIA_ADAPTER_KHR, 1, &device, &count) : CL_INVALID_VALUE;
                const cl_context_properties props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_ADAPTER_D3D9EX_KHR,
                                                       reinterpret_cast<cl_context_properties>(device9), 0};
                HANDLE shared_handle = nullptr;
                Com<IDirect3DSurface9> surface;
                const HRESULT made = device9->CreateRenderTarget(16, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, surface.put(), &shared_handle);
                if (found != CL_SUCCESS || count == 0 || !side.context(props, device, error) || FAILED(made) || api.CreateFromDX9Surface == nullptr) {
                    problems.add("dx9: the sharing context cannot be set up (" + ClTools::error_name(found) + ", " + D3d9Tools::hr(made) + "): " + error);
                } else {
                    ++judged;
                    cl_dx9_surface_info_khr info{surface.get(), shared_handle};
                    cl_int code = CL_SUCCESS;
                    ClOwned<cl_mem> shared;
                    shared.adopt(side.cl, api.CreateFromDX9Surface(side.cl.context, CL_MEM_READ_WRITE, type, &info, 0, &code));
                    if (!shared) {
                        problems.add("dx9: creating the shared surface image returned " + ClTools::error_name(code));
                    } else {
                        cl_mem handle = shared.get();
                        const cl_command_queue queue = side.cl.queue;
                        clkit::Kit::expect(problems, "dx9: clEnqueueAcquireDX9MediaSurfacesKHR of the shared surface", api.AcquireDX9Surfaces(queue, 1, &handle, 0, nullptr, nullptr), {CL_SUCCESS});
                        clkit::Kit::expect(problems, "dx9: a second clEnqueueAcquireDX9MediaSurfacesKHR", api.AcquireDX9Surfaces(queue, 1, &handle, 0, nullptr, nullptr), {CL_DX9_MEDIA_SURFACE_ALREADY_ACQUIRED_KHR});
                        clkit::Kit::expect(problems, "dx9: clEnqueueReleaseDX9MediaSurfacesKHR of the acquired surface", api.ReleaseDX9Surfaces(queue, 1, &handle, 0, nullptr, nullptr), {CL_SUCCESS});
                        clkit::Kit::expect(problems, "dx9: a second clEnqueueReleaseDX9MediaSurfacesKHR", api.ReleaseDX9Surfaces(queue, 1, &handle, 0, nullptr, nullptr), {CL_DX9_MEDIA_SURFACE_NOT_ACQUIRED_KHR});
                        side.cl.api.Finish(queue);
                    }
                }
            } else {
                cl_context_properties plain[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), 0};
                cl_int code = CL_SUCCESS;
                const cl_context_properties shared_props[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(side.cl.platform), CL_CONTEXT_ADAPTER_D3D9EX_KHR,
                                                              reinterpret_cast<cl_context_properties>(device9), 0};
                cl_context made_context = side.cl.api.CreateContext(shared_props, 1, &side.cl.device, nullptr, nullptr, &code);
                ++judged;
                if (made_context != nullptr) {
                    side.cl.api.ReleaseContext(made_context);
                    problems.add("dx9: a context with CL_CONTEXT_ADAPTER_D3D9EX_KHR is created although cl_khr_dx9_media_sharing is not advertised");
                }
                side.cl.context = side.cl.api.CreateContext(plain, 1, &side.cl.device, nullptr, nullptr, &code);
                if (side.cl.context != nullptr && api.CreateFromDX9Surface != nullptr) {
                    HANDLE shared_handle = nullptr;
                    Com<IDirect3DSurface9> surface;
                    if (SUCCEEDED(device9->CreateRenderTarget(16, 16, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, TRUE, surface.put(), &shared_handle))) {
                        cl_dx9_surface_info_khr info{surface.get(), shared_handle};
                        cl_mem made = api.CreateFromDX9Surface(side.cl.context, CL_MEM_READ_WRITE, type, &info, 0, &code);
                        if (made != nullptr) {
                            side.cl.api.ReleaseMemObject(made);
                            problems.add("dx9: clCreateFromDX9MediaSurfaceKHR succeeds although cl_khr_dx9_media_sharing is not advertised");
                        }
                    }
                }
            }
            side.cl.drain();
        }
    }
    if (judged == 0 && problems.empty()) {
        return Context::skip("no Direct3D sharing path could be judged here");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::interop
