#include "sanity/memory/leak_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/api_loop.hpp"
#include "support/api_policy.hpp"
#include "support/child_runs.hpp"
#include "support/d3d11_textures.hpp"
#include "support/d3d12_backend.hpp"
#include "support/d3d9_backend.hpp"
#include "support/dxgi_budget.hpp"
#include "support/dyn_library.hpp"
#include "support/gl_backend.hpp"
#include "support/handle_types.hpp"
#include "support/leak_loop.hpp"
#include "support/memory_tools.hpp"
#include "support/opencl_backend.hpp"
#include "support/opengl_context.hpp"
#include "support/process_sampler.hpp"
#include "support/pattern.hpp"
#include "support/target_adapter.hpp"
#include "support/test_window.hpp"
#include "support/vulkan_backend.hpp"

#include <d3d11_1.h>
#include <dxgi1_2.h>

namespace lsw::suites::sanity::memory {

namespace {

constexpr int kCount = 5000;
constexpr int kDeviceCount = 300;
constexpr int kBuildCount = 500;

bool pick(AvAdapter& adapter, std::string& error) {
    return TargetAdapter::pick(adapter, error);
}

}  // namespace

Outcome LeakChecks::buffers_all_apis(Context& context) {
    Problems problems;
    ApiLoop::run(problems, {"d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl"},
                 [&](GpuBackend& backend, const std::string& api, const AvAdapter& adapter) {
        std::vector<std::uint8_t> payload(4096);
        Pattern::fill(1, 0, payload.data(), payload.size());
        LeakLoop::run(context, problems, api + ".buffers", adapter.luid, kCount, [&](int i, std::string& error) {
            AllocStatus status = AllocStatus::Ok;
            std::unique_ptr<GpuBuffer> buffer = backend.create(256 << 10, BufferKind::Device, status, error);
            if (buffer == nullptr || !backend.upload(*buffer, 0, payload.data(), payload.size(), error)) {
                return false;
            }
            return i % 32 != 31 || backend.finish(error);
        });
        std::string error;
        static_cast<void>(backend.finish(error));
    });
    return problems.outcome();
}

Outcome LeakChecks::textures_all_apis(Context& context) {
    AvAdapter adapter;
    std::string error;
    Problems problems;
    if (!pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        if (!device.create(adapter.luid, error)) {
            problems.add(error);
        } else {
            LeakLoop::run(context, problems, "d3d11.textures", adapter.luid, kCount, [&](int i, std::string& detail) {
                HRESULT result = S_OK;
                ID3D11Texture2D* texture = D3d11Textures::make2d(device, 256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                                 D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, result);
                if (texture == nullptr) {
                    detail = "CreateTexture2D failed";
                    return false;
                }
                texture->Release();
                if (i % 64 == 63) {
                    device.context->Flush();
                }
                return true;
            });
        }
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Backend backend;
        if (!backend.init(adapter.luid, error)) {
            problems.add(error);
        } else {
            LeakLoop::run(context, problems, "d3d12.textures", adapter.luid, kCount, [&](int, std::string& detail) {
                D3D12_HEAP_PROPERTIES heap{};
                heap.Type = D3D12_HEAP_TYPE_DEFAULT;
                D3D12_RESOURCE_DESC desc{};
                desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                desc.Width = 256;
                desc.Height = 256;
                desc.DepthOrArraySize = 1;
                desc.MipLevels = 1;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
                ID3D12Resource* resource = nullptr;
                const HRESULT result = backend.shared().device->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource),
                    reinterpret_cast<void**>(&resource));
                if (FAILED(result)) {
                    detail = "CreateCommittedResource (texture) failed";
                    return false;
                }
                resource->Release();
                return true;
            });
        }
    }
    if (!ApiPolicy::is_skipped("d3d9")) {
        D3d9Backend backend;
        if (!backend.init(adapter.luid, error)) {
            problems.add(error);
        } else {
            LeakLoop::run(context, problems, "d3d9.textures", adapter.luid, kCount, [&](int, std::string& detail) {
                IDirect3DTexture9* texture = nullptr;
                if (FAILED(backend.device()->CreateTexture(256, 256, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture, nullptr))) {
                    detail = "CreateTexture failed";
                    return false;
                }
                texture->Release();
                return true;
            });
        }
    }
    if (!ApiPolicy::is_skipped("gl")) {
        TestWindow window;
        OpenGlContext gl;
        if (!window.create({}, error) || !gl.create(window.dc(), error)) {
            problems.add(error);
        } else {
            using GenTextures = void(__stdcall*)(int, unsigned*);
            using DeleteTextures = void(__stdcall*)(int, const unsigned*);
            using BindTexture = void(__stdcall*)(unsigned, unsigned);
            using TexImage = void(__stdcall*)(unsigned, int, int, int, int, int, unsigned, unsigned, const void*);
            auto gen = reinterpret_cast<GenTextures>(gl.export_symbol("glGenTextures"));
            auto del = reinterpret_cast<DeleteTextures>(gl.export_symbol("glDeleteTextures"));
            auto bind = reinterpret_cast<BindTexture>(gl.export_symbol("glBindTexture"));
            auto image = reinterpret_cast<TexImage>(gl.export_symbol("glTexImage2D"));
            LeakLoop::run(context, problems, "gl.textures", adapter.luid, kCount, [&](int, std::string&) {
                unsigned id = 0;
                gen(1, &id);
                bind(0x0DE1, id);
                image(0x0DE1, 0, 0x8058, 256, 256, 0, 0x1908, 0x1401, nullptr);
                del(1, &id);
                return true;
            });
        }
    }
    {
        VulkanBackend backend;
        if (backend.init(adapter.luid, error)) {
            context.note("Vulkan image and OpenCL image loops are not part of this row: buffers cover their memory path");
        }
    }
    return problems.outcome();
}

