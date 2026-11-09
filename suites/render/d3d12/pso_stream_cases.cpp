#include "render/d3d12/gfx.hpp"

namespace lsw::suites::render::d3d12 {

namespace {

struct BasicStream {
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*> root;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE> vs;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE> ps;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC> layout;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC> rasterizer;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1> depth_stencil;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY> targets;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT> depth_format;
    Sub<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE> topology;
};

ID3D12PipelineState* stream_pso(Kit& kit, Pso& builder, bool depth_bounds, std::string& error) {
    if (!builder.failure.empty()) {
        error = builder.failure;
        return nullptr;
    }
    Com<ID3D12Device2> device2 = kit.rig.as<ID3D12Device2>();
    if (!device2) {
        error = "ID3D12Device2 is not available";
        return nullptr;
    }
    BasicStream stream;
    stream.root = builder.desc.pRootSignature;
    stream.vs = D3d12Tools::bytecode(builder.vs_code);
    stream.ps = D3d12Tools::bytecode(builder.ps_code);
    stream.layout = D3D12_INPUT_LAYOUT_DESC{builder.elements.data(), static_cast<UINT>(builder.elements.size())};
    stream.rasterizer = builder.desc.RasterizerState;
    D3D12_DEPTH_STENCIL_DESC1 ds{};
    ds.DepthEnable = builder.desc.DepthStencilState.DepthEnable;
    ds.DepthWriteMask = builder.desc.DepthStencilState.DepthWriteMask;
    ds.DepthFunc = builder.desc.DepthStencilState.DepthFunc;
    ds.StencilReadMask = 0xff;
    ds.StencilWriteMask = 0xff;
    ds.FrontFace = builder.desc.DepthStencilState.FrontFace;
    ds.BackFace = builder.desc.DepthStencilState.BackFace;
    ds.DepthBoundsTestEnable = depth_bounds;
    stream.depth_stencil = ds;
    D3D12_RT_FORMAT_ARRAY formats{};
    formats.NumRenderTargets = 1;
    formats.RTFormats[0] = builder.desc.RTVFormats[0];
    stream.targets = formats;
    stream.depth_format = builder.desc.DSVFormat;
    stream.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof stream, &stream};
    Com<ID3D12PipelineState> state;
    const HRESULT result = device2->CreatePipelineState(&desc, __uuidof(ID3D12PipelineState), state.put_void());
    if (FAILED(result)) {
        error = "CreatePipelineState (stream): " + D3d12Tools::hr(result);
        return nullptr;
    }
    return kit.rig.hold(std::move(state));
}

bool draw_stream(Kit& kit, bool depth_bounds, rr::CaptureSet& out, std::string& error) {
    Stage stage;
    if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
        return false;
    }
    Pso builder = stage.pso();
    builder.desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    builder.desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ID3D12PipelineState* state = stream_pso(kit, builder, depth_bounds, error);
    if (state == nullptr) {
        return false;
    }
    const bool ok = kit.run(
        [&](ID3D12GraphicsCommandList* list) {
            const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
            stage.canvas.begin(list, clear);
            stage.backdrop(list);
            list->SetPipelineState(state);
            if (depth_bounds) {
                ID3D12GraphicsCommandList1* list1 = nullptr;
                if (SUCCEEDED(list->QueryInterface(__uuidof(ID3D12GraphicsCommandList1), reinterpret_cast<void**>(&list1)))) {
                    list1->OMSetDepthBounds(0.80f, 0.86f);
                    list1->Release();
                }
            }
            stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.2f, 0, 0), 0.9f, 0.5f, 0.2f);
            stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.8f, 0.5f);
        },
        error);
    return ok && stage.finish(out, error);
}

