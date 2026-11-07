#include "sanity/sanity.hpp"

#include "sanity/data/align_checks.hpp"
#include "sanity/data/load_checks.hpp"
#include "sanity/data/sequence_checks.hpp"
#include "sanity/data/size_checks.hpp"
#include "sanity/enumeration/caps_checks.hpp"
#include "sanity/enumeration/identity_checks.hpp"
#include "sanity/enumeration/memory_checks.hpp"
#include "sanity/enumeration/stability_checks.hpp"
#include "sanity/env/adapters_present.hpp"
#include "sanity/memory/budget_checks.hpp"
#include "sanity/memory/evict_map_checks.hpp"
#include "sanity/memory/leak_checks.hpp"
#include "sanity/memory/limit_checks.hpp"
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

    suite.add({"sanity.data.size.buffer_roundtrip_small", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SizeChecks::buffer_roundtrip_small});
    suite.add({"sanity.data.size.buffer_roundtrip_boundaries", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, data::SizeChecks::buffer_roundtrip_boundaries});
    suite.add({"sanity.data.size.buffer_roundtrip_hundreds_mib", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, data::SizeChecks::buffer_roundtrip_hundreds_mib});
    suite.add({"sanity.data.size.texture_upload_sizes", {"data", "d3d11"}, false, 180, data::SizeChecks::texture_upload_sizes});
    suite.add({"sanity.data.size.texture_full_screen_upload", {"data", "d3d11"}, false, 60, data::SizeChecks::texture_full_screen_upload});
    suite.add({"sanity.data.size.readback_every_size", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, data::SizeChecks::readback_every_size});
    suite.add({"sanity.data.align.offset_every_byte", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, data::AlignChecks::offset_every_byte});
    suite.add({"sanity.data.align.unaligned_copy_regions", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, data::AlignChecks::unaligned_copy_regions});
    suite.add({"sanity.data.align.mapped_pointer_alignment", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 60, data::AlignChecks::mapped_pointer_alignment});
    suite.add({"sanity.data.align.row_pitch_variants", {"data", "d3d11"}, false, 60, data::AlignChecks::row_pitch_variants});
    suite.add({"sanity.data.align.sub_region_updates", {"data", "d3d11"}, false, 60, data::AlignChecks::sub_region_updates});
    suite.add({"sanity.data.seq.back_to_back_10000", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::back_to_back_10000});
    suite.add({"sanity.data.seq.interleaved_small_large", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 180, data::SequenceChecks::interleaved_small_large});
    suite.add({"sanity.data.seq.overwrite_ordering", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 60, data::SequenceChecks::overwrite_ordering});
    suite.add({"sanity.data.seq.read_after_write_same_context", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::read_after_write_same_context});
    suite.add({"sanity.data.seq.many_contexts", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::many_contexts});
    suite.add({"sanity.data.seq.many_devices_one_process", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::many_devices_one_process});
    suite.add({"sanity.data.seq.mixed_api_chain", {"data", "interop", "d3d11", "d3d12", "vulkan", "opencl"}, false, 60, data::SequenceChecks::mixed_api_chain});
    suite.add({"sanity.data.load.parallel_processes", {"data", "slow"}, false, 180, data::LoadChecks::parallel_processes});
    suite.add({"sanity.data.load.parallel_with_render_load", {"data", "slow"}, false, 180, data::LoadChecks::parallel_with_render_load});
    suite.add({"sanity.data.load.memory_pressure_host", {"data", "exclusive", "slow"}, true, 180, data::LoadChecks::memory_pressure_host});
    suite.add({"sanity.data.load.memory_pressure_guest", {"data", "exclusive", "slow"}, true, 180, data::LoadChecks::memory_pressure_guest});
    suite.add({"sanity.data.load.sustained_30s", {"data", "exclusive", "slow"}, true, 180, data::LoadChecks::sustained_30s});
    suite.add({"sanity.mem.limit.allocate_up_to_reported", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, memory::LimitChecks::allocate_up_to_reported});
    suite.add({"sanity.mem.limit.fail_beyond_reported", {"mem", "d3d12", "vulkan", "opencl"}, false, 120, memory::LimitChecks::fail_beyond_reported});
    suite.add({"sanity.mem.limit.single_allocation_max", {"mem", "vulkan", "opencl"}, false, 180, memory::LimitChecks::single_allocation_max});
    suite.add({"sanity.mem.limit.zero_size", {"mem", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl"}, false, 120, memory::LimitChecks::zero_size});
    suite.add({"sanity.mem.limit.after_failure_usable", {"mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, memory::LimitChecks::after_failure_usable});
    suite.add({"sanity.mem.reuse.freed_becomes_usable", {"mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, memory::LimitChecks::freed_becomes_usable});
    suite.add({"sanity.mem.reuse.fragmented_pattern", {"mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, memory::LimitChecks::fragmented_pattern});
    suite.add({"sanity.mem.budget.dxgi_moves_with_alloc", {"mem", "dxgi", "d3d11", "d3d12"}, false, 120, memory::BudgetChecks::dxgi_moves_with_alloc});
    suite.add({"sanity.mem.budget.vulkan_budget_moves", {"mem", "vulkan"}, false, 120, memory::BudgetChecks::vulkan_budget_moves});
    suite.add({"sanity.mem.budget.reservation_honoured", {"mem", "dxgi"}, false, 60, memory::BudgetChecks::reservation_honoured});
    suite.add({"sanity.mem.budget.opencl_free_memory", {"mem", "opencl"}, false, 120, memory::BudgetChecks::opencl_free_memory});
    suite.add({"sanity.mem.leak.buffers_all_apis", {"mem", "leak", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl", "slow"}, false, 300, memory::LeakChecks::buffers_all_apis});
    suite.add({"sanity.mem.leak.textures_all_apis", {"mem", "leak", "d3d9", "d3d11", "d3d12", "gl", "slow"}, false, 300, memory::LeakChecks::textures_all_apis});
    suite.add({"sanity.mem.leak.views_and_samplers", {"mem", "leak", "d3d11", "d3d12", "gl", "slow"}, false, 300, memory::LeakChecks::views_and_samplers});
    suite.add({"sanity.mem.leak.shaders_pipelines", {"mem", "leak", "d3d11", "opencl", "gl", "slow"}, false, 300, memory::LeakChecks::shaders_pipelines});
    suite.add({"sanity.mem.leak.contexts_devices", {"mem", "leak", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl", "slow"}, false, 300, memory::LeakChecks::contexts_devices});
    suite.add({"sanity.mem.leak.sync_objects", {"mem", "leak", "d3d11", "d3d12", "gl", "slow"}, false, 300, memory::LeakChecks::sync_objects});
    suite.add({"sanity.mem.leak.shared_resources", {"mem", "leak", "d3d11", "interop", "slow"}, false, 300, memory::LeakChecks::shared_resources});
    suite.add({"sanity.mem.leak.process_exit_releases", {"mem", "leak", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, memory::LeakChecks::process_exit_releases});
    suite.add({"sanity.mem.evict.contents_survive_pressure", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, memory::EvictMapChecks::contents_survive_pressure});
    suite.add({"sanity.mem.evict.make_resident_semantics", {"mem", "d3d12"}, false, 180, memory::EvictMapChecks::make_resident_semantics});
    suite.add({"sanity.mem.map.pointer_valid_until_unmap", {"mem", "vulkan", "d3d12", "gl"}, false, 180, memory::EvictMapChecks::pointer_valid_until_unmap});
    suite.add({"sanity.mem.map.unmap_flushes", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "gl"}, false, 120, memory::EvictMapChecks::unmap_flushes});
    suite.add({"sanity.mem.map.exit_with_live_mappings", {"mem", "d3d12", "vulkan", "opencl", "d3d11"}, false, 300, memory::EvictMapChecks::exit_with_live_mappings});
    suite.add({"sanity.mem.map.large_map_many", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, memory::EvictMapChecks::large_map_many});
}

}  // namespace lsw::suites::sanity
