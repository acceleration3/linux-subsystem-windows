#include "sanity/sanity.hpp"

#include "sanity/enumeration/caps_checks.hpp"
#include "sanity/enumeration/identity_checks.hpp"
#include "sanity/enumeration/memory_checks.hpp"
#include "sanity/enumeration/stability_checks.hpp"
#include "sanity/env/adapters_present.hpp"
#include "sanity/env/display_checks.hpp"
#include "sanity/env/icd_checks.hpp"
#include "sanity/env/install_checks.hpp"

namespace lsw::suites::sanity {

void add(Registry& registry) {
    Suite& suite = registry.add_suite("sanity", Policy::Parallel);
    suite.add({"sanity.env.adapters.present", {"env", "dxgi"}, false, 30, env::AdaptersPresent::run});

    suite.add({"sanity.env.install.adapter_started", {"env", "setupapi"}, false, 30,
               env::InstallChecks::adapter_started});
    suite.add({"sanity.env.install.driver_version_consistent", {"env", "setupapi", "dxgi"}, false, 30,
               env::InstallChecks::driver_version_consistent});
    suite.add({"sanity.env.install.umd_files_present_both_bitnesses", {"env", "setupapi"}, false, 30,
               env::InstallChecks::umd_files_present_both_bitnesses});
    suite.add({"sanity.env.install.signed_and_loadable", {"env", "setupapi", "dxgi"}, false, 60,
               env::InstallChecks::signed_and_loadable});
    suite.add({"sanity.env.install.no_duplicate_adapters", {"env", "setupapi", "dxgi", "kmt"}, false, 30,
               env::InstallChecks::no_duplicate_adapters});
    suite.add({"sanity.env.install.requirements_hold", {"env"}, false, 30,
               env::InstallChecks::requirements_hold});

    suite.add({"sanity.env.icd.opencl_registry_x64", {"env", "opencl", "x64-only"}, false, 60,
               env::IcdChecks::opencl_registry_x64});
    suite.add({"sanity.env.icd.opencl_registry_x86", {"env", "opencl", "x86-only", "cross-bitness"}, false, 90,
               env::IcdChecks::opencl_registry_x86});
    suite.add({"sanity.env.icd.vulkan_manifest_both", {"env", "vulkan", "cross-bitness"}, false, 90,
               env::IcdChecks::vulkan_manifest_both});
    suite.add({"sanity.env.icd.opengl_icd_both", {"env", "gl", "cross-bitness"}, false, 90,
               env::IcdChecks::opengl_icd_both});
    suite.add({"sanity.env.icd.loader_independent_of_cwd", {"env", "vulkan", "opencl", "gl"}, false, 180,
               env::IcdChecks::loader_independent_of_cwd});

    suite.add({"sanity.env.display.output_present", {"env", "dxgi", "gdi"}, false, 30,
               env::DisplayChecks::output_present});
    suite.add({"sanity.env.display.output_active_mode", {"env", "gdi"}, false, 30,
               env::DisplayChecks::output_active_mode});
    suite.add({"sanity.env.display.desktop_rect_consistent", {"env", "gdi"}, false, 30,
               env::DisplayChecks::desktop_rect_consistent});
    suite.add({"sanity.env.display.dpi_scaling_consistent", {"env", "gdi"}, false, 30,
               env::DisplayChecks::dpi_scaling_consistent});

    suite.add({"sanity.enum.identity.dxgi_vs_kmt_luid", {"enum", "dxgi", "kmt"}, false, 30,
               enumeration::IdentityChecks::dxgi_vs_kmt_luid});
    suite.add({"sanity.enum.identity.vulkan_luid_matches", {"enum", "dxgi", "vulkan"}, false, 30,
               enumeration::IdentityChecks::vulkan_luid_matches});
    suite.add({"sanity.enum.identity.opencl_luid_matches", {"enum", "dxgi", "opencl"}, false, 30,
               enumeration::IdentityChecks::opencl_luid_matches});
    suite.add({"sanity.enum.identity.gl_renderer_matches", {"enum", "dxgi", "gl"}, false, 60,
               enumeration::IdentityChecks::gl_renderer_matches});
    suite.add({"sanity.enum.identity.setupapi_matches", {"enum", "dxgi", "kmt", "setupapi"}, false, 30,
               enumeration::IdentityChecks::setupapi_matches});
    suite.add({"sanity.enum.identity.names_nonempty_printable",
               {"enum", "dxgi", "kmt", "vulkan", "opencl", "setupapi"}, false, 30,
               enumeration::IdentityChecks::names_nonempty_printable});

    suite.add({"sanity.enum.stable.repeat_calls", {"enum", "dxgi", "kmt", "vulkan", "opencl", "setupapi", "slow"},
               false, 180, enumeration::StabilityChecks::repeat_calls});
    suite.add({"sanity.enum.stable.across_processes", {"enum", "dxgi", "kmt", "vulkan", "opencl", "slow"}, false,
               180, enumeration::StabilityChecks::across_processes});
    suite.add({"sanity.enum.stable.across_bitness", {"enum", "dxgi", "kmt", "cross-bitness"}, false, 90,
               enumeration::StabilityChecks::across_bitness});
    suite.add({"sanity.enum.stable.adapter_order_matches_primary", {"enum", "dxgi", "cross-bitness"}, false, 90,
               enumeration::StabilityChecks::adapter_order_matches_primary});
    suite.add({"sanity.enum.stable.create_destroy_enumeration", {"enum", "dxgi", "kmt", "d3d11", "vulkan", "opencl"},
               false, 120, enumeration::StabilityChecks::create_destroy_enumeration});

    suite.add({"sanity.enum.memory.dedicated_vs_shared_consistent", {"enum", "dxgi", "kmt", "vulkan"}, false, 30,
               enumeration::MemoryChecks::dedicated_vs_shared_consistent});
    suite.add({"sanity.enum.memory.opencl_global_size", {"enum", "dxgi", "vulkan", "opencl"}, false, 30,
               enumeration::MemoryChecks::opencl_global_size});
    suite.add({"sanity.enum.caps.feature_level_vs_api_support", {"enum", "dxgi", "d3d11", "d3d12", "vulkan"}, false,
               60, enumeration::CapsChecks::feature_level_vs_api_support});
}

}  // namespace lsw::suites::sanity