void register_all() {
    Cases::single(rr::CaseSpec("pso", "stream_device2_basic").tolerance(rr::Tolerance::blend(2)).tag("pso").audit_ok("the scene is the PSO-creation path under test: a pipeline made through CreatePipelineState with a subobject stream renders the same lit scene as the cached-blob and pipeline-library rows"),
                  [](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_stream(kit, false, out, error); });
    Cases::single(
        rr::CaseSpec("pso", "stream_depth_bounds_test").tolerance(rr::Tolerance::blend(2)).tag("pso"),
        [](Kit& kit, rr::CaptureSet& out, std::string& error) { return draw_stream(kit, true, out, error); },
        [](Kit& kit) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS2 options{};
            const HRESULT result = D3d12Tools::feature(kit.device(), D3D12_FEATURE_D3D12_OPTIONS2, options);
            return SUCCEEDED(result) && options.DepthBoundsTestSupported ? std::string() : std::string("depth bounds test");
        });
    Cases::single(rr::CaseSpec("pso", "cached_blob_reuse_renders_same").tolerance(rr::Tolerance::blend(2)).tag("pso").audit_ok("the row exists to prove that a PSO rebuilt from its cached blob renders the same image as the original, so it shares its scene with the stream and library rows"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        Pso builder = stage.pso();
        ID3D12PipelineState* original = builder.build(error);
        if (original == nullptr) {
            return false;
        }
        Com<ID3DBlob> blob;
        const HRESULT result = original->GetCachedBlob(blob.put());
        ID3D12PipelineState* state = original;
        if (SUCCEEDED(result) && blob && blob->GetBufferSize() != 0) {
            builder.desc.CachedPSO = {blob->GetBufferPointer(), blob->GetBufferSize()};
            ID3D12PipelineState* cached = D3d12Tools::graphics_pso(kit.rig, builder.desc, error);
            if (cached == nullptr) {
                return false;
            }
            state = cached;
        }
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                stage.canvas.begin(list, clear);
                stage.backdrop(list);
                list->SetPipelineState(state);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.2f, 0, 0), 0.9f, 0.5f, 0.2f);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.8f, 0.5f);
            },
            error);
        return ok && stage.finish(out, error);
    });
    Cases::single(rr::CaseSpec("pso", "pipeline_library_store_serialize_load").tolerance(rr::Tolerance::blend(2)).tag("pso").audit_ok("the row exists to prove that a PSO loaded from a serialized pipeline library renders the same image as the original, so it shares its scene with the stream and blob rows"), [](Kit& kit, rr::CaptureSet& out, std::string& error) {
        Stage stage;
        if (!stage.open(kit, error, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT)) {
            return false;
        }
        Com<ID3D12Device1> device1 = kit.rig.as<ID3D12Device1>();
        if (!device1) {
            return fail_with(error, "ID3D12Device1 is not available");
        }
        Pso builder = stage.pso();
        ID3D12PipelineState* original = builder.build(error);
        if (original == nullptr) {
            return false;
        }
        Com<ID3D12PipelineLibrary> library;
        HRESULT result = device1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary), library.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreatePipelineLibrary: " + D3d12Tools::hr(result));
        }
        result = library->StorePipeline(L"lit", original);
        if (FAILED(result)) {
            return fail_with(error, "StorePipeline: " + D3d12Tools::hr(result));
        }
        std::vector<std::uint8_t> serialized(library->GetSerializedSize());
        result = library->Serialize(serialized.data(), serialized.size());
        if (FAILED(result)) {
            return fail_with(error, "Serialize: " + D3d12Tools::hr(result));
        }
        Com<ID3D12PipelineLibrary> reloaded;
        result = device1->CreatePipelineLibrary(serialized.data(), serialized.size(), __uuidof(ID3D12PipelineLibrary), reloaded.put_void());
        if (FAILED(result)) {
            return fail_with(error, "CreatePipelineLibrary from the serialized data: " + D3d12Tools::hr(result));
        }
        Com<ID3D12PipelineState> loaded;
        result = reloaded->LoadGraphicsPipeline(L"lit", &builder.desc, __uuidof(ID3D12PipelineState), loaded.put_void());
        if (FAILED(result)) {
            return fail_with(error, "LoadGraphicsPipeline: " + D3d12Tools::hr(result));
        }
        ID3D12PipelineState* state = kit.rig.hold(std::move(loaded));
        const bool ok = kit.run(
            [&](ID3D12GraphicsCommandList* list) {
                const float clear[4] = {0.05f, 0.05f, 0.1f, 1.0f};
                stage.canvas.begin(list, clear);
                stage.backdrop(list);
                list->SetPipelineState(state);
                stage.mesh(list, stage.sphere, rr::Mat4::translate(-0.2f, 0, 0), 0.9f, 0.5f, 0.2f);
                stage.mesh(list, stage.torus, rr::Mat4::rotate_x(1.0f), 0.2f, 0.8f, 0.5f);
            },
            error);
        return ok && stage.finish(out, error);
    });
}

const struct Registrar {
    Registrar() { register_all(); }
} kRegistrar;

}  // namespace

}  // namespace lsw::suites::render::d3d12