Outcome LeakChecks::views_and_samplers(Context& context) {
    AvAdapter adapter;
    std::string error;
    Problems problems;
    if (!pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        HRESULT result = S_OK;
        if (!device.create(adapter.luid, error)) {
            problems.add(error);
        } else if (ID3D11Texture2D* texture = D3d11Textures::make2d(device, 128, 128, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                                                                  D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, result)) {
            LeakLoop::run(context, problems, "d3d11.views", adapter.luid, kCount, [&](int, std::string& detail) {
                ID3D11ShaderResourceView* srv = nullptr;
                ID3D11RenderTargetView* rtv = nullptr;
                ID3D11SamplerState* sampler = nullptr;
                D3D11_SAMPLER_DESC desc{};
                desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
                desc.MaxLOD = D3D11_FLOAT32_MAX;
                const bool ok = SUCCEEDED(device.device->CreateShaderResourceView(texture, nullptr, &srv)) &&
                                SUCCEEDED(device.device->CreateRenderTargetView(texture, nullptr, &rtv)) &&
                                SUCCEEDED(device.device->CreateSamplerState(&desc, &sampler));
                for (IUnknown* object : {static_cast<IUnknown*>(srv), static_cast<IUnknown*>(rtv), static_cast<IUnknown*>(sampler)}) {
                    if (object != nullptr) {
                        object->Release();
                    }
                }
                detail = "creating a view or sampler failed";
                return ok;
            });
            texture->Release();
        } else {
            problems.add("creating the view texture failed");
        }
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Backend backend;
        if (!backend.init(adapter.luid, error)) {
            problems.add(error);
        } else {
            LeakLoop::run(context, problems, "d3d12.descriptor_heaps", adapter.luid, kCount, [&](int, std::string& detail) {
                D3D12_DESCRIPTOR_HEAP_DESC desc{};
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
                desc.NumDescriptors = 16;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                ID3D12DescriptorHeap* heap = nullptr;
                if (FAILED(backend.shared().device->CreateDescriptorHeap(&desc, __uuidof(ID3D12DescriptorHeap), reinterpret_cast<void**>(&heap)))) {
                    detail = "CreateDescriptorHeap failed";
                    return false;
                }
                D3D12_SAMPLER_DESC sampler{};
                sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
                sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
                sampler.MaxLOD = D3D12_FLOAT32_MAX;
                backend.shared().device->CreateSampler(&sampler, heap->GetCPUDescriptorHandleForHeapStart());
                heap->Release();
                return true;
            });
        }
    }
    if (!ApiPolicy::is_skipped("gl")) {
        TestWindow window;
        OpenGlContext gl;
        if (window.create({}, error) && gl.create(window.dc(), error)) {
            using Gen = void(__stdcall*)(int, unsigned*);
            using Del = void(__stdcall*)(int, const unsigned*);
            using Param = void(__stdcall*)(unsigned, unsigned, int);
            auto gen = reinterpret_cast<Gen>(gl.proc("glGenSamplers"));
            auto del = reinterpret_cast<Del>(gl.proc("glDeleteSamplers"));
            auto param = reinterpret_cast<Param>(gl.proc("glSamplerParameteri"));
            if (gen == nullptr || del == nullptr || param == nullptr) {
                context.note("OpenGL sampler objects are not available on this context: " + gl.version());
            } else {
                LeakLoop::run(context, problems, "gl.samplers", adapter.luid, kCount, [&](int, std::string&) {
                    unsigned id = 0;
                    gen(1, &id);
                    param(id, 0x2801, 0x2601);
                    del(1, &id);
                    return true;
                });
            }
        }
    }
    context.note("Vulkan image-view and OpenCL sampler loops are not part of this row");
    return problems.outcome();
}

