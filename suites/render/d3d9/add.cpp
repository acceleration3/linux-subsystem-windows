#include "render/d3d/warp_check.hpp"
#include "render/d3d9/d3d9.hpp"
#include "render/common/pattern.hpp"
#include "render/host/registration.hpp"

namespace lsw::suites::render::d3d9 {

namespace {

Outcome on12_clear(Context& context, unsigned variant) {
    Kit kit;
    std::string error;
    const rr::CaseSpec spec = rr::CaseSpec("oracle", "on12").variant(variant);
    if (kit.open(true, spec, error) != OpenStatus::Ok) {
        return Outcome::fail("Direct3DCreate9On12" + std::string(variant != 0 ? "Ex" : "") + " over WARP12 failed: " + error);
    }
    Surface target;
    rr::Image image;
    IDirect3DDevice9* d = kit.device();
    if (!kit.make_target(target, 32, 32, D3DFMT_A8R8G8B8, error)) {
        return Outcome::fail(error);
    }
    d->SetRenderTarget(0, target.surface.get());
    if (FAILED(d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 10, 200, 30), 1.0f, 0)) || !kit.read_color(target, image, error)) {
        return Outcome::fail("clear and readback through D3D9On12 failed: " + error);
    }
    const std::uint32_t pixel = image.rgba8(16, 16);
    context.measure("pixel", static_cast<double>(pixel));
    if (pixel != rr::Pattern::pack(10, 200, 30)) {
        return Outcome::fail("the D3D9On12 device cleared to a different colour than requested");
    }
    return Outcome::ok();
}

}  // namespace

void add(Suite& suite) {
    for (const std::string& problem : rr::CaseTable<Kit>::instance().problems()) {
        suite.add_problem("d3d9: " + problem);
    }
    rr::CaseSpec plain("oracle", "on12_warp_clear");
    rr::CaseSpec extended("oracle", "on12ex_warp_clear");
    CaseRegistration::add(suite, "d3d9", "d3d9", plain, [](Context& context) { return on12_clear(context, 0); });
    CaseRegistration::add(suite, "d3d9", "d3d9", extended, [](Context& context) { return on12_clear(context, 1); });
    add_caps(suite);
    for (const auto* entry : rr::CaseTable<Kit>::instance().sorted()) {
        CaseRegistration::add(suite, "d3d9", "d3d9", entry->spec,
                              [entry](Context& context) { return WarpCheck::run<Kit>(context, *entry); });
    }
}

}  // namespace lsw::suites::render::d3d9
