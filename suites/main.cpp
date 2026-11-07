#include "core/exe_identity.hpp"
#include "core/options.hpp"
#include "core/registry.hpp"
#include "core/runner.hpp"
#include "core/single_check.hpp"
#include "sanity/sanity.hpp"
#include "selftest/selftest.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    using namespace lsw::suites;
    std::string error;
    const auto options = Options::parse(argc, argv, error);
    if (!options) {
        std::fprintf(stderr, "%s: %s\n%s", ExeIdentity::name().c_str(), error.c_str(),
                     Options::usage().c_str());
        return 2;
    }
    if (options->help) {
        std::fputs(Options::usage().c_str(), stdout);
        return 0;
    }
    if (!options->skip_apis.empty()) {
        _putenv_s("LSW_SUITES_SKIP_APIS", options->skip_apis.c_str());
        SetEnvironmentVariableA("LSW_SUITES_SKIP_APIS", options->skip_apis.c_str());
    }
    Registry registry;
    sanity::add(registry);
    selftest::add(registry);
    if (!options->run_one.empty()) {
        return SingleCheck(registry, options->artifacts).run(options->run_one);
    }
    Runner runner(registry, *options);
    return options->list ? runner.list() : runner.run();
}