Outcome LeakChecks::shaders_pipelines(Context& context) {
    AvAdapter adapter;
    std::string error;
    Problems problems;
    if (!pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        DynLibrary compiler;
        using Compile = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*, const char*, const char*,
                                         UINT, UINT, ID3DBlob**, ID3DBlob**);
        if (!device.create(adapter.luid, error)) {
            problems.add(error);
        } else if (!compiler.load("d3dcompiler_47.dll")) {
            problems.add("d3dcompiler_47.dll cannot be loaded");
        } else {
            auto compile = compiler.symbol<Compile>("D3DCompile");
            static const char kVertex[] = "float4 main(float4 p : POSITION) : SV_Position { return p; }";
            static const char kPixel[] = "float4 main() : SV_Target { return float4(1,0,0,1); }";
            ID3DBlob* vs_blob = nullptr;
            ID3DBlob* ps_blob = nullptr;
            if (compile == nullptr || FAILED(compile(kVertex, sizeof(kVertex) - 1, "vs", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, nullptr)) ||
                FAILED(compile(kPixel, sizeof(kPixel) - 1, "ps", nullptr, nullptr, "main", "ps_5_0", 0, 0, &ps_blob, nullptr))) {
                problems.add("compiling the D3D11 shaders failed");
            } else {
                LeakLoop::run(context, problems, "d3d11.shaders", adapter.luid, kCount, [&](int, std::string& detail) {
                    ID3D11VertexShader* vs = nullptr;
                    ID3D11PixelShader* ps = nullptr;
                    const bool ok = SUCCEEDED(device.device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs)) &&
                                    SUCCEEDED(device.device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps));
                    if (vs != nullptr) {
                        vs->Release();
                    }
                    if (ps != nullptr) {
                        ps->Release();
                    }
                    detail = "creating a D3D11 shader failed";
                    return ok;
                });
            }
        }
    }
    {
        OpenClBackend backend;
        if (backend.init(adapter.luid, error)) {
            LeakLoop::run(context, problems, "opencl.programs", adapter.luid, kBuildCount, [&](int, std::string& detail) {
                return backend.churn_program(detail);
            });
        } else {
            problems.add(error);
        }
    }
    if (!ApiPolicy::is_skipped("gl")) {
        TestWindow window;
        OpenGlContext gl;
        if (window.create({}, error) && gl.create(window.dc(), error)) {
            using Create = unsigned(__stdcall*)(unsigned);
            using Source = void(__stdcall*)(unsigned, int, const char* const*, const int*);
            using Compile = void(__stdcall*)(unsigned);
            using Delete = void(__stdcall*)(unsigned);
            using CreateProgram = unsigned(__stdcall*)();
            using Attach = void(__stdcall*)(unsigned, unsigned);
            using Link = void(__stdcall*)(unsigned);
            auto create_shader = reinterpret_cast<Create>(gl.proc("glCreateShader"));
            auto source = reinterpret_cast<Source>(gl.proc("glShaderSource"));
            auto compile = reinterpret_cast<Compile>(gl.proc("glCompileShader"));
            auto delete_shader = reinterpret_cast<Delete>(gl.proc("glDeleteShader"));
            auto create_program = reinterpret_cast<CreateProgram>(gl.proc("glCreateProgram"));
            auto attach = reinterpret_cast<Attach>(gl.proc("glAttachShader"));
            auto link = reinterpret_cast<Link>(gl.proc("glLinkProgram"));
            auto delete_program = reinterpret_cast<Delete>(gl.proc("glDeleteProgram"));
            if (create_shader == nullptr || source == nullptr || compile == nullptr || create_program == nullptr || link == nullptr) {
                context.note("OpenGL shader objects are not available: " + gl.version());
            } else {
                const char* text = "#version 120\nvoid main() { gl_Position = gl_Vertex; }\n";
                LeakLoop::run(context, problems, "gl.programs", adapter.luid, kBuildCount, [&](int, std::string&) {
                    const unsigned shader = create_shader(0x8B31);
                    source(shader, 1, &text, nullptr);
                    compile(shader);
                    const unsigned program = create_program();
                    attach(program, shader);
                    link(program);
                    delete_program(program);
                    delete_shader(shader);
                    return true;
                });
            }
        }
    }
    context.note("D3D12 and Vulkan pipeline loops are not part of this row");
    return problems.outcome();
}

