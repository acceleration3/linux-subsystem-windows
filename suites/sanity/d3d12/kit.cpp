#include "sanity/d3d12/kit.hpp"

#include "support/dxgi_budget.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace lsw::suites::sanity::d3d12 {

Outcome Kit::finish(const std::vector<std::string>& skips, const Problems& problems) {
    if (!problems.empty()) {
        return problems.outcome();
    }
    if (!skips.empty()) {
        std::string reason;
        for (const std::string& item : skips) {
            reason += (reason.empty() ? "" : "; ") + item;
        }
        return Context::skip(reason);
    }
    return Outcome::ok();
}

Outcome Kit::each_device(Context& context, D3D_FEATURE_LEVEL level, const Body& body) {
    std::string error;
    const std::vector<AvAdapter> adapters = D3d12Rig::target_adapters(error);
    if (adapters.empty()) {
        return Outcome::fail("no target adapter: " + error);
    }
    Problems problems;
    std::vector<std::string> skips;
    std::size_t exercised = 0;
    for (const AvAdapter& adapter : adapters) {
        D3d12Rig rig;
        if (!rig.open(adapter, level, error)) {
            problems.add(adapter.name + ": " + error);
            continue;
        }
        Problems device_problems;
        const std::string skipped = body(rig, device_problems);
        std::string idle_error;
        if (!rig.idle(idle_error)) {
            device_problems.add("the queues did not go idle: " + idle_error);
        }
        if (!skipped.empty()) {
            skips.push_back(skipped);
        } else {
            ++exercised;
        }
        if (!device_problems.empty()) {
            problems.add(rig.describe() + ": " + device_problems.text());
        }
    }
    context.measure("d3d12.devices", adapters.size());
    if (exercised != 0 && problems.empty()) {
        return Outcome::ok();
    }
    return finish(skips, problems);
}

Outcome Kit::one_device(Context& context, D3D_FEATURE_LEVEL level, const Body& body) {
    std::string error;
    AvAdapter adapter;
    if (!TargetAdapter::pick(adapter, error)) {
        return Outcome::fail("no target adapter: " + error);
    }
    D3d12Rig rig;
    if (!rig.open(adapter, level, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const std::string skipped = body(rig, problems);
    std::string idle_error;
    if (!rig.idle(idle_error)) {
        problems.add("the queues did not go idle: " + idle_error);
    }
    static_cast<void>(context);
    if (!problems.empty()) {
        return problems.outcome();
    }
    return skipped.empty() ? Outcome::ok() : Context::skip(skipped);
}

bool Kit::expect(Problems& problems, const std::string& what, HRESULT got, std::initializer_list<HRESULT> accepted) {
    if (D3d12Tools::in(got, accepted)) {
        return true;
    }
    problems.add(what + " returned " + hr(got) + ", expected " + D3d12Tools::accepted_text(accepted));
    return false;
}

std::string Kit::sanitize(std::string text) {
    for (char& c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
            c = '_';
        }
    }
    return text;
}

bool Kit::same_bytes(Context& context, Problems& problems, const std::string& what, const void* expected, const void* actual,
                     std::size_t size) {
    if (size == 0 || std::memcmp(expected, actual, size) == 0) {
        return true;
    }
    const auto* a = static_cast<const std::uint8_t*>(expected);
    const auto* b = static_cast<const std::uint8_t*>(actual);
    std::size_t at = 0;
    while (at < size && a[at] == b[at]) {
        ++at;
    }
    problems.add(what + ": byte " + std::to_string(at) + " is 0x" + Text::hex(b[at], 2) + ", expected 0x" + Text::hex(a[at], 2));
    ArtifactWriter writer(context);
    const std::string base = sanitize(what);
    writer.bytes(base + ".expected", expected, size);
    writer.bytes(base + ".actual", actual, size);
    return false;
}

void Kit::compare_words(Context& context, Problems& problems, const std::string& what, const std::vector<std::uint32_t>& expected,
                        const std::vector<std::uint32_t>& actual) {
    if (expected.size() != actual.size()) {
        problems.add(what + ": " + std::to_string(actual.size()) + " words, expected " + std::to_string(expected.size()));
        return;
    }
    if (!expected.empty()) {
        static_cast<void>(same_bytes(context, problems, what, expected.data(), actual.data(), expected.size() * 4));
    }
}

std::vector<std::uint8_t> Kit::bytes(std::uint64_t seed, std::size_t size) {
    std::vector<std::uint8_t> out(size);
    if (size != 0) {
        Pattern::fill(seed, 0, out.data(), size);
    }
    return out;
}

std::vector<std::uint32_t> Kit::words(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint32_t> out(raw.size() / 4);
    if (!out.empty()) {
        std::memcpy(out.data(), raw.data(), out.size() * 4);
    }
    return out;
}

bool Kit::uniform(const TexelImage& image, std::uint32_t value, std::string& where) {
    for (UINT y = 0; y < image.height; ++y) {
        for (UINT x = 0; x < image.width; ++x) {
            if (image.pixel(x, y) != value) {
                where = "pixel (" + std::to_string(x) + "," + std::to_string(y) + ") is 0x" + Text::hex(image.pixel(x, y), 8) +
                        ", expected 0x" + Text::hex(value, 8);
                return false;
            }
        }
    }
    return true;
}

std::uint64_t Kit::device_local_budget(D3d12Rig& rig) {
    VideoMemory memory;
    std::string error;
    return DxgiBudget::query(rig.luid, true, memory, error) ? memory.budget : 0;
}

std::uint64_t Kit::host_budget(D3d12Rig& rig) {
    VideoMemory memory;
    std::string error;
    return DxgiBudget::query(rig.luid, false, memory, error) ? memory.budget : 0;
}

bool Kit::hazard_allowed() {
    return std::getenv("LSW_SUITES_ALLOW_HOST_HAZARD") != nullptr;
}

D3D12_RESOURCE_BINDING_TIER Kit::binding_tier(D3d12Rig& rig) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    static_cast<void>(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS, options));
    return options.ResourceBindingTier;
}

