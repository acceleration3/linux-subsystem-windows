#include "sanity/sanity.hpp"

#include "support/api_policy.hpp"

#include <functional>

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
#include "sanity/d3d11/fl_checks.hpp"
#include "sanity/d3d11/res_checks.hpp"
#include "sanity/d3d11/map_checks.hpp"
#include "sanity/d3d11/transfer_checks.hpp"
#include "sanity/d3d11/render_checks.hpp"
#include "sanity/d3d11/compute_checks.hpp"
#include "sanity/d3d11/query_checks.hpp"
#include "sanity/d3d11/deferred_checks.hpp"
#include "sanity/d3d11/share_checks.hpp"
#include "sanity/d3d11/tiled_checks.hpp"
#include "sanity/d3d11/err_checks.hpp"
#include "sanity/env/display_checks.hpp"
#include "sanity/env/icd_checks.hpp"
#include "sanity/env/install_checks.hpp"

namespace lsw::suites::sanity {

namespace {

void guarded(Suite& suite, Check check) {
    std::vector<std::string> apis;
    for (const std::string& tag : check.tags) {
        for (const std::string& known : ApiPolicy::known()) {
            if (tag == known) {
                apis.push_back(tag);
            }
        }
    }
    std::function<Outcome(Context&)> inner = check.run;
    check.run = [apis, inner](Context& context) {
        std::vector<std::string> excluded;
        for (const std::string& api : apis) {
            if (ApiPolicy::is_skipped(api)) {
                excluded.push_back(api);
            }
        }
        if (!apis.empty() && excluded.size() == apis.size()) {
            return Context::skip("api excluded by LSW_SUITES_SKIP_APIS");
        }
        for (const std::string& api : excluded) {
            context.note("api excluded by LSW_SUITES_SKIP_APIS: " + api);
        }
        return inner(context);
    };
    suite.add(std::move(check));
}

}  // namespace

void add(Registry& registry) {
    Suite& suite = registry.add_suite("sanity", Policy::Parallel);
    guarded(suite, {"sanity.env.adapters.present", {"env", "dxgi"}, false, 30, env::AdaptersPresent::run});

    guarded(suite, {"sanity.env.install.adapter_started", {"env", "setupapi"}, false, 30,
               env::InstallChecks::adapter_started});
    guarded(suite, {"sanity.env.install.driver_version_consistent", {"env", "setupapi", "dxgi"}, false, 30,
               env::InstallChecks::driver_version_consistent});
    guarded(suite, {"sanity.env.install.umd_files_present_both_bitnesses", {"env", "setupapi"}, false, 30,
               env::InstallChecks::umd_files_present_both_bitnesses});
    guarded(suite, {"sanity.env.install.signed_and_loadable", {"env", "setupapi", "dxgi"}, false, 60,
               env::InstallChecks::signed_and_loadable});
    guarded(suite, {"sanity.env.install.no_duplicate_adapters", {"env", "setupapi", "dxgi", "kmt"}, false, 30,
               env::InstallChecks::no_duplicate_adapters});
    guarded(suite, {"sanity.env.install.requirements_hold", {"env"}, false, 30,
               env::InstallChecks::requirements_hold});

    guarded(suite, {"sanity.env.icd.opencl_registry_x64", {"env", "opencl", "x64-only"}, false, 60,
               env::IcdChecks::opencl_registry_x64});
    guarded(suite, {"sanity.env.icd.opencl_registry_x86", {"env", "opencl", "x86-only", "cross-bitness"}, false, 90,
               env::IcdChecks::opencl_registry_x86});
    guarded(suite, {"sanity.env.icd.vulkan_manifest_both", {"env", "vulkan", "cross-bitness"}, false, 90,
               env::IcdChecks::vulkan_manifest_both});
    guarded(suite, {"sanity.env.icd.opengl_icd_both", {"env", "gl", "cross-bitness"}, false, 90,
               env::IcdChecks::opengl_icd_both});
    guarded(suite, {"sanity.env.icd.loader_independent_of_cwd", {"env", "vulkan", "opencl", "gl"}, false, 180,
               env::IcdChecks::loader_independent_of_cwd});

    guarded(suite, {"sanity.env.display.output_present", {"env", "dxgi", "gdi"}, false, 30,
               env::DisplayChecks::output_present});
    guarded(suite, {"sanity.env.display.output_active_mode", {"env", "gdi"}, false, 30,
               env::DisplayChecks::output_active_mode});
    guarded(suite, {"sanity.env.display.desktop_rect_consistent", {"env", "gdi"}, false, 30,
               env::DisplayChecks::desktop_rect_consistent});
    guarded(suite, {"sanity.env.display.dpi_scaling_consistent", {"env", "gdi"}, false, 30,
               env::DisplayChecks::dpi_scaling_consistent});

    guarded(suite, {"sanity.enum.identity.dxgi_vs_kmt_luid", {"enum", "dxgi", "kmt"}, false, 30,
               enumeration::IdentityChecks::dxgi_vs_kmt_luid});
    guarded(suite, {"sanity.enum.identity.vulkan_luid_matches", {"enum", "dxgi", "vulkan"}, false, 30,
               enumeration::IdentityChecks::vulkan_luid_matches});
    guarded(suite, {"sanity.enum.identity.opencl_luid_matches", {"enum", "dxgi", "opencl"}, false, 30,
               enumeration::IdentityChecks::opencl_luid_matches});
    guarded(suite, {"sanity.enum.identity.gl_renderer_matches", {"enum", "dxgi", "gl"}, false, 60,
               enumeration::IdentityChecks::gl_renderer_matches});
    guarded(suite, {"sanity.enum.identity.setupapi_matches", {"enum", "dxgi", "kmt", "setupapi"}, false, 30,
               enumeration::IdentityChecks::setupapi_matches});
    guarded(suite, {"sanity.enum.identity.names_nonempty_printable",
               {"enum", "dxgi", "kmt", "vulkan", "opencl", "setupapi"}, false, 30,
               enumeration::IdentityChecks::names_nonempty_printable});

    guarded(suite, {"sanity.enum.stable.repeat_calls", {"enum", "dxgi", "kmt", "vulkan", "opencl", "setupapi", "slow"},
               false, 180, enumeration::StabilityChecks::repeat_calls});
    guarded(suite, {"sanity.enum.stable.across_processes", {"enum", "dxgi", "kmt", "vulkan", "opencl", "slow"}, false,
               180, enumeration::StabilityChecks::across_processes});
    guarded(suite, {"sanity.enum.stable.across_bitness", {"enum", "dxgi", "kmt", "cross-bitness"}, false, 90,
               enumeration::StabilityChecks::across_bitness});
    guarded(suite, {"sanity.enum.stable.adapter_order_matches_primary", {"enum", "dxgi", "cross-bitness"}, false, 90,
               enumeration::StabilityChecks::adapter_order_matches_primary});
    guarded(suite, {"sanity.enum.stable.create_destroy_enumeration", {"enum", "dxgi", "kmt", "d3d11", "vulkan", "opencl"},
               false, 120, enumeration::StabilityChecks::create_destroy_enumeration});

    guarded(suite, {"sanity.enum.memory.dedicated_vs_shared_consistent", {"enum", "dxgi", "kmt", "vulkan"}, false, 30,
               enumeration::MemoryChecks::dedicated_vs_shared_consistent});
    guarded(suite, {"sanity.enum.memory.opencl_global_size", {"enum", "dxgi", "vulkan", "opencl"}, false, 30,
               enumeration::MemoryChecks::opencl_global_size});
    guarded(suite, {"sanity.enum.caps.feature_level_vs_api_support", {"enum", "dxgi", "d3d11", "d3d12", "vulkan"}, false,
               60, enumeration::CapsChecks::feature_level_vs_api_support});

    guarded(suite, {"sanity.data.size.buffer_roundtrip_small", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SizeChecks::buffer_roundtrip_small});
    guarded(suite, {"sanity.data.size.buffer_roundtrip_boundaries", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, data::SizeChecks::buffer_roundtrip_boundaries});
    guarded(suite, {"sanity.data.size.buffer_roundtrip_hundreds_mib", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow", "exclusive"}, true, 300, data::SizeChecks::buffer_roundtrip_hundreds_mib});
    guarded(suite, {"sanity.data.size.texture_upload_sizes", {"data", "d3d11"}, false, 180, data::SizeChecks::texture_upload_sizes});
    guarded(suite, {"sanity.data.size.texture_full_screen_upload", {"data", "d3d11"}, false, 60, data::SizeChecks::texture_full_screen_upload});
    guarded(suite, {"sanity.data.size.readback_every_size", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, data::SizeChecks::readback_every_size});
    guarded(suite, {"sanity.data.align.offset_every_byte", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, data::AlignChecks::offset_every_byte});
    guarded(suite, {"sanity.data.align.unaligned_copy_regions", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, data::AlignChecks::unaligned_copy_regions});
    guarded(suite, {"sanity.data.align.mapped_pointer_alignment", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 60, data::AlignChecks::mapped_pointer_alignment});
    guarded(suite, {"sanity.data.align.row_pitch_variants", {"data", "d3d11"}, false, 60, data::AlignChecks::row_pitch_variants});
    guarded(suite, {"sanity.data.align.sub_region_updates", {"data", "d3d11"}, false, 60, data::AlignChecks::sub_region_updates});
    guarded(suite, {"sanity.data.seq.back_to_back_10000", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::back_to_back_10000});
    guarded(suite, {"sanity.data.seq.interleaved_small_large", {"data", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 180, data::SequenceChecks::interleaved_small_large});
    guarded(suite, {"sanity.data.seq.overwrite_ordering", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 60, data::SequenceChecks::overwrite_ordering});
    guarded(suite, {"sanity.data.seq.read_after_write_same_context", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::read_after_write_same_context});
    guarded(suite, {"sanity.data.seq.many_contexts", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::many_contexts});
    guarded(suite, {"sanity.data.seq.many_devices_one_process", {"data", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, data::SequenceChecks::many_devices_one_process});
    guarded(suite, {"sanity.data.seq.mixed_api_chain", {"data", "interop", "d3d11", "d3d12", "vulkan", "opencl"}, false, 60, data::SequenceChecks::mixed_api_chain});
    guarded(suite, {"sanity.data.load.parallel_processes", {"data", "slow"}, false, 180, data::LoadChecks::parallel_processes});
    guarded(suite, {"sanity.data.load.parallel_with_render_load", {"data", "slow"}, false, 180, data::LoadChecks::parallel_with_render_load});
    guarded(suite, {"sanity.data.load.memory_pressure_host", {"data", "exclusive", "slow"}, true, 180, data::LoadChecks::memory_pressure_host});
    guarded(suite, {"sanity.data.load.memory_pressure_guest", {"data", "exclusive", "slow"}, true, 180, data::LoadChecks::memory_pressure_guest});
    guarded(suite, {"sanity.data.load.sustained_30s", {"data", "exclusive", "slow"}, true, 180, data::LoadChecks::sustained_30s});
    guarded(suite, {"sanity.mem.limit.allocate_up_to_reported", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "slow", "exclusive"}, true, 300, memory::LimitChecks::allocate_up_to_reported});
    guarded(suite, {"sanity.mem.limit.fail_beyond_reported", {"mem", "d3d12", "vulkan", "opencl", "exclusive"}, true, 120, memory::LimitChecks::fail_beyond_reported});
    guarded(suite, {"sanity.mem.limit.single_allocation_max", {"mem", "vulkan", "opencl"}, false, 180, memory::LimitChecks::single_allocation_max});
    guarded(suite, {"sanity.mem.limit.zero_size", {"mem", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl"}, false, 120, memory::LimitChecks::zero_size});
    guarded(suite, {"sanity.mem.limit.after_failure_usable", {"mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, memory::LimitChecks::after_failure_usable});
    guarded(suite, {"sanity.mem.reuse.freed_becomes_usable", {"mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, memory::LimitChecks::freed_becomes_usable});
    guarded(suite, {"sanity.mem.reuse.fragmented_pattern", {"mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, memory::LimitChecks::fragmented_pattern});
    guarded(suite, {"sanity.mem.budget.dxgi_moves_with_alloc", {"mem", "dxgi", "d3d11", "d3d12"}, false, 120, memory::BudgetChecks::dxgi_moves_with_alloc});
    guarded(suite, {"sanity.mem.budget.vulkan_budget_moves", {"mem", "vulkan"}, false, 120, memory::BudgetChecks::vulkan_budget_moves});
    guarded(suite, {"sanity.mem.budget.reservation_honoured", {"mem", "dxgi"}, false, 60, memory::BudgetChecks::reservation_honoured});
    guarded(suite, {"sanity.mem.budget.opencl_free_memory", {"mem", "opencl"}, false, 120, memory::BudgetChecks::opencl_free_memory});
    guarded(suite, {"sanity.mem.leak.buffers_all_apis", {"mem", "leak", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl", "slow"}, false, 300, memory::LeakChecks::buffers_all_apis});
    guarded(suite, {"sanity.mem.leak.textures_all_apis", {"mem", "leak", "d3d9", "d3d11", "d3d12", "gl", "slow"}, false, 300, memory::LeakChecks::textures_all_apis});
    guarded(suite, {"sanity.mem.leak.views_and_samplers", {"mem", "leak", "d3d11", "d3d12", "gl", "slow"}, false, 300, memory::LeakChecks::views_and_samplers});
    guarded(suite, {"sanity.mem.leak.shaders_pipelines", {"mem", "leak", "d3d11", "opencl", "gl", "slow"}, false, 300, memory::LeakChecks::shaders_pipelines});
    guarded(suite, {"sanity.mem.leak.contexts_devices", {"mem", "leak", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "opencl", "slow"}, false, 300, memory::LeakChecks::contexts_devices});
    guarded(suite, {"sanity.mem.leak.sync_objects", {"mem", "leak", "d3d11", "d3d12", "gl", "slow"}, false, 300, memory::LeakChecks::sync_objects});
    guarded(suite, {"sanity.mem.leak.shared_resources", {"mem", "leak", "d3d11", "interop", "slow"}, false, 300, memory::LeakChecks::shared_resources});
    guarded(suite, {"sanity.mem.leak.process_exit_releases", {"mem", "leak", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, memory::LeakChecks::process_exit_releases});
    guarded(suite, {"sanity.mem.evict.contents_survive_pressure", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "slow", "exclusive"}, true, 300, memory::EvictMapChecks::contents_survive_pressure});
    guarded(suite, {"sanity.mem.evict.make_resident_semantics", {"mem", "d3d12", "exclusive"}, true, 180, memory::EvictMapChecks::make_resident_semantics});
    guarded(suite, {"sanity.mem.map.pointer_valid_until_unmap", {"mem", "vulkan", "d3d12", "gl"}, false, 180, memory::EvictMapChecks::pointer_valid_until_unmap});
    guarded(suite, {"sanity.mem.map.unmap_flushes", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "gl"}, false, 120, memory::EvictMapChecks::unmap_flushes});
    guarded(suite, {"sanity.mem.map.exit_with_live_mappings", {"mem", "d3d12", "vulkan", "opencl", "d3d11"}, false, 300, memory::EvictMapChecks::exit_with_live_mappings});
    guarded(suite, {"sanity.mem.map.large_map_many", {"mem", "d3d11", "d3d12", "vulkan", "opencl", "slow", "exclusive"}, true, 300, memory::EvictMapChecks::large_map_many});
    guarded(suite, {"sanity.d3d11.fl.requested_levels", {"d3d11"}, false, 60, d3d11::FlChecks::requested_levels});
    guarded(suite, {"sanity.d3d11.fl.checkfeaturesupport_options", {"d3d11"}, false, 60, d3d11::FlChecks::checkfeaturesupport_options});
    guarded(suite, {"sanity.d3d11.fl.format_support_vs_creation", {"d3d11", "formats"}, false, 120, d3d11::FlChecks::format_support_vs_creation});
    guarded(suite, {"sanity.d3d11.fl.msaa_quality_vs_use", {"d3d11", "msaa"}, false, 120, d3d11::FlChecks::msaa_quality_vs_use});
    guarded(suite, {"sanity.d3d11.fl.limits_at_level", {"d3d11"}, false, 120, d3d11::FlChecks::limits_at_level});
    guarded(suite, {"sanity.d3d11.res.usage_bind_cpu_matrix", {"d3d11"}, false, 120, d3d11::ResChecks::usage_bind_cpu_matrix});
    guarded(suite, {"sanity.d3d11.res.immutable_initial", {"d3d11"}, false, 60, d3d11::ResChecks::immutable_initial});
    guarded(suite, {"sanity.d3d11.res.views_matrix", {"d3d11"}, false, 120, d3d11::ResChecks::views_matrix});
    guarded(suite, {"sanity.d3d11.res.sizes_extremes", {"d3d11"}, false, 120, d3d11::ResChecks::sizes_extremes});
    guarded(suite, {"sanity.d3d11.res.exhaustion_and_reclaim", {"d3d11", "mem", "slow", "exclusive"}, true, 300, d3d11::ResChecks::exhaustion_and_reclaim});
    guarded(suite, {"sanity.d3d11.res.exit_reclaim", {"d3d11", "mem", "slow"}, false, 300, d3d11::ResChecks::exit_reclaim});
    guarded(suite, {"sanity.d3d11.map.read_last_write", {"d3d11"}, false, 60, d3d11::MapChecks::read_last_write});
    guarded(suite, {"sanity.d3d11.map.discard_old_draws", {"d3d11"}, false, 60, d3d11::MapChecks::discard_old_draws});
    guarded(suite, {"sanity.d3d11.map.nooverwrite_inflight", {"d3d11"}, false, 60, d3d11::MapChecks::nooverwrite_inflight});
    guarded(suite, {"sanity.d3d11.map.do_not_wait", {"d3d11"}, false, 120, d3d11::MapChecks::do_not_wait});
    guarded(suite, {"sanity.d3d11.map.pitches", {"d3d11"}, false, 60, d3d11::MapChecks::pitches});
    guarded(suite, {"sanity.d3d11.map.errors", {"d3d11"}, false, 60, d3d11::MapChecks::errors});
    guarded(suite, {"sanity.d3d11.map.persistent_across_frames", {"d3d11", "dxgi", "present"}, false, 60, d3d11::MapChecks::persistent_across_frames});
    guarded(suite, {"sanity.d3d11.update.boxes_pitches", {"d3d11"}, false, 60, d3d11::TransferChecks::update_boxes_pitches});
    guarded(suite, {"sanity.d3d11.update.bc_and_formats", {"d3d11", "formats"}, false, 60, d3d11::TransferChecks::update_bc_and_formats});
    guarded(suite, {"sanity.d3d11.update.copy_flags", {"d3d11"}, false, 60, d3d11::TransferChecks::update_copy_flags});
    guarded(suite, {"sanity.d3d11.update.deferred_ctx", {"d3d11", "deferred"}, false, 60, d3d11::TransferChecks::update_deferred_ctx});
    guarded(suite, {"sanity.d3d11.copy.variants", {"d3d11"}, false, 60, d3d11::TransferChecks::copy_variants});
    guarded(suite, {"sanity.d3d11.copy.typeless_cross_format", {"d3d11", "formats"}, false, 60, d3d11::TransferChecks::copy_typeless_cross_format});
    guarded(suite, {"sanity.d3d11.copy.structure_copy", {"d3d11", "compute"}, false, 60, d3d11::TransferChecks::copy_structure_copy});
    guarded(suite, {"sanity.d3d11.copy.resolve", {"d3d11", "msaa"}, false, 60, d3d11::TransferChecks::copy_resolve});
    guarded(suite, {"sanity.d3d11.copy.generatemips", {"d3d11"}, false, 60, d3d11::TransferChecks::copy_generatemips});
    guarded(suite, {"sanity.d3d11.copy.clearview_discard", {"d3d11"}, false, 60, d3d11::TransferChecks::copy_clearview_discard});
    guarded(suite, {"sanity.d3d11.render.basic_stages", {"d3d11", "streamout"}, false, 60, d3d11::RenderChecks::basic_stages});
    guarded(suite, {"sanity.d3d11.render.states", {"d3d11"}, false, 60, d3d11::RenderChecks::states});
    guarded(suite, {"sanity.d3d11.render.msaa", {"d3d11", "msaa"}, false, 60, d3d11::RenderChecks::msaa});
    guarded(suite, {"sanity.d3d11.render.tessellation", {"d3d11", "tessellation"}, false, 60, d3d11::RenderChecks::tessellation});
    guarded(suite, {"sanity.d3d11.render.geometry_streamout", {"d3d11", "streamout"}, false, 60, d3d11::RenderChecks::geometry_streamout});
    guarded(suite, {"sanity.d3d11.render.format_sweep", {"d3d11", "formats"}, false, 90, d3d11::RenderChecks::format_sweep});
    guarded(suite, {"sanity.d3d11.render.depth_stencil_formats", {"d3d11", "formats"}, false, 60, d3d11::RenderChecks::depth_stencil_formats});
    guarded(suite, {"sanity.d3d11.render.multi_rt_viewports", {"d3d11"}, false, 60, d3d11::RenderChecks::multi_rt_viewports});
    guarded(suite, {"sanity.d3d11.render.resource_hazards", {"d3d11"}, false, 60, d3d11::RenderChecks::resource_hazards});
    guarded(suite, {"sanity.d3d11.compute.uav_basic", {"d3d11", "compute"}, false, 90, d3d11::ComputeChecks::uav_basic});
    guarded(suite, {"sanity.d3d11.compute.atomics", {"d3d11", "compute"}, false, 90, d3d11::ComputeChecks::atomics});
    guarded(suite, {"sanity.d3d11.compute.append_consume", {"d3d11", "compute"}, false, 90, d3d11::ComputeChecks::append_consume});
    guarded(suite, {"sanity.d3d11.compute.typed_uav_load", {"d3d11", "compute", "formats"}, false, 90, d3d11::ComputeChecks::typed_uav_load});
    guarded(suite, {"sanity.d3d11.compute.barriers_sharing", {"d3d11", "compute"}, false, 90, d3d11::ComputeChecks::barriers_sharing});
    guarded(suite, {"sanity.d3d11.compute.cs_on_downlevel", {"d3d11", "compute"}, false, 60, d3d11::ComputeChecks::cs_on_downlevel});
    guarded(suite, {"sanity.d3d11.query.occlusion_counts", {"d3d11"}, false, 60, d3d11::QueryChecks::occlusion_counts});
    guarded(suite, {"sanity.d3d11.query.timestamp_disjoint", {"d3d11", "compute"}, false, 90, d3d11::QueryChecks::timestamp_disjoint});
    guarded(suite, {"sanity.d3d11.query.pipeline_stats", {"d3d11"}, false, 60, d3d11::QueryChecks::pipeline_stats});
    guarded(suite, {"sanity.d3d11.query.event_and_predication", {"d3d11"}, false, 90, d3d11::QueryChecks::event_and_predication});
    guarded(suite, {"sanity.d3d11.deferred.equivalence", {"d3d11", "deferred"}, false, 60, d3d11::DeferredChecks::equivalence});
    guarded(suite, {"sanity.d3d11.deferred.maps", {"d3d11", "deferred"}, false, 60, d3d11::DeferredChecks::maps});
    guarded(suite, {"sanity.d3d11.deferred.list_reuse", {"d3d11", "deferred"}, false, 60, d3d11::DeferredChecks::list_reuse});
    guarded(suite, {"sanity.d3d11.deferred.clearstate", {"d3d11", "deferred"}, false, 60, d3d11::DeferredChecks::clearstate});
    guarded(suite, {"sanity.d3d11.mt.free_threaded", {"d3d11"}, false, 90, d3d11::DeferredChecks::free_threaded});
    guarded(suite, {"sanity.d3d11.mt.multithread_protect", {"d3d11"}, false, 90, d3d11::DeferredChecks::multithread_protect});
    guarded(suite, {"sanity.d3d11.mt.two_devices", {"d3d11"}, false, 90, d3d11::DeferredChecks::two_devices});
    guarded(suite, {"sanity.d3d11.share.legacy_handle", {"d3d11", "interop"}, false, 60, d3d11::ShareChecks::legacy_handle});
    guarded(suite, {"sanity.d3d11.share.nt_handle", {"d3d11", "interop"}, false, 120, d3d11::ShareChecks::nt_handle});
    guarded(suite, {"sanity.d3d11.share.keyed_mutex", {"d3d11", "interop"}, false, 180, d3d11::ShareChecks::keyed_mutex});
    guarded(suite, {"sanity.d3d11.share.fence", {"d3d11", "interop"}, false, 90, d3d11::ShareChecks::fence});
    guarded(suite, {"sanity.d3d11.tiled.caps_driven", {"d3d11"}, false, 90, d3d11::TiledChecks::caps_driven});
    guarded(suite, {"sanity.d3d11.ifaces.d3d11_1", {"d3d11"}, false, 90, d3d11::TiledChecks::d3d11_1});
    guarded(suite, {"sanity.d3d11.ifaces.d3d11_2_3_4", {"d3d11"}, false, 90, d3d11::TiledChecks::d3d11_2_3_4});
    guarded(suite, {"sanity.d3d11.ifaces.on12", {"d3d11", "d3d12", "interop"}, false, 90, d3d11::TiledChecks::on12});
    guarded(suite, {"sanity.d3d11.err.bad_parameters", {"d3d11"}, false, 60, d3d11::ErrChecks::bad_parameters});
    guarded(suite, {"sanity.d3d11.err.device_removed", {"d3d11", "exclusive"}, true, 120, d3d11::ErrChecks::device_removed});
    guarded(suite, {"sanity.d3d11.err.stress_destroy", {"d3d11", "mem", "leak", "slow"}, false, 300, d3d11::ErrChecks::stress_destroy});
}

}  // namespace lsw::suites::sanity