Outcome LeakChecks::contexts_devices(Context& context) {
    AvAdapter adapter;
    std::string error;
    Problems problems;
    if (!pick(adapter, error)) {
        return Outcome::fail(error);
    }
    for (const std::string& api : ApiPolicy::filter({"d3d11", "d3d12", "vulkan", "opencl", "gl", "d3d9"})) {
        LeakLoop::run(context, problems, api + ".devices", adapter.luid, kDeviceCount, [&](int, std::string& detail) {
            std::unique_ptr<GpuBackend> backend = GpuBackends::make(api);
            return backend != nullptr && backend->init(adapter.luid, detail);
        });
        AvAdapter again;
        std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, again, error);
        if (backend == nullptr) {
            problems.add(api + ": a new device cannot be created after the loop: " + error);
        }
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        TestWindow window;
        if (device.create(adapter.luid, error) && window.create({}, error)) {
            IDXGIDevice* dxgi_device = nullptr;
            IDXGIAdapter* dxgi_adapter = nullptr;
            IDXGIFactory2* factory = nullptr;
            device.device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi_device));
            dxgi_device->GetAdapter(&dxgi_adapter);
            dxgi_adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory));
            LeakLoop::run(context, problems, "d3d11.swap_chains", adapter.luid, kDeviceCount, [&](int, std::string& detail) {
                DXGI_SWAP_CHAIN_DESC1 desc{};
                desc.Width = 256;
                desc.Height = 256;
                desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                desc.SampleDesc.Count = 1;
                desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                desc.BufferCount = 2;
                desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                IDXGISwapChain1* chain = nullptr;
                const HRESULT result = factory->CreateSwapChainForHwnd(device.device, window.hwnd(), &desc, nullptr, nullptr, &chain);
                if (FAILED(result)) {
                    detail = "CreateSwapChainForHwnd failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8);
                    return false;
                }
                chain->Release();
                return true;
            });
            factory->Release();
            dxgi_adapter->Release();
            dxgi_device->Release();
        } else {
            problems.add(error);
        }
    }
    return problems.outcome();
}

