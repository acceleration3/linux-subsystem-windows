#include "render/d3d/warp_check.hpp"
#include "render/d3d11/d3d11.hpp"
#include "render/host/registration.hpp"

namespace lsw::suites::render::d3d11 {

void add(Suite& suite) {
    for (const auto* entry : rr::CaseTable<Kit>::instance().sorted()) {
        CaseRegistration::add(suite, "d3d11", "d3d11", entry->spec,
                              [entry](Context& context) { return WarpCheck::run<Kit>(context, *entry); });
    }
}

}  // namespace lsw::suites::render::d3d11
