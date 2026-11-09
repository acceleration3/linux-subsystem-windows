#include "core/exe_identity.hpp"
#include "core/options.hpp"
#include "core/registry.hpp"
#include "core/runner.hpp"
#include "core/single_check.hpp"
#include "render/render.hpp"
#include "sanity/sanity.hpp"
#include "selftest/selftest.hpp"
#include "support/target_adapter.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    using namespace lsw::suites;
    using SetContext = BOOL(WINAPI*)(HANDLE);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto set_context = reinterpret_cast<SetContext>(reinterpret_cast<void*>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")));
        if (set_context != nullptr) {
            set_context(reinterpret_cast<HANDLE>(-4));
        }
    }
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
    if (options->list_adapters) {
        return TargetAdapter::list();
    }
    if (!options->adapter.empty()) {
        _putenv_s("LSW_SUITES_ADAPTER", options->adapter.c_str());
        SetEnvironmentVariableA("LSW_SUITES_ADAPTER", options->adapter.c_str());
    }
    if (options->run_one.empty() && !options->list && std::getenv("LSW_SUITES_DEV_ANY") == nullptr) {
        AvAdapter adapter;
        std::size_t index = 0;
        std::string adapter_error;
        if (TargetAdapter::resolve(TargetAdapter::spec(), adapter, index, adapter_error)) {
            TargetAdapter::export_identity(adapter, index);
        } else if (!options->adapter.empty()) {
            std::fprintf(stderr, "%s: --adapter: %s\n", ExeIdentity::name().c_str(), adapter_error.c_str());
            return 2;
        }
    }
    if (!options->skip_apis.empty()) {
        _putenv_s("LSW_SUITES_SKIP_APIS", options->skip_apis.c_str());
        SetEnvironmentVariableA("LSW_SUITES_SKIP_APIS", options->skip_apis.c_str());
    }
    if (!options->refs.empty()) {
        const std::string refs = options->refs.string();
        _putenv_s("LSW_SUITES_REFS", refs.c_str());
        SetEnvironmentVariableA("LSW_SUITES_REFS", refs.c_str());
    }
    if (options->device_under_test != "av") {
        _putenv_s("LSW_RENDER_DUT", options->device_under_test.c_str());
        SetEnvironmentVariableA("LSW_RENDER_DUT", options->device_under_test.c_str());
    }
    if (options->dump_captures) {
        _putenv_s("LSW_RENDER_DUMP", "1");
        SetEnvironmentVariableA("LSW_RENDER_DUMP", "1");
    }
    Registry registry;
    sanity::add(registry);
    render::add(registry);
    selftest::add(registry);
    if (const std::vector<std::string> problems = registry.problems(); !problems.empty()) {
        for (const std::string& problem : problems) {
            std::fprintf(stderr, "%s: registration error: %s\n", ExeIdentity::name().c_str(), problem.c_str());
        }
        return 2;
    }
    if (!options->run_one.empty()) {
        return SingleCheck(registry, options->artifacts).run(options->run_one);
    }
    Runner runner(registry, *options);
    return options->list ? runner.list() : runner.run();
}