Outcome LeakChecks::sync_objects(Context& context) {
    AvAdapter adapter;
    std::string error;
    Problems problems;
    if (!pick(adapter, error)) {
        return Outcome::fail(error);
    }
    if (!ApiPolicy::is_skipped("d3d11")) {
        D3d11Device device;
        if (device.create(adapter.luid, error)) {
            LeakLoop::run(context, problems, "d3d11.queries", adapter.luid, kCount, [&](int, std::string& detail) {
                D3D11_QUERY_DESC desc{};
                desc.Query = D3D11_QUERY_EVENT;
                ID3D11Query* query = nullptr;
                if (FAILED(device.device->CreateQuery(&desc, &query))) {
                    detail = "CreateQuery failed";
                    return false;
                }
                device.context->End(query);
                query->Release();
                return true;
            });
        } else {
            problems.add(error);
        }
    }
    if (!ApiPolicy::is_skipped("d3d12")) {
        D3d12Backend backend;
        if (backend.init(adapter.luid, error)) {
            LeakLoop::run(context, problems, "d3d12.fences", adapter.luid, kCount, [&](int i, std::string& detail) {
                ID3D12Fence* fence = nullptr;
                if (FAILED(backend.shared().device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), reinterpret_cast<void**>(&fence)))) {
                    detail = "CreateFence failed";
                    return false;
                }
                HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
                fence->SetEventOnCompletion(0, event);
                if (i % 2 == 0) {
                    fence->Signal(1);
                }
                ::CloseHandle(event);
                fence->Release();
                return true;
            });
        } else {
            problems.add(error);
        }
    }
    if (!ApiPolicy::is_skipped("gl")) {
        TestWindow window;
        OpenGlContext gl;
        if (window.create({}, error) && gl.create(window.dc(), error)) {
            using Fence = void*(__stdcall*)(unsigned, unsigned);
            using DeleteSync = void(__stdcall*)(void*);
            auto fence = reinterpret_cast<Fence>(gl.proc("glFenceSync"));
            auto del = reinterpret_cast<DeleteSync>(gl.proc("glDeleteSync"));
            if (fence != nullptr && del != nullptr) {
                LeakLoop::run(context, problems, "gl.sync", adapter.luid, kCount, [&](int, std::string&) {
                    del(fence(0x9117, 0));
                    return true;
                });
            } else {
                context.note("OpenGL sync objects are not available: " + gl.version());
            }
        }
    }
    context.note("Vulkan fence and semaphore loops and OpenCL event loops are not part of this row");
    return problems.outcome();
}