D3D12_RESOURCE_HEAP_TIER Kit::heap_tier(D3d12Rig& rig) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    static_cast<void>(D3d12Tools::feature(rig.device(), D3D12_FEATURE_D3D12_OPTIONS, options));
    return options.ResourceHeapTier;
}

D3D12_HEAP_FLAGS Kit::buffer_heap_flags(D3d12Rig& rig) {
    return heap_tier(rig) == D3D12_RESOURCE_HEAP_TIER_1 ? D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS : D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
}

D3D12_HEAP_FLAGS Kit::texture_heap_flags(D3d12Rig& rig, bool render_target) {
    if (heap_tier(rig) != D3D12_RESOURCE_HEAP_TIER_1) {
        return D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES;
    }
    return render_target ? D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES : D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
}

ID3D12PipelineState* Kit::pipeline(D3d12Rig& rig, ID3D12RootSignature* signature, const std::string& ps_source, DXGI_FORMAT target,
                                   std::string& error, const std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)>& tweak) {
    return pipeline_with(rig, signature, vs_source(), ps_source, target, error, tweak);
}

ID3D12PipelineState* Kit::pipeline_with(D3d12Rig& rig, ID3D12RootSignature* signature, const std::string& vs_text, const std::string& ps_source,
                                        DXGI_FORMAT target, std::string& error, const std::function<void(D3D12_GRAPHICS_PIPELINE_STATE_DESC&)>& tweak) {
    D3d12Tools::Bytes vs;
    D3d12Tools::Bytes ps;
    if (!D3d12Tools::compile(vs_text, "main", "vs_5_1", vs, error) || !D3d12Tools::compile(ps_source, "main", "ps_5_1", ps, error)) {
        return nullptr;
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = D3d12Tools::graphics_desc(signature, vs, ps, target);
    if (tweak) {
        tweak(desc);
    }
    return D3d12Tools::graphics_pso(rig, desc, error);
}

void Kit::draw_fullscreen(ID3D12GraphicsCommandList* list, ID3D12RootSignature* signature, ID3D12PipelineState* state) {
    list->SetGraphicsRootSignature(signature);
    list->SetPipelineState(state);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);
}

std::vector<std::uint32_t> Kit::image_words(const TexelImage& image) {
    return words(image.data);
}

std::string Kit::vs_source() {
    return D3d11Tools::kFullscreenVs;
}

}  // namespace lsw::suites::sanity::d3d12
