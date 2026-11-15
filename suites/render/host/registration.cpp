#include "render/host/registration.hpp"

#include "support/api_policy.hpp"

namespace lsw::suites::render {

void CaseRegistration::add(Suite& suite, const std::string& api, const std::string& api_tag, const rr::CaseSpec& spec,
                           std::function<Outcome(Context&)> run) {
    Check check;
    check.id = spec.id(api);
    check.tags = {"render", api_tag, spec.multiframe() ? "multiframe" : "single", spec.group()};
    check.tags.insert(check.tags.end(), spec.tags().begin(), spec.tags().end());
    check.exclusive = false;
    check.timeout_seconds = spec.timeout_seconds();
    check.run = [api_tag, run = std::move(run)](Context& context) {
        if (ApiPolicy::is_skipped(api_tag)) {
            return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
        }
        return run(context);
    };
    suite.add(std::move(check));
}

}  // namespace lsw::suites::render