Outcome LeakChecks::shared_resources(Context& context) {
    constexpr int kShared = 200;
    const std::string prefix = ChildMode::variable(L"LSW_SUITES_SHARED_PREFIX");
    if (ChildMode::active() && !prefix.empty()) {
        AvAdapter adapter;
        std::string error;
        D3d11Device device;
        if (!pick(adapter, error) || !device.create(adapter.luid, error)) {
            return Outcome::fail(error);
        }
        ID3D11Device1* device1 = nullptr;
        device.device->QueryInterface(__uuidof(ID3D11Device1), reinterpret_cast<void**>(&device1));
        if (device1 == nullptr) {
            return Outcome::fail("ID3D11Device1 is not available");
        }
        const ProcessSample before = ProcessSampler::take();
        const auto types_before = HandleTypes::count();
        for (int i = 0; i < kShared; ++i) {
            ID3D11Texture2D* texture = nullptr;
            const HRESULT result = device1->OpenSharedResourceByName(Text::wide(prefix + std::to_string(i)).c_str(), DXGI_SHARED_RESOURCE_READ,
                                                                      __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture));
            if (FAILED(result)) {
                return Outcome::fail("OpenSharedResourceByName " + std::to_string(i) + " failed: 0x" + Text::hex(static_cast<std::uint32_t>(result), 8));
            }
            texture->Release();
        }
        device.context->Flush();
        ::Sleep(300);
        const ProcessSample after = ProcessSampler::take();
        context.measure("child.private_delta", static_cast<long long>(after.private_bytes) - static_cast<long long>(before.private_bytes));
        context.measure("child.handle_delta", static_cast<long long>(after.handles) - static_cast<long long>(before.handles));
        context.measure("child.handle_types", HandleTypes::grown(types_before, HandleTypes::count()));
        device1->Release();
        return Outcome::ok();
    }
    AvAdapter adapter;
    std::string error;
    D3d11Device device;
    if (!pick(adapter, error) || !device.create(adapter.luid, error)) {
        return Outcome::fail(error);
    }
    const std::string name_prefix = "Local\\lsw-suites-shared-" + std::to_string(::GetCurrentProcessId()) + "-";
    const ProcessSample before = ProcessSampler::take();
    VideoMemory gpu_before;
    static_cast<void>(DxgiBudget::query(adapter.luid, true, gpu_before, error));
    Problems problems;
    CheckResult child;
    {
        std::vector<ID3D11Texture2D*> textures;
        std::vector<HANDLE> handles;
        for (int i = 0; i < kShared; ++i) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 256;
            desc.Height = 256;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            ID3D11Texture2D* texture = nullptr;
            IDXGIResource1* resource = nullptr;
            HANDLE handle = nullptr;
            if (FAILED(device.device->CreateTexture2D(&desc, nullptr, &texture)) ||
                FAILED(texture->QueryInterface(__uuidof(IDXGIResource1), reinterpret_cast<void**>(&resource))) ||
                FAILED(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                                    Text::wide(name_prefix + std::to_string(i)).c_str(), &handle))) {
                problems.add("creating shared texture " + std::to_string(i) + " failed");
                break;
            }
            resource->Release();
            textures.push_back(texture);
            handles.push_back(handle);
        }
        if (problems.empty()) {
            ChildRunOptions options;
            options.timeout_seconds = 120;
            options.env = {{"LSW_SUITES_SHARED_PREFIX", name_prefix}};
            child = ChildRuns::run_self("sanity.mem.leak.shared_resources", options);
            if (child.status != Status::Ok) {
                problems.add("the second process failed: " + child.reason);
            }
        }
        for (HANDLE handle : handles) {
            ::CloseHandle(handle);
        }
        for (ID3D11Texture2D* texture : textures) {
            texture->Release();
        }
    }
    device.context->Flush();
    ::Sleep(500);
    const ProcessSample after = ProcessSampler::take();
    VideoMemory gpu_after;
    static_cast<void>(DxgiBudget::query(adapter.luid, true, gpu_after, error));
    context.measure("parent.private_delta", static_cast<long long>(after.private_bytes) - static_cast<long long>(before.private_bytes));
    context.measure("parent.handle_delta", static_cast<long long>(after.handles) - static_cast<long long>(before.handles));
    context.measure("parent.gpu_delta", static_cast<long long>(gpu_after.usage) - static_cast<long long>(gpu_before.usage));
    if (static_cast<long long>(after.handles) - static_cast<long long>(before.handles) > LeakLoop::kHandleTolerance) {
        problems.add("handles were not released in the creating process");
    }
    if (static_cast<long long>(gpu_after.usage) - static_cast<long long>(gpu_before.usage) > static_cast<long long>(LeakLoop::kGpuTolerance)) {
        problems.add("GPU usage did not return to its baseline in the creating process");
    }
    if (ChildRuns::number(child, "child.handle_delta") > LeakLoop::kHandleTolerance) {
        problems.add("the opening process kept " + std::to_string(ChildRuns::number(child, "child.handle_delta")) + " handles after closing the resources (" +
                     ChildRuns::text(child, "child.handle_types") + ")");
    }
    if (ChildRuns::number(child, "child.private_delta") > static_cast<long long>(LeakLoop::kPrivateTolerance)) {
        problems.add("the opening process kept private memory after closing the resources");
    }
    return problems.outcome();
}

Outcome LeakChecks::process_exit_releases(Context& context) {
    AvAdapter adapter;
    std::string error;
    if (ChildMode::active()) {
        for (const std::string& api : GpuBackends::names()) {
            std::unique_ptr<GpuBackend> backend = BackendHarness::open(api, adapter, error);
            if (backend == nullptr) {
                return Outcome::fail(error);
            }
            const std::uint64_t each = std::min<std::uint64_t>(backend->available_memory() / 8, 512ull << 20);
            for (int i = 0; i < 4; ++i) {
                AllocStatus status = AllocStatus::Ok;
                std::string detail;
                GpuBuffer* buffer = backend->create(each, BufferKind::Device, status, detail).release();
                if (buffer == nullptr || !MemoryTools::touch(*backend, *buffer, 5, detail)) {
                    return Outcome::fail(api + ": " + detail);
                }
            }
            static_cast<void>(backend->finish(error));
            context.measure(api + ".held", each * 4);
            backend.release();
        }
        return Outcome::ok();
    }
    if (!pick(adapter, error)) {
        return Outcome::fail(error);
    }
    const std::string own_api = ApiPolicy::first_enabled({"d3d12", "d3d11", "vulkan", "opencl"});
    if (own_api.empty()) {
        return Context::skip("every API of this row is excluded by LSW_SUITES_SKIP_APIS");
    }
    std::unique_ptr<GpuBackend> d3d12 = BackendHarness::open(own_api, adapter, error);
    if (d3d12 == nullptr) {
        return Outcome::fail(error);
    }
    VideoMemory before;
    VideoMemory after;
    if (!DxgiBudget::query(adapter.luid, true, before, error)) {
        return Outcome::fail(error);
    }
    const ProcessSample sample_before = ProcessSampler::take();
    ChildRunOptions options;
    options.timeout_seconds = 180;
    const CheckResult child = ChildRuns::run_self("sanity.mem.leak.process_exit_releases", options);
    if (child.status != Status::Ok) {
        return Outcome::fail("the allocating process failed: " + child.reason);
    }
    bool restored = false;
    for (int poll = 0; poll < 100 && !restored; ++poll) {
        if (DxgiBudget::query(adapter.luid, true, after, error)) {
            const std::uint64_t free_before = before.budget > before.usage ? before.budget - before.usage : 0;
            const std::uint64_t free_after = after.budget > after.usage ? after.budget - after.usage : 0;
            restored = free_after + (256ull << 20) >= free_before;
        }
        if (!restored) {
            ::Sleep(100);
        }
    }
    context.measure("budget.before", before.budget - std::min(before.budget, before.usage));
    context.measure("budget.after", after.budget - std::min(after.budget, after.usage));
    Problems problems;
    if (!restored) {
        problems.add("the memory held by the exited process was not returned to the budget");
    }
    const std::uint64_t request = std::min<std::uint64_t>(d3d12->available_memory() / 2, 2ull << 30);
    AllocStatus status = AllocStatus::Ok;
    std::string detail;
    std::unique_ptr<GpuBuffer> buffer = d3d12->create(request, BufferKind::Device, status, detail);
    if (buffer == nullptr) {
        problems.add("allocating " + std::to_string(request >> 20) + " MiB after the process exit fails: " + detail);
    }
    const ProcessSample sample_after = ProcessSampler::take();
    if (static_cast<long long>(sample_after.handles) - static_cast<long long>(sample_before.handles) > LeakLoop::kHandleTolerance) {
        problems.add("this process gained handles from the child's exit");
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::memory
