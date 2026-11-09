#include "sanity/sanity.hpp"

#include "support/api_policy.hpp"

#include <algorithm>
#include <cstdlib>
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
#include "sanity/d3d10/create_checks.hpp"
#include "sanity/d3d10/render_checks.hpp"
#include "sanity/d3d10/dxgi_checks.hpp"
#include "sanity/d3d10/query_checks.hpp"
#include "sanity/d3d9/caps_checks.hpp"
#include "sanity/d3d9/res_checks.hpp"
#include "sanity/d3d9/lock_checks.hpp"
#include "sanity/d3d9/blt_checks.hpp"
#include "sanity/d3d9/render_checks.hpp"
#include "sanity/d3d9/query_checks.hpp"
#include "sanity/d3d9/present_checks.hpp"
#include "sanity/d3d9/lost_checks.hpp"
#include "sanity/d3d9/share_checks.hpp"
#include "sanity/d3d9/fail_checks.hpp"
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
#include "sanity/d3d12/dev_checks.hpp"
#include "sanity/d3d12/heap_checks.hpp"
#include "sanity/d3d12/map_checks.hpp"
#include "sanity/d3d12/copy_checks.hpp"
#include "sanity/d3d12/queue_checks.hpp"
#include "sanity/d3d12/fence_checks.hpp"
#include "sanity/d3d12/barrier_checks.hpp"
#include "sanity/d3d12/desc_checks.hpp"
#include "sanity/d3d12/pso_checks.hpp"
#include "sanity/d3d12/draw_checks.hpp"
#include "sanity/d3d12/query_checks.hpp"
#include "sanity/d3d12/share_checks.hpp"
#include "sanity/d3d12/resid_checks.hpp"
#include "sanity/d3d12/opt_checks.hpp"
#include "sanity/d3d12/life_checks.hpp"
#include "sanity/dxgi/enum_checks.hpp"
#include "sanity/dxgi/swap_checks.hpp"
#include "sanity/dxgi/mem_checks.hpp"
#include "sanity/cl/platform_checks.hpp"
#include "sanity/cl/device_checks.hpp"
#include "sanity/cl/mem_checks.hpp"
#include "sanity/cl/transfer_checks.hpp"
#include "sanity/cl/image_checks.hpp"
#include "sanity/cl/image_transfer_checks.hpp"
#include "sanity/cl/image_kernel_checks.hpp"
#include "sanity/cl/svm_checks.hpp"
#include "sanity/cl/program_checks.hpp"
#include "sanity/cl/kernel_checks.hpp"
#include "sanity/cl/math_checks.hpp"
#include "sanity/cl/event_checks.hpp"
#include "sanity/cl/queue_checks.hpp"
#include "sanity/cl/error_checks.hpp"
#include "sanity/gl/context_checks.hpp"
#include "sanity/gl/format_checks.hpp"
#include "sanity/gl/thread_checks.hpp"
#include "sanity/gl/buffer_checks.hpp"
#include "sanity/gl/texture_checks.hpp"
#include "sanity/gl/render_checks.hpp"
#include "sanity/gl/sync_checks.hpp"
#include "sanity/gl/swap_checks.hpp"
#include "sanity/gl/ext_checks.hpp"
#include "sanity/vk/instance_checks.hpp"
#include "sanity/vk/memory_checks.hpp"
#include "sanity/vk/resource_checks.hpp"
#include "sanity/vk/transfer_checks.hpp"
#include "sanity/vk/compute_checks.hpp"
#include "sanity/vk/graphics_checks.hpp"
#include "sanity/vk/sync_checks.hpp"
#include "sanity/err/family_checks.hpp"
#include "sanity/err/kill_checks.hpp"
#include "sanity/err/oom_checks.hpp"
#include "sanity/err/shader_checks.hpp"
#include "sanity/err/wait_checks.hpp"
#include "sanity/vk/query_checks.hpp"
#include "sanity/vk/wsi_checks.hpp"
#include "sanity/vk/robust_checks.hpp"
#include "sanity/vk/external_checks.hpp"
#include "sanity/fmt/bc_checks.hpp"
#include "sanity/fmt/color_checks.hpp"
#include "sanity/fmt/depth_checks.hpp"
#include "sanity/fmt/display_checks.hpp"
#include "sanity/fmt/footprint_checks.hpp"
#include "sanity/fmt/roundtrip_checks.hpp"
#include "sanity/fmt/support_checks.hpp"
#include "sanity/fmt/value_checks.hpp"
#include "sanity/fmt/yuv_checks.hpp"
#include "sanity/interop/d3d_checks.hpp"
#include "sanity/interop/d3d_process_checks.hpp"
#include "sanity/interop/d3dcl_checks.hpp"
#include "sanity/interop/gdi_capture_checks.hpp"
#include "sanity/interop/gdi_d2d_checks.hpp"
#include "sanity/interop/gdi_d3d9_checks.hpp"
#include "sanity/interop/gdi_dib_checks.hpp"
#include "sanity/interop/gdi_dxgi_checks.hpp"
#include "sanity/interop/gdi_gl_checks.hpp"
#include "sanity/interop/gdi_layered_checks.hpp"
#include "sanity/interop/gld3d_checks.hpp"
#include "sanity/interop/glcl_checks.hpp"
#include "sanity/interop/vk_checks.hpp"
#include "sanity/capture/cadence_checks.hpp"
#include "sanity/capture/dup_checks.hpp"
#include "sanity/capture/thumbnail_checks.hpp"
#include "sanity/capture/wgc_checks.hpp"
#include "sanity/present/alpha_checks.hpp"
#include "sanity/present/content_checks.hpp"
#include "sanity/present/display_checks.hpp"
#include "sanity/present/pacing_checks.hpp"
#include "sanity/present/path_checks.hpp"
#include "sanity/present/process_checks.hpp"
#include "sanity/present/timing_checks.hpp"
#include "sanity/present/window_checks.hpp"
#include "sanity/shader/cache_checks.hpp"
#include "sanity/shader/cl_checks.hpp"
#include "sanity/shader/determinism_checks.hpp"
#include "sanity/shader/dxbc_checks.hpp"
#include "sanity/shader/dxil_checks.hpp"
#include "sanity/shader/glsl_checks.hpp"
#include "sanity/shader/spirv_checks.hpp"
#include "sanity/env/display_checks.hpp"
#include "sanity/env/icd_checks.hpp"
#include "sanity/env/install_checks.hpp"
#include "sanity/sync/order_checks.hpp"
#include "sanity/sync/wait_checks.hpp"
#include "sanity/sync/thread_checks.hpp"
#include "sanity/sync/process_checks.hpp"
#include "sanity/sync/cross_checks.hpp"
#include "sanity/sync/fair_checks.hpp"
#include "sanity/iso/fresh_checks.hpp"
#include "sanity/iso/handle_checks.hpp"
#include "sanity/iso/fault_checks.hpp"
#include "sanity/iso/stall_checks.hpp"
#include "sanity/err/arg_checks.hpp"
#include "sanity/err/unsupported_checks.hpp"
#include "sanity/err/recovery_checks.hpp"
#include "sanity/err/limit_checks.hpp"
#include "sanity/life/life_checks.hpp"
#include "sanity/kmd/query_checks.hpp"
#include "sanity/kmd/display_checks.hpp"
#include "sanity/kmd/surrogate_checks.hpp"
#include "sanity/bits/parity_checks.hpp"
#include "sanity/bits/share_checks.hpp"
#include "sanity/bits/addr_checks.hpp"
#include "sanity/reg/reg_checks.hpp"
#include "sanity/lsw/lsw_checks.hpp"
#include "sanity/diag/diag_checks.hpp"

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
    guarded(suite, {"sanity.enum.identity.hybrid_roles", {"enum", "dxgi", "kmt"}, false, 30,
               enumeration::IdentityChecks::hybrid_roles});
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
    guarded(suite, {"sanity.d3d10.create.device_flags", {"d3d10"}, false, 60, d3d10::CreateChecks::device_flags});
    guarded(suite, {"sanity.d3d10.fl.levels_101", {"d3d10"}, false, 60, d3d10::CreateChecks::levels_101});
    guarded(suite, {"sanity.d3d10.res.format_support", {"d3d10", "formats"}, false, 120, d3d10::CreateChecks::format_support});
    guarded(suite, {"sanity.d3d10.render.pipeline_warp", {"d3d10"}, false, 120, d3d10::RenderChecks::pipeline_warp});
    guarded(suite, {"sanity.d3d10.render.streamout", {"d3d10"}, false, 60, d3d10::RenderChecks::streamout});
    guarded(suite, {"sanity.d3d10.map.contracts", {"d3d10"}, false, 120, d3d10::RenderChecks::map_contracts});
    guarded(suite, {"sanity.d3d10.dxgi.swapchain", {"d3d10", "dxgi", "present", "dwm", "exclusive", "pacing"}, true, 120, d3d10::DxgiChecks::swapchain});
    guarded(suite, {"sanity.d3d10.dxgi.interop_d3d11", {"d3d10", "d3d11", "dxgi", "interop"}, false, 120, d3d10::DxgiChecks::interop_d3d11});
    guarded(suite, {"sanity.d3d10.query.predicate", {"d3d10"}, false, 120, d3d10::QueryChecks::predicate});
    guarded(suite, {"sanity.d3d10.state.clearstate_and_stale", {"d3d10"}, false, 60, d3d10::QueryChecks::clearstate_and_stale});
    guarded(suite, {"sanity.d3d9.caps.create_matrix", {"d3d9"}, false, 120, d3d9::CapsChecks::create_matrix});
    guarded(suite, {"sanity.d3d9.caps.caps_vs_behaviour", {"d3d9"}, false, 120, d3d9::CapsChecks::caps_vs_behaviour});
    guarded(suite, {"sanity.d3d9.caps.check_device_format_consistent", {"d3d9", "formats"}, false, 120, d3d9::CapsChecks::check_device_format_consistent});
    guarded(suite, {"sanity.d3d9.caps.ex_interface", {"d3d9"}, false, 60, d3d9::CapsChecks::ex_interface});
    guarded(suite, {"sanity.d3d9.caps.adapter_identity", {"d3d9", "dxgi"}, false, 60, d3d9::CapsChecks::adapter_identity});
    guarded(suite, {"sanity.d3d9.res.pool_usage_matrix", {"d3d9"}, false, 120, d3d9::ResChecks::pool_usage_matrix});
    guarded(suite, {"sanity.d3d9.res.mip_levels_and_sizes", {"d3d9"}, false, 60, d3d9::ResChecks::mip_levels_and_sizes});
    guarded(suite, {"sanity.d3d9.res.volume_and_cube", {"d3d9"}, false, 60, d3d9::ResChecks::volume_and_cube});
    guarded(suite, {"sanity.d3d9.res.managed_roundtrip", {"d3d9", "mem"}, false, 120, d3d9::ResChecks::managed_roundtrip});
    guarded(suite, {"sanity.d3d9.res.priority_and_lod", {"d3d9"}, false, 60, d3d9::ResChecks::priority_and_lod});
    guarded(suite, {"sanity.d3d9.res.autogenmipmap", {"d3d9"}, false, 60, d3d9::ResChecks::autogenmipmap});
    guarded(suite, {"sanity.d3d9.res.exhaustion", {"d3d9", "mem", "slow", "exclusive"}, true, 300, d3d9::ResChecks::exhaustion});
    guarded(suite, {"sanity.d3d9.lock.discard_fresh", {"d3d9"}, false, 60, d3d9::LockChecks::discard_fresh});
    guarded(suite, {"sanity.d3d9.lock.nooverwrite_inflight", {"d3d9"}, false, 60, d3d9::LockChecks::nooverwrite_inflight});
    guarded(suite, {"sanity.d3d9.lock.readonly_and_partial", {"d3d9"}, false, 60, d3d9::LockChecks::readonly_and_partial});
    guarded(suite, {"sanity.d3d9.lock.dirty_regions", {"d3d9"}, false, 60, d3d9::LockChecks::dirty_regions});
    guarded(suite, {"sanity.d3d9.lock.errors", {"d3d9"}, false, 60, d3d9::LockChecks::errors});
    guarded(suite, {"sanity.d3d9.blt.stretchrect", {"d3d9"}, false, 120, d3d9::BltChecks::stretchrect});
    guarded(suite, {"sanity.d3d9.blt.updatesurface_texture", {"d3d9"}, false, 60, d3d9::BltChecks::updatesurface_texture});
    guarded(suite, {"sanity.d3d9.blt.colorfill_and_readback", {"d3d9"}, false, 60, d3d9::BltChecks::colorfill_and_readback});
    guarded(suite, {"sanity.d3d9.blt.frontbuffer", {"d3d9", "dwm", "exclusive", "pacing"}, true, 60, d3d9::BltChecks::frontbuffer});
    guarded(suite, {"sanity.d3d9.render.fixed_function", {"d3d9"}, false, 120, d3d9::RenderChecks::fixed_function});
    guarded(suite, {"sanity.d3d9.render.shaders", {"d3d9"}, false, 120, d3d9::RenderChecks::shaders});
    guarded(suite, {"sanity.d3d9.render.states_and_blend", {"d3d9"}, false, 120, d3d9::RenderChecks::states_and_blend});
    guarded(suite, {"sanity.d3d9.render.multisample", {"d3d9", "msaa"}, false, 120, d3d9::RenderChecks::multisample});
    guarded(suite, {"sanity.d3d9.render.draw_variants", {"d3d9"}, false, 60, d3d9::RenderChecks::draw_variants});
    guarded(suite, {"sanity.d3d9.query.occlusion", {"d3d9"}, false, 60, d3d9::QueryChecks::occlusion});
    guarded(suite, {"sanity.d3d9.query.event_timestamp", {"d3d9"}, false, 60, d3d9::QueryChecks::event_timestamp});
    guarded(suite, {"sanity.d3d9.present.swap_effects", {"d3d9", "present", "dwm", "exclusive", "pacing"}, true, 120, d3d9::PresentChecks::swap_effects});
    guarded(suite, {"sanity.d3d9.present.intervals", {"d3d9", "present", "exclusive", "pacing"}, true, 60, d3d9::PresentChecks::intervals});
    guarded(suite, {"sanity.d3d9.present.presentex_flags", {"d3d9", "present", "pacing"}, false, 60, d3d9::PresentChecks::presentex_flags});
    guarded(suite, {"sanity.d3d9.present.src_dst_rects", {"d3d9", "present", "dwm", "exclusive", "pacing"}, true, 60, d3d9::PresentChecks::src_dst_rects});
    guarded(suite, {"sanity.d3d9.lost.reset_semantics", {"d3d9"}, false, 60, d3d9::LostChecks::reset_semantics});
    guarded(suite, {"sanity.d3d9.lost.ex_never_lost", {"d3d9", "present", "exclusive"}, true, 120, d3d9::LostChecks::ex_never_lost});
    guarded(suite, {"sanity.d3d9.share.cross_device", {"d3d9", "interop"}, false, 60, d3d9::ShareChecks::cross_device});
    guarded(suite, {"sanity.d3d9.share.cross_process", {"d3d9", "interop"}, false, 180, d3d9::ShareChecks::cross_process});
    guarded(suite, {"sanity.d3d9.share.d3dimage_pattern", {"d3d9", "d3d11", "interop"}, false, 120, d3d9::ShareChecks::d3dimage_pattern});
    guarded(suite, {"sanity.d3d9.fail.unsupported_features", {"d3d9"}, false, 60, d3d9::FailChecks::unsupported_features});
    guarded(suite, {"sanity.d3d9.fail.bad_parameters", {"d3d9"}, false, 60, d3d9::FailChecks::bad_parameters});
    guarded(suite, {"sanity.d3d9.fail.lifetime", {"d3d9", "mem", "slow"}, false, 300, d3d9::FailChecks::lifetime});
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
    guarded(suite, {"sanity.d3d11.map.persistent_across_frames", {"d3d11", "dxgi", "present", "exclusive", "pacing"}, true, 60, d3d11::MapChecks::persistent_across_frames});
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
    guarded(suite, {"sanity.dxgi.enum.adapters_consistent", {"dxgi"}, false, 60, dxgi::EnumChecks::adapters_consistent});
    guarded(suite, {"sanity.dxgi.enum.factory_lifecycle", {"dxgi"}, false, 60, dxgi::EnumChecks::factory_lifecycle});
    guarded(suite, {"sanity.dxgi.enum.adapter_desc_matches_apis", {"dxgi", "d3d9", "d3d11", "d3d12", "vulkan"}, false, 90, dxgi::EnumChecks::adapter_desc_matches_apis});
    guarded(suite, {"sanity.dxgi.enum.output_modes", {"dxgi", "d3d11"}, false, 60, dxgi::EnumChecks::output_modes});
    guarded(suite, {"sanity.dxgi.enum.output_desc_vs_win32", {"dxgi"}, false, 60, dxgi::EnumChecks::output_desc_vs_win32});
    guarded(suite, {"sanity.dxgi.swap.model_matrix", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 180, dxgi::SwapChecks::model_matrix});
    guarded(suite, {"sanity.dxgi.swap.buffer_contents", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::buffer_contents});
    guarded(suite, {"sanity.dxgi.swap.present1", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::present1});
    guarded(suite, {"sanity.dxgi.swap.allow_tearing", {"dxgi", "d3d11", "present", "pacing"}, false, 60, dxgi::SwapChecks::allow_tearing});
    guarded(suite, {"sanity.dxgi.swap.do_not_wait", {"dxgi", "d3d11", "present", "pacing"}, false, 90, dxgi::SwapChecks::do_not_wait});
    guarded(suite, {"sanity.dxgi.swap.waitable_latency", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::waitable_latency});
    guarded(suite, {"sanity.dxgi.swap.frame_statistics", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::frame_statistics});
    guarded(suite, {"sanity.dxgi.swap.resize_buffers", {"dxgi", "d3d11", "present"}, false, 90, dxgi::SwapChecks::resize_buffers});
    guarded(suite, {"sanity.dxgi.swap.fullscreen_transition", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 120, dxgi::SwapChecks::fullscreen_transition});
    guarded(suite, {"sanity.dxgi.swap.hdr_colorspace", {"dxgi", "d3d11", "present"}, false, 90, dxgi::SwapChecks::hdr_colorspace});
    guarded(suite, {"sanity.dxgi.swap.getdc", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::getdc});
    guarded(suite, {"sanity.dxgi.swap.background_alpha_composition", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::background_alpha_composition});
    guarded(suite, {"sanity.dxgi.swap.multiple_and_orphan", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::SwapChecks::multiple_and_orphan});
    guarded(suite, {"sanity.dxgi.mem.video_memory_info", {"dxgi", "d3d11", "mem"}, false, 90, dxgi::MemChecks::video_memory_info});
    guarded(suite, {"sanity.dxgi.mem.offer_reclaim", {"dxgi", "d3d11", "mem"}, false, 60, dxgi::MemChecks::offer_reclaim});
    guarded(suite, {"sanity.dxgi.mem.trim_and_priority", {"dxgi", "d3d11"}, false, 60, dxgi::MemChecks::trim_and_priority});
    guarded(suite, {"sanity.dxgi.overlay.caps_consistent", {"dxgi", "d3d11", "present"}, false, 60, dxgi::MemChecks::overlay_caps_consistent});
    guarded(suite, {"sanity.dxgi.overlay.mpo_present", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 90, dxgi::MemChecks::overlay_mpo_present});
    guarded(suite, {"sanity.dxgi.debug.no_messages", {"dxgi", "d3d11", "present"}, false, 60, dxgi::MemChecks::debug_no_messages});
    guarded(suite, {"sanity.dxgi.device.removal_events", {"dxgi", "d3d11", "d3d12", "slow"}, false, 60, dxgi::MemChecks::removal_events});
    guarded(suite, {"sanity.dxgi.present.duplication_visible", {"dxgi", "d3d11", "present", "exclusive", "pacing"}, true, 60, dxgi::MemChecks::duplication_visible});
    guarded(suite, {"sanity.gl.pixelformat.enumerate_choosable", {"gl"}, false, 120, gl::FormatChecks::enumerate_choosable});
    guarded(suite, {"sanity.gl.pixelformat.every_format_renders", {"gl"}, false, 180, gl::FormatChecks::every_format_renders});
    guarded(suite, {"sanity.gl.pixelformat.choose_matches_request", {"gl"}, false, 60, gl::FormatChecks::choose_matches_request});
    guarded(suite, {"sanity.gl.pixelformat.arb_attribs_agree_with_describe", {"gl"}, false, 60, gl::FormatChecks::arb_attribs_agree_with_describe});
    guarded(suite, {"sanity.gl.pixelformat.set_twice_fails", {"gl"}, false, 60, gl::FormatChecks::set_twice_fails});
    guarded(suite, {"sanity.gl.pixelformat.srgb_and_float", {"gl", "formats"}, false, 90, gl::FormatChecks::srgb_and_float});
    guarded(suite, {"sanity.gl.context.legacy_create", {"gl"}, false, 60, gl::ContextChecks::legacy_create});
    guarded(suite, {"sanity.gl.context.arb_version_profile", {"gl"}, false, 120, gl::ContextChecks::arb_version_profile});
    guarded(suite, {"sanity.gl.context.flags_granted_or_error", {"gl"}, false, 60, gl::ContextChecks::flags_granted_or_error});
    guarded(suite, {"sanity.gl.context.robustness_reset", {"gl"}, false, 60, gl::ContextChecks::robustness_reset});
    guarded(suite, {"sanity.gl.context.no_error", {"gl"}, false, 60, gl::ContextChecks::no_error});
    guarded(suite, {"sanity.gl.context.share_lists", {"gl"}, false, 60, gl::ContextChecks::share_lists});
    guarded(suite, {"sanity.gl.context.non_shared_isolation", {"gl"}, false, 60, gl::ContextChecks::non_shared_isolation});
    guarded(suite, {"sanity.gl.context.share_across_threads", {"gl"}, false, 90, gl::ThreadChecks::share_across_threads});
    guarded(suite, {"sanity.gl.context.make_current_rules", {"gl"}, false, 60, gl::ThreadChecks::make_current_rules});
    guarded(suite, {"sanity.gl.context.many_contexts", {"gl", "slow"}, false, 240, gl::ContextChecks::many_contexts});
    guarded(suite, {"sanity.gl.thread.concurrent_contexts", {"gl"}, false, 90, gl::ThreadChecks::concurrent_contexts});
    guarded(suite, {"sanity.gl.thread.producer_consumer", {"gl"}, false, 120, gl::ThreadChecks::producer_consumer});
    guarded(suite, {"sanity.gl.memobj.external_memory_caps", {"gl", "d3d11", "interop"}, false, 90, gl::ExtChecks::external_memory_caps});
    guarded(suite, {"sanity.gl.ext.string_matches_entry_points", {"gl"}, false, 60, gl::ExtChecks::string_matches_entry_points});
    guarded(suite, {"sanity.gl.ext.unadvertised_not_supported", {"gl"}, false, 90, gl::ExtChecks::unadvertised_not_supported});
    guarded(suite, {"sanity.gl.ext.version_implies_core_functions", {"gl"}, false, 60, gl::ExtChecks::version_implies_core_functions});
    guarded(suite, {"sanity.gl.error.getError_contracts", {"gl"}, false, 60, gl::ExtChecks::getError_contracts});
    guarded(suite, {"sanity.gl.error.debug_output", {"gl"}, false, 60, gl::ExtChecks::debug_output});
    guarded(suite, {"sanity.gl.swap.interval_semantics", {"gl", "present", "pacing"}, false, 60, gl::SwapChecks::interval_semantics});
    guarded(suite, {"sanity.gl.swap.swapbuffers_content", {"gl", "present", "dwm", "exclusive", "pacing"}, true, 90, gl::SwapChecks::swapbuffers_content});
    guarded(suite, {"sanity.gl.swap.resize_and_dc_lifetime", {"gl", "present", "pacing"}, false, 60, gl::SwapChecks::resize_and_dc_lifetime});
    guarded(suite, {"sanity.gl.sync.fence_ordering", {"gl"}, false, 90, gl::SyncChecks::fence_ordering});
    guarded(suite, {"sanity.gl.sync.timeout_semantics", {"gl"}, false, 90, gl::SyncChecks::timeout_semantics});
    guarded(suite, {"sanity.gl.sync.finish_flush", {"gl"}, false, 90, gl::SyncChecks::finish_flush});
    guarded(suite, {"sanity.gl.query.timer_samples_primitives", {"gl"}, false, 90, gl::SyncChecks::timer_samples_primitives});
    guarded(suite, {"sanity.gl.query.availability_rules", {"gl"}, false, 60, gl::SyncChecks::availability_rules});
    guarded(suite, {"sanity.gl.fbo.completeness_matrix", {"gl", "formats"}, false, 90, gl::RenderChecks::completeness_matrix});
    guarded(suite, {"sanity.gl.fbo.blit", {"gl", "msaa"}, false, 90, gl::RenderChecks::blit});
    guarded(suite, {"sanity.gl.draw.core_vs_compat_equal", {"gl"}, false, 60, gl::RenderChecks::core_vs_compat_equal});
    guarded(suite, {"sanity.gl.draw.instanced_indirect", {"gl"}, false, 90, gl::RenderChecks::instanced_indirect});
    guarded(suite, {"sanity.gl.draw.large_batches", {"gl", "slow"}, false, 180, gl::RenderChecks::large_batches});
    guarded(suite, {"sanity.gl.texture.all_targets_roundtrip", {"gl", "formats"}, false, 90, gl::TextureChecks::all_targets_roundtrip});
    guarded(suite, {"sanity.gl.texture.compressed_formats", {"gl", "formats"}, false, 120, gl::TextureChecks::compressed_formats});
    guarded(suite, {"sanity.gl.texture.generate_mipmap", {"gl"}, false, 90, gl::TextureChecks::generate_mipmap});
    guarded(suite, {"sanity.gl.texture.views", {"gl"}, false, 60, gl::TextureChecks::views});
    guarded(suite, {"sanity.gl.texture.immutable_storage", {"gl"}, false, 60, gl::TextureChecks::immutable_storage});
    guarded(suite, {"sanity.gl.texture.max_size_limits", {"gl"}, false, 60, gl::TextureChecks::max_size_limits});
    guarded(suite, {"sanity.gl.buffer.storage_flag_matrix", {"gl"}, false, 90, gl::BufferChecks::storage_flag_matrix});
    guarded(suite, {"sanity.gl.buffer.persistent_coherent_gpu_write", {"gl"}, false, 90, gl::BufferChecks::persistent_coherent_gpu_write});
    guarded(suite, {"sanity.gl.buffer.persistent_noncoherent_flush", {"gl"}, false, 90, gl::BufferChecks::persistent_noncoherent_flush});
    guarded(suite, {"sanity.gl.buffer.dynamic_storage_subdata", {"gl"}, false, 60, gl::BufferChecks::dynamic_storage_subdata});
    guarded(suite, {"sanity.gl.buffer.map_range_flags", {"gl"}, false, 60, gl::BufferChecks::map_range_flags});
    guarded(suite, {"sanity.gl.buffer.map_errors", {"gl"}, false, 60, gl::BufferChecks::map_errors});
    guarded(suite, {"sanity.gl.buffer.copy_and_clear", {"gl", "mem", "slow"}, false, 180, gl::BufferChecks::copy_and_clear});
    guarded(suite, {"sanity.gl.buffer.pbo_pack_unpack", {"gl", "formats"}, false, 120, gl::BufferChecks::pbo_pack_unpack});
    guarded(suite, {"sanity.cl.platform.query_consistent", {"opencl"}, false, 60, cl::PlatformChecks::query_consistent});
    guarded(suite, {"sanity.cl.device.query_all_params", {"opencl"}, false, 60, cl::DeviceChecks::query_all_params});
    guarded(suite, {"sanity.cl.device.extensions_match_entry_points", {"opencl"}, false, 90, cl::DeviceChecks::extensions_match_entry_points});
    guarded(suite, {"sanity.cl.device.limits_honoured", {"opencl"}, false, 120, cl::DeviceChecks::limits_honoured});
    guarded(suite, {"sanity.cl.device.both_adapters", {"opencl"}, false, 120, cl::DeviceChecks::both_adapters});
    guarded(suite, {"sanity.cl.device.create_sub_devices_error", {"opencl"}, false, 60, cl::DeviceChecks::create_sub_devices_error});
    guarded(suite, {"sanity.cl.mem.use_host_ptr_coherence", {"opencl"}, false, 60, cl::MemChecks::use_host_ptr_coherence});
    guarded(suite, {"sanity.cl.mem.use_host_ptr_blocking_ops", {"opencl"}, false, 60, cl::MemChecks::use_host_ptr_blocking_ops});
    guarded(suite, {"sanity.cl.mem.alloc_host_ptr", {"opencl"}, false, 60, cl::MemChecks::alloc_host_ptr});
    guarded(suite, {"sanity.cl.mem.copy_host_ptr", {"opencl"}, false, 60, cl::MemChecks::copy_host_ptr});
    guarded(suite, {"sanity.cl.mem.access_flags_honoured", {"opencl"}, false, 60, cl::MemChecks::access_flags_honoured});
    guarded(suite, {"sanity.cl.mem.sub_buffer_alignment", {"opencl"}, false, 60, cl::MemChecks::sub_buffer_alignment});
    guarded(suite, {"sanity.cl.transfer.read_write_blocking_nonblocking", {"opencl", "slow"}, false, 180, cl::TransferChecks::read_write_blocking_nonblocking});
    guarded(suite, {"sanity.cl.transfer.rect_and_fill_copy", {"opencl"}, false, 60, cl::TransferChecks::rect_and_fill_copy});
    guarded(suite, {"sanity.cl.transfer.map_unmap_ranges", {"opencl"}, false, 60, cl::TransferChecks::map_unmap_ranges});
    guarded(suite, {"sanity.cl.transfer.migrate_mem_objects", {"opencl"}, false, 60, cl::TransferChecks::migrate_mem_objects});
    guarded(suite, {"sanity.cl.image.formats_supported", {"opencl", "formats"}, false, 180, cl::ImageChecks::formats_supported});
    guarded(suite, {"sanity.cl.image.transfers_rect_fill", {"opencl"}, false, 90, cl::ImageTransferChecks::transfers_rect_fill});
    guarded(suite, {"sanity.cl.image.from_buffer", {"opencl"}, false, 60, cl::ImageTransferChecks::from_buffer});
    guarded(suite, {"sanity.cl.kernel.images_in_kernels", {"opencl"}, false, 90, cl::ImageKernelChecks::images_in_kernels});
    guarded(suite, {"sanity.cl.svm.coarse_grain", {"opencl"}, false, 60, cl::SvmChecks::coarse_grain});
    guarded(suite, {"sanity.cl.svm.fine_grain_caps", {"opencl"}, false, 60, cl::SvmChecks::fine_grain_caps});
    guarded(suite, {"sanity.cl.program.build_source_options", {"opencl"}, false, 90, cl::ProgramChecks::build_source_options});
    guarded(suite, {"sanity.cl.program.build_errors_logs", {"opencl"}, false, 60, cl::ProgramChecks::build_errors_logs});
    guarded(suite, {"sanity.cl.program.binary_roundtrip", {"opencl"}, false, 60, cl::ProgramChecks::binary_roundtrip});
    guarded(suite, {"sanity.cl.program.il_spirv_caps", {"opencl"}, false, 60, cl::ProgramChecks::il_spirv_caps});
    guarded(suite, {"sanity.cl.program.compile_link", {"opencl"}, false, 60, cl::ProgramChecks::compile_link});
    guarded(suite, {"sanity.cl.kernel.atomics_vs_cpu", {"opencl"}, false, 90, cl::KernelChecks::atomics_vs_cpu});
    guarded(suite, {"sanity.cl.kernel.local_memory_barrier", {"opencl"}, false, 90, cl::KernelChecks::local_memory_barrier});
    guarded(suite, {"sanity.cl.kernel.arithmetic_vs_reference", {"opencl"}, false, 120, cl::MathChecks::arithmetic_vs_reference});
    guarded(suite, {"sanity.cl.kernel.doubles_caps", {"opencl"}, false, 60, cl::MathChecks::doubles_caps});
    guarded(suite, {"sanity.cl.kernel.args_and_ndrange", {"opencl"}, false, 60, cl::KernelChecks::args_and_ndrange});
    guarded(suite, {"sanity.cl.kernel.large_workloads", {"opencl", "slow"}, false, 180, cl::KernelChecks::large_workloads});
    guarded(suite, {"sanity.cl.event.in_order_ordering", {"opencl"}, false, 60, cl::EventChecks::in_order_ordering});
    guarded(suite, {"sanity.cl.event.out_of_order_deps", {"opencl"}, false, 60, cl::EventChecks::out_of_order_deps});
    guarded(suite, {"sanity.cl.event.user_events_callbacks", {"opencl"}, false, 60, cl::EventChecks::user_events_callbacks});
    guarded(suite, {"sanity.cl.event.profiling_monotonic", {"opencl"}, false, 60, cl::EventChecks::profiling_monotonic});
    guarded(suite, {"sanity.cl.queue.multiple_queues_concurrent", {"opencl"}, false, 90, cl::QueueChecks::multiple_queues_concurrent});
    guarded(suite, {"sanity.cl.queue.finish_flush_release", {"opencl"}, false, 60, cl::QueueChecks::finish_flush_release});
    guarded(suite, {"sanity.cl.error.codes_documented", {"opencl"}, false, 60, cl::ErrorChecks::codes_documented});
    guarded(suite, {"sanity.d3d12.dev.create_levels", {"d3d12", "dxgi"}, false, 90, d3d12::DevChecks::create_levels});
    guarded(suite, {"sanity.d3d12.dev.options_consistent", {"d3d12"}, false, 90, d3d12::DevChecks::options_consistent});
    guarded(suite, {"sanity.d3d12.dev.format_support", {"d3d12", "formats"}, false, 180, d3d12::DevChecks::format_support});
    guarded(suite, {"sanity.d3d12.dev.node_and_uma", {"d3d12"}, false, 60, d3d12::DevChecks::node_and_uma});
    guarded(suite, {"sanity.d3d12.heap.default_upload_readback", {"d3d12", "mem", "slow"}, false, 180, d3d12::HeapChecks::default_upload_readback});
    guarded(suite, {"sanity.d3d12.heap.custom_properties", {"d3d12"}, false, 90, d3d12::HeapChecks::custom_properties});
    guarded(suite, {"sanity.d3d12.heap.placed_alignment", {"d3d12"}, false, 90, d3d12::HeapChecks::placed_alignment});
    guarded(suite, {"sanity.d3d12.heap.aliasing", {"d3d12"}, false, 90, d3d12::HeapChecks::aliasing});
    guarded(suite, {"sanity.d3d12.heap.reserved_tiles", {"d3d12", "tiled"}, false, 90, d3d12::HeapChecks::reserved_tiles});
    guarded(suite, {"sanity.d3d12.heap.committed_flags", {"d3d12"}, false, 90, d3d12::HeapChecks::committed_flags});
    guarded(suite, {"sanity.d3d12.heap.zeroed_new", {"d3d12", "mem"}, false, 180, d3d12::HeapChecks::zeroed_new});
    guarded(suite, {"sanity.d3d12.heap.exhaustion", {"d3d12", "mem", "slow", "exclusive"}, true, 300, d3d12::HeapChecks::exhaustion});
    guarded(suite, {"sanity.d3d12.map.persistent", {"d3d12", "mem"}, false, 90, d3d12::MapChecks::persistent});
    guarded(suite, {"sanity.d3d12.map.upload_visible", {"d3d12"}, false, 90, d3d12::MapChecks::upload_visible});
    guarded(suite, {"sanity.d3d12.map.readback_after_fence", {"d3d12"}, false, 90, d3d12::MapChecks::readback_after_fence});
    guarded(suite, {"sanity.d3d12.copy.footprints", {"d3d12", "formats"}, false, 120, d3d12::CopyChecks::footprints});
    guarded(suite, {"sanity.d3d12.copy.region_variants", {"d3d12", "msaa"}, false, 90, d3d12::CopyChecks::region_variants});
    guarded(suite, {"sanity.d3d12.queue.types", {"d3d12", "compute"}, false, 90, d3d12::QueueChecks::types});
    guarded(suite, {"sanity.d3d12.queue.many_lists", {"d3d12"}, false, 90, d3d12::QueueChecks::many_lists});
    guarded(suite, {"sanity.d3d12.fence.cross_queue", {"d3d12", "compute"}, false, 90, d3d12::FenceChecks::cross_queue});
    guarded(suite, {"sanity.d3d12.fence.cpu_semantics", {"d3d12"}, false, 180, d3d12::FenceChecks::cpu_semantics});
    guarded(suite, {"sanity.d3d12.fence.shared", {"d3d12", "interop"}, false, 240, d3d12::FenceChecks::shared});
    guarded(suite, {"sanity.d3d12.barrier.transition_uav", {"d3d12", "compute"}, false, 90, d3d12::BarrierChecks::transition_uav});
    guarded(suite, {"sanity.d3d12.barrier.enhanced", {"d3d12", "compute"}, false, 90, d3d12::BarrierChecks::enhanced});
    guarded(suite, {"sanity.d3d12.barrier.misuse_tolerance", {"d3d12"}, false, 90, d3d12::BarrierChecks::misuse_tolerance});
    guarded(suite, {"sanity.d3d12.desc.heaps_and_tables", {"d3d12", "compute"}, false, 120, d3d12::DescChecks::heaps_and_tables});
    guarded(suite, {"sanity.d3d12.root.signatures", {"d3d12", "compute"}, false, 90, d3d12::DescChecks::root_signatures});
    guarded(suite, {"sanity.d3d12.pso.graphics_compute", {"d3d12", "compute"}, false, 120, d3d12::PsoChecks::graphics_compute});
    guarded(suite, {"sanity.d3d12.pso.library_cache", {"d3d12"}, false, 90, d3d12::PsoChecks::library_cache});
    guarded(suite, {"sanity.d3d12.render.draw_variants", {"d3d12", "compute"}, false, 120, d3d12::DrawChecks::draw_variants});
    guarded(suite, {"sanity.d3d12.render.msaa_resolve", {"d3d12", "msaa"}, false, 120, d3d12::DrawChecks::msaa_resolve});
    guarded(suite, {"sanity.d3d12.query.occlusion_pipeline", {"d3d12"}, false, 90, d3d12::QueryChecks::occlusion_pipeline});
    guarded(suite, {"sanity.d3d12.query.timestamp_calibration", {"d3d12", "compute"}, true, 90, d3d12::QueryChecks::timestamp_calibration});
    guarded(suite, {"sanity.d3d12.present.dxgi_queue", {"d3d12", "dxgi", "present", "dwm", "exclusive", "pacing"}, true, 180, d3d12::QueryChecks::present_queue});
    guarded(suite, {"sanity.d3d12.share.heap_resource_nt", {"d3d12", "interop"}, false, 240, d3d12::ShareChecks::heap_resource_nt});
    guarded(suite, {"sanity.d3d12.share.keyed_cross_api", {"d3d12", "interop"}, false, 120, d3d12::ShareChecks::keyed_cross_api});
    guarded(suite, {"sanity.d3d12.resid.make_evict", {"d3d12", "mem", "exclusive"}, true, 240, d3d12::ResidChecks::make_evict});
    guarded(suite, {"sanity.d3d12.resid.budget_pressure", {"d3d12", "mem", "slow", "exclusive"}, true, 300, d3d12::ResidChecks::budget_pressure});
    guarded(suite, {"sanity.d3d12.opt.raytracing", {"d3d12"}, false, 90, d3d12::OptChecks::raytracing});
    guarded(suite, {"sanity.d3d12.opt.mesh_shaders", {"d3d12"}, false, 60, d3d12::OptChecks::mesh_shaders});
    guarded(suite, {"sanity.d3d12.opt.vrs_sampler_feedback", {"d3d12"}, false, 90, d3d12::OptChecks::vrs_sampler_feedback});
    guarded(suite, {"sanity.d3d12.opt.work_graphs_dxr11", {"d3d12", "compute"}, false, 90, d3d12::OptChecks::work_graphs_dxr11});
    guarded(suite, {"sanity.d3d12.removal.reasons", {"d3d12", "exclusive"}, true, 120, d3d12::LifeChecks::removal_reasons});
    guarded(suite, {"sanity.d3d12.stress.lifetime_exit", {"d3d12", "mem", "leak", "slow"}, false, 300, d3d12::LifeChecks::lifetime_exit});
    guarded(suite, {"sanity.d3d12.stress.concurrent_queues", {"d3d12", "slow"}, false, 120, d3d12::LifeChecks::concurrent_queues});
    guarded(suite, {"sanity.vk.instance.version_layers_extensions", {"vulkan"}, false, 60, vk::InstanceChecks::version_layers_extensions});
    guarded(suite, {"sanity.vk.device.props_features_consistent", {"vulkan"}, false, 120, vk::InstanceChecks::props_features_consistent});
    guarded(suite, {"sanity.vk.device.limits_minimums_honoured", {"vulkan"}, false, 120, vk::InstanceChecks::limits_minimums_honoured});
    guarded(suite, {"sanity.vk.device.queue_families", {"vulkan"}, false, 90, vk::InstanceChecks::queue_families});
    guarded(suite, {"sanity.vk.device.extensions_match_functions", {"vulkan"}, false, 90, vk::InstanceChecks::extensions_match_functions});
    guarded(suite, {"sanity.vk.memory.every_type_allocates", {"vulkan", "mem"}, false, 120, vk::MemoryChecks::every_type_allocates});
    guarded(suite, {"sanity.vk.memory.host_visible_map", {"vulkan", "mem"}, false, 90, vk::MemoryChecks::host_visible_map});
    guarded(suite, {"sanity.vk.memory.host_coherent_no_flush", {"vulkan", "mem"}, false, 90, vk::MemoryChecks::host_coherent_no_flush});
    guarded(suite, {"sanity.vk.memory.noncoherent_flush_invalidate", {"vulkan", "mem"}, false, 90, vk::MemoryChecks::noncoherent_flush_invalidate});
    guarded(suite, {"sanity.vk.memory.device_local_and_budget", {"vulkan", "mem"}, true, 90, vk::MemoryChecks::device_local_and_budget});
    guarded(suite, {"sanity.vk.memory.allocation_limits", {"vulkan", "mem", "slow", "exclusive"}, true, 300, vk::MemoryChecks::allocation_limits});
    guarded(suite, {"sanity.vk.memory.dedicated_and_aliasing", {"vulkan", "mem"}, false, 90, vk::MemoryChecks::dedicated_and_aliasing});
    guarded(suite, {"sanity.vk.buffer.all_usages", {"vulkan"}, false, 90, vk::ResourceChecks::all_usages});
    guarded(suite, {"sanity.vk.image.formats_features_usage", {"vulkan", "formats"}, false, 180, vk::ResourceChecks::formats_features_usage});
    guarded(suite, {"sanity.vk.image.types_layers_mips", {"vulkan"}, false, 120, vk::ResourceChecks::types_layers_mips});
    guarded(suite, {"sanity.vk.image.sparse_caps", {"vulkan"}, false, 90, vk::ResourceChecks::sparse_caps});
    guarded(suite, {"sanity.vk.transfer.copy_all_forms", {"vulkan", "formats"}, false, 120, vk::TransferChecks::copy_all_forms});
    guarded(suite, {"sanity.vk.transfer.blit_resolve", {"vulkan"}, false, 90, vk::TransferChecks::blit_resolve});
    guarded(suite, {"sanity.vk.transfer.clear_fill_update", {"vulkan", "formats"}, false, 120, vk::TransferChecks::clear_fill_update});
    guarded(suite, {"sanity.vk.compute.vs_cpu", {"vulkan"}, false, 120, vk::ComputeChecks::vs_cpu});
    guarded(suite, {"sanity.vk.descriptor.pools_templates_samplers", {"vulkan"}, false, 90, vk::ComputeChecks::pools_templates_samplers});
    guarded(suite, {"sanity.vk.graphics.vs_twin", {"vulkan", "msaa"}, false, 120, vk::GraphicsChecks::vs_twin});
    guarded(suite, {"sanity.vk.graphics.pipeline_features", {"vulkan"}, false, 120, vk::GraphicsChecks::pipeline_features});
    guarded(suite, {"sanity.vk.sync.fences", {"vulkan"}, false, 90, vk::SyncChecks::fences});
    guarded(suite, {"sanity.vk.sync.binary_semaphores", {"vulkan"}, false, 120, vk::SyncChecks::binary_semaphores});
    guarded(suite, {"sanity.vk.sync.timeline_semaphores", {"vulkan"}, false, 90, vk::SyncChecks::timeline_semaphores});
    guarded(suite, {"sanity.vk.sync.events_barriers", {"vulkan"}, false, 120, vk::SyncChecks::events_barriers});
    guarded(suite, {"sanity.vk.sync.memory_model", {"vulkan"}, false, 120, vk::SyncChecks::memory_model});
    guarded(suite, {"sanity.vk.sync.submit2_and_wait_idle", {"vulkan"}, false, 90, vk::SyncChecks::submit2_and_wait_idle});
    guarded(suite, {"sanity.vk.query.timestamps", {"vulkan"}, false, 90, vk::QueryChecks::timestamps});
    guarded(suite, {"sanity.vk.cmd.buffers_reset_reuse", {"vulkan"}, false, 90, vk::QueryChecks::buffers_reset_reuse});
    guarded(suite, {"sanity.vk.wsi.surface_caps_consistent", {"vulkan", "present"}, true, 90, vk::WsiChecks::surface_caps_consistent});
    guarded(suite, {"sanity.vk.wsi.fifo_every_image_in_order", {"vulkan", "present", "dwm", "exclusive", "pacing"}, true, 90, vk::WsiChecks::fifo_every_image_in_order});
    guarded(suite, {"sanity.vk.wsi.mailbox_immediate_caps", {"vulkan", "present", "dwm", "exclusive", "pacing"}, true, 120, vk::WsiChecks::mailbox_immediate_caps});
    guarded(suite, {"sanity.vk.wsi.out_of_date_suboptimal", {"vulkan", "present", "dwm", "exclusive", "pacing"}, true, 90, vk::WsiChecks::out_of_date_suboptimal});
    guarded(suite, {"sanity.vk.wsi.acquire_timeouts", {"vulkan", "present", "pacing"}, false, 90, vk::WsiChecks::acquire_timeouts});
    guarded(suite, {"sanity.vk.wsi.minimized_and_destroyed", {"vulkan", "present", "dwm", "exclusive", "pacing"}, true, 120, vk::WsiChecks::minimized_and_destroyed});
    guarded(suite, {"sanity.vk.device.independent_devices", {"vulkan"}, false, 90, vk::RobustChecks::independent_devices});
    guarded(suite, {"sanity.vk.validation.reference_workload", {"vulkan", "present"}, true, 120, vk::RobustChecks::reference_workload});
    guarded(suite, {"sanity.vk.external.memory_caps_consistent", {"vulkan", "interop"}, false, 90, vk::ExternalChecks::memory_caps_consistent});
    guarded(suite, {"sanity.vk.external.semaphore_fence_caps", {"vulkan", "interop"}, false, 90, vk::ExternalChecks::semaphore_fence_caps});
    guarded(suite, {"sanity.fmt.support_bits_match_use", {"formats", "d3d9", "d3d11", "d3d12"}, false, 240, fmt::SupportChecks::support_bits_match_use});
    guarded(suite, {"sanity.fmt.cross_api_equal", {"formats", "d3d11", "d3d12"}, false, 240, fmt::SupportChecks::cross_api_equal});
    guarded(suite, {"sanity.fmt.roundtrip_uncompressed", {"formats", "d3d11", "d3d12", "vulkan", "gl", "opencl"}, false, 300, fmt::RoundtripChecks::roundtrip_uncompressed});
    guarded(suite, {"sanity.fmt.packed_formats", {"formats", "d3d11", "d3d12", "vulkan", "gl"}, false, 240, fmt::RoundtripChecks::packed_formats});
    guarded(suite, {"sanity.fmt.bc_blocks", {"formats", "d3d11", "d3d12", "vulkan", "gl"}, false, 240, fmt::BcChecks::bc_blocks});
    guarded(suite, {"sanity.fmt.bc_dimensions", {"formats", "d3d11", "d3d12", "vulkan", "gl"}, false, 240, fmt::BcChecks::bc_dimensions});
    guarded(suite, {"sanity.fmt.srgb_conversion", {"formats", "d3d9", "d3d11", "d3d12"}, false, 180, fmt::ColorChecks::srgb_conversion});
    guarded(suite, {"sanity.fmt.typeless_casts", {"formats", "d3d11", "d3d12"}, false, 180, fmt::ColorChecks::typeless_casts});
    guarded(suite, {"sanity.fmt.depth_stencil", {"formats", "d3d11", "vulkan", "gl"}, false, 180, fmt::DepthChecks::depth_stencil});
    guarded(suite, {"sanity.fmt.yuv_nv12_p010", {"formats", "d3d11", "d3d12"}, false, 180, fmt::YuvChecks::yuv_nv12_p010});
    guarded(suite, {"sanity.fmt.yuv_video_processing", {"formats", "d3d11"}, false, 120, fmt::YuvChecks::yuv_video_processing});
    guarded(suite, {"sanity.fmt.float_special_values", {"formats", "d3d11", "d3d12", "vulkan", "gl", "opencl"}, false, 240, fmt::ValueChecks::float_special_values});
    guarded(suite, {"sanity.fmt.snorm_and_clamp", {"formats", "d3d11", "d3d12", "vulkan", "gl"}, false, 180, fmt::ValueChecks::snorm_and_clamp});
    guarded(suite, {"sanity.fmt.mip_and_array_footprints", {"formats", "d3d9", "d3d11", "d3d12", "vulkan", "gl", "opencl"}, false, 300, fmt::FootprintChecks::mip_and_array_footprints});
    guarded(suite, {"sanity.fmt.display_formats", {"formats", "d3d9", "d3d11", "dxgi", "present", "dwm", "exclusive", "pacing"}, true, 180, fmt::DisplayChecks::display_formats});
    guarded(suite, {"sanity.shader.dxbc.sm2_sm3", {"shader", "d3d9", "d3d11"}, false, 120, shader::DxbcChecks::sm2_sm3});
    guarded(suite, {"sanity.shader.dxbc.sm4_sm5", {"shader", "d3d10", "d3d11", "d3d12"}, false, 180, shader::DxbcChecks::sm4_sm5});
    guarded(suite, {"sanity.shader.dxil.sm6", {"shader", "d3d12"}, false, 120, shader::DxilChecks::sm6});
    guarded(suite, {"sanity.shader.spirv.valid_runs", {"shader", "vulkan"}, false, 120, shader::SpirvChecks::valid_runs});
    guarded(suite, {"sanity.shader.spirv.invalid_fails", {"shader", "vulkan"}, false, 120, shader::SpirvChecks::invalid_fails});
    guarded(suite, {"sanity.shader.glsl.versions", {"shader", "gl"}, false, 120, shader::GlslChecks::versions});
    guarded(suite, {"sanity.shader.glsl.invalid_diagnostics", {"shader", "gl"}, false, 90, shader::GlslChecks::invalid_diagnostics});
    guarded(suite, {"sanity.shader.dxbc.invalid_diagnostics", {"shader", "d3d9", "d3d11", "d3d12"}, false, 120, shader::DxbcChecks::invalid_diagnostics});
    guarded(suite, {"sanity.shader.cl.c_and_il", {"shader", "opencl"}, false, 120, shader::ClChecks::c_and_il});
    guarded(suite, {"sanity.shader.determinism.created_twice", {"shader", "d3d11", "d3d12", "vulkan", "gl", "opencl"}, false, 180, shader::DeterminismChecks::created_twice});
    guarded(suite, {"sanity.shader.determinism.two_processes", {"shader", "d3d11", "d3d12", "vulkan", "gl", "opencl", "slow"}, false, 300, shader::DeterminismChecks::two_processes});
    guarded(suite, {"sanity.shader.cache.reuse_correct", {"shader", "vulkan", "gl", "d3d12"}, false, 180, shader::CacheChecks::reuse_correct});
    guarded(suite, {"sanity.interop.gdi.dxgi_getdc_roundtrip", {"interop", "gdi", "dxgi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiDxgiChecks::getdc_roundtrip});
    guarded(suite, {"sanity.interop.gdi.dxgi_getdc_discard_flag", {"interop", "gdi", "dxgi", "d3d11"}, false, 90, interop::GdiDxgiChecks::getdc_discard_flag});
    guarded(suite, {"sanity.interop.gdi.dxgi_getdc_lifetime", {"interop", "gdi", "dxgi", "d3d11"}, false, 90, interop::GdiDxgiChecks::getdc_lifetime});
    guarded(suite, {"sanity.interop.gdi.d3d9_surface_getdc", {"interop", "gdi", "d3d9"}, false, 90, interop::GdiD3d9Checks::surface_getdc});
    guarded(suite, {"sanity.interop.gdi.d3d9ex_flip_gdi", {"interop", "gdi", "d3d9", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiD3d9Checks::flip_getdc});
    guarded(suite, {"sanity.interop.gdi.gl_window_owndc", {"interop", "gdi", "gl", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiGlChecks::window_owndc});
    guarded(suite, {"sanity.interop.gdi.gl_mixed_swap", {"interop", "gdi", "gl", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiGlChecks::mixed_swap});
    guarded(suite, {"sanity.interop.gdi.bitblt_each_api", {"interop", "gdi", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "dwm", "exclusive", "pacing"}, true, 240, interop::GdiCaptureChecks::bitblt_each_api});
    guarded(suite, {"sanity.interop.gdi.printwindow_full_content", {"interop", "gdi", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "dwm", "exclusive", "pacing"}, true, 240, interop::GdiCaptureChecks::printwindow_full_content});
    guarded(suite, {"sanity.interop.gdi.layered_per_pixel_alpha", {"interop", "gdi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiLayeredChecks::per_pixel_alpha});
    guarded(suite, {"sanity.interop.gdi.layered_over_3d_update", {"interop", "gdi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 180, interop::GdiLayeredChecks::over_3d_update});
    guarded(suite, {"sanity.interop.gdi.dib_section_upload", {"interop", "gdi", "d3d9", "d3d11", "d3d12", "gl", "vulkan"}, false, 180, interop::GdiDibChecks::section_upload});
    guarded(suite, {"sanity.interop.gdi.dib_after_gpu_writes", {"interop", "gdi", "d3d9", "d3d11"}, false, 120, interop::GdiDibChecks::after_gpu_writes});
    guarded(suite, {"sanity.interop.gdi.d2d_gdi_interop_target", {"interop", "gdi", "d3d11"}, false, 120, interop::GdiD2dChecks::gdi_interop_target});
    guarded(suite, {"sanity.interop.gdi.dwrite_text_on_surface", {"interop", "gdi", "d3d11"}, false, 120, interop::GdiD2dChecks::dwrite_text_on_surface});
    guarded(suite, {"sanity.interop.gdi.text_over_flip_window", {"interop", "gdi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiLayeredChecks::text_over_flip_window});
    guarded(suite, {"sanity.interop.gdi.redirection_mixed_windows", {"interop", "gdi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 180, interop::GdiLayeredChecks::mixed_windows});
    guarded(suite, {"sanity.interop.gdi.cursor_in_captures", {"interop", "gdi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiCaptureChecks::cursor_in_captures});
    guarded(suite, {"sanity.interop.gdi.gdi_after_device_lost", {"interop", "gdi", "d3d9", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiD3d9Checks::after_device_lost});
    guarded(suite, {"sanity.interop.gdi.multi_thread_gdi_3d", {"interop", "gdi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GdiDibChecks::multi_thread});
    guarded(suite, {"sanity.interop.glcl.context_device_match", {"interop", "gl", "opencl"}, false, 90, interop::GlClChecks::context_device_match});
    guarded(suite, {"sanity.interop.glcl.buffer_sharing", {"interop", "gl", "opencl"}, false, 90, interop::GlClChecks::buffer_sharing});
    guarded(suite, {"sanity.interop.glcl.texture_targets_mips_layers", {"interop", "gl", "opencl"}, false, 120, interop::GlClChecks::texture_targets_mips_layers});
    guarded(suite, {"sanity.interop.glcl.renderbuffer_sharing", {"interop", "gl", "opencl"}, false, 90, interop::GlClChecks::renderbuffer_sharing});
    guarded(suite, {"sanity.interop.glcl.gl_event_sync", {"interop", "gl", "opencl"}, false, 90, interop::GlClChecks::gl_event_sync});
    guarded(suite, {"sanity.interop.glcl.acquire_release_errors", {"interop", "gl", "opencl"}, false, 90, interop::GlClChecks::acquire_release_errors});
    guarded(suite, {"sanity.interop.glcl.caps_unsupported", {"interop", "gl", "opencl"}, false, 90, interop::GlClChecks::caps_unsupported});
    guarded(suite, {"sanity.interop.d3dcl.d3d11_sharing", {"interop", "d3d11", "opencl"}, false, 120, interop::D3dClChecks::d3d11_sharing});
    guarded(suite, {"sanity.interop.d3dcl.d3d10_sharing", {"interop", "d3d10", "opencl"}, false, 120, interop::D3dClChecks::d3d10_sharing});
    guarded(suite, {"sanity.interop.d3dcl.dx9_media_sharing", {"interop", "d3d9", "opencl"}, false, 120, interop::D3dClChecks::dx9_media_sharing});
    guarded(suite, {"sanity.interop.d3dcl.acquire_order_caps", {"interop", "d3d9", "d3d10", "d3d11", "opencl"}, false, 120, interop::D3dClChecks::acquire_order_caps});
    guarded(suite, {"sanity.interop.gld3d.nv_dx_interop_d3d11", {"interop", "gl", "d3d11"}, false, 120, interop::GlD3dChecks::d3d11_objects});
    guarded(suite, {"sanity.interop.gld3d.nv_dx_interop_d3d9ex", {"interop", "gl", "d3d9"}, false, 120, interop::GlD3dChecks::d3d9ex_objects});
    guarded(suite, {"sanity.interop.gld3d.lock_ordering", {"interop", "gl", "d3d11"}, false, 120, interop::GlD3dChecks::lock_ordering});
    guarded(suite, {"sanity.interop.gld3d.formats_and_targets", {"interop", "gl", "d3d11", "formats"}, false, 180, interop::GlD3dChecks::formats_and_targets});
    guarded(suite, {"sanity.interop.gld3d.present_via_d3d", {"interop", "gl", "d3d11", "dxgi", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::GlD3dChecks::present_via_d3d});
    guarded(suite, {"sanity.interop.gld3d.caps_unsupported", {"interop", "gl", "d3d11"}, false, 90, interop::GlD3dChecks::caps_unsupported});
    guarded(suite, {"sanity.interop.d3d.d3d9ex_shared_to_d3d11", {"interop", "d3d9", "d3d11"}, false, 120, interop::D3dChecks::d3d9ex_shared_to_d3d11});
    guarded(suite, {"sanity.interop.d3d.d3d11_nt_handle_d3d12", {"interop", "d3d11", "d3d12"}, false, 120, interop::D3dChecks::d3d11_nt_handle_d3d12});
    guarded(suite, {"sanity.interop.d3d.shared_fence_ordering", {"interop", "d3d11", "d3d12", "slow"}, false, 300, interop::D3dChecks::shared_fence_ordering});
    guarded(suite, {"sanity.interop.d3d.keyed_mutex", {"interop", "d3d11"}, false, 240, interop::D3dChecks::keyed_mutex});
    guarded(suite, {"sanity.interop.d3d.d3d11on12", {"interop", "d3d11", "d3d12", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::D3dChecks::d3d11on12});
    guarded(suite, {"sanity.interop.d3d.cross_adapter_render_only", {"interop", "d3d12", "present", "dwm", "exclusive", "pacing"}, true, 120, interop::D3dProcessChecks::cross_adapter_render_only});
    guarded(suite, {"sanity.interop.d3d.cross_process_sharing", {"interop", "d3d11"}, false, 240, interop::D3dProcessChecks::cross_process_sharing});
    guarded(suite, {"sanity.interop.d3d.cross_bitness_sharing", {"interop", "d3d11", "x86-only", "cross-bitness"}, false, 240, interop::D3dProcessChecks::cross_bitness_sharing});
    guarded(suite, {"sanity.interop.d3d.handle_lifetime", {"interop", "d3d11", "mem"}, false, 180, interop::D3dProcessChecks::handle_lifetime});
    guarded(suite, {"sanity.interop.d3d.invalid_open_errors", {"interop", "d3d11", "d3d12"}, false, 120, interop::D3dProcessChecks::invalid_open_errors});
    guarded(suite, {"sanity.interop.vk.import_d3d11_texture", {"interop", "vulkan", "d3d11"}, false, 120, interop::VkChecks::import_d3d11_texture});
    guarded(suite, {"sanity.interop.vk.import_d3d12_resource", {"interop", "vulkan", "d3d12"}, false, 120, interop::VkChecks::import_d3d12_resource});
    guarded(suite, {"sanity.interop.vk.export_to_d3d12", {"interop", "vulkan", "d3d12"}, false, 120, interop::VkChecks::export_to_d3d12});
    guarded(suite, {"sanity.interop.vk.export_to_gl", {"interop", "vulkan", "gl"}, false, 120, interop::VkChecks::export_to_gl});
    guarded(suite, {"sanity.interop.vk.opencl_external_memory_caps", {"interop", "vulkan", "opencl"}, false, 120, interop::VkChecks::opencl_external_memory_caps});
    guarded(suite, {"sanity.interop.vk.kmt_handles_caps", {"interop", "vulkan", "d3d11"}, false, 240, interop::VkChecks::kmt_handles_caps});
    guarded(suite, {"sanity.interop.vk.cross_process_present_chain", {"interop", "vulkan", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 180, interop::VkChecks::cross_process_present_chain});
    guarded(suite, {"sanity.present.path.d3d9_blt", {"present", "dwm", "d3d9", "exclusive", "pacing"}, true, 120, present::PathChecks::d3d9_blt});
    guarded(suite, {"sanity.present.path.d3d9ex_flip", {"present", "dwm", "d3d9", "exclusive", "pacing"}, true, 120, present::PathChecks::d3d9ex_flip});
    guarded(suite, {"sanity.present.path.dxgi_blt_sequential", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::PathChecks::dxgi_blt_sequential});
    guarded(suite, {"sanity.present.path.dxgi_blt_discard", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::PathChecks::dxgi_blt_discard});
    guarded(suite, {"sanity.present.path.dxgi_flip_sequential", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::PathChecks::dxgi_flip_sequential});
    guarded(suite, {"sanity.present.path.dxgi_flip_discard", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::PathChecks::dxgi_flip_discard});
    guarded(suite, {"sanity.present.path.d3d12_flip", {"present", "dwm", "dxgi", "d3d12", "exclusive", "pacing"}, true, 120, present::PathChecks::d3d12_flip});
    guarded(suite, {"sanity.present.path.gl_swapbuffers", {"present", "dwm", "gl", "exclusive", "pacing"}, true, 120, present::PathChecks::gl_swapbuffers});
    guarded(suite, {"sanity.present.path.vk_fifo", {"present", "dwm", "vulkan", "exclusive", "pacing"}, true, 120, present::PathChecks::vk_fifo});
    guarded(suite, {"sanity.present.path.vk_immediate_mailbox", {"present", "dwm", "vulkan", "exclusive", "pacing"}, true, 120, present::PathChecks::vk_immediate_mailbox});
    guarded(suite, {"sanity.present.content.exact_pixels", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 300, present::ContentChecks::exact_pixels});
    guarded(suite, {"sanity.present.content.sizes_and_formats", {"present", "dwm", "dxgi", "d3d11", "formats", "exclusive", "pacing"}, true, 240, present::ContentChecks::sizes_and_formats});
    guarded(suite, {"sanity.present.content.scaling_modes", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::ContentChecks::scaling_modes});
    guarded(suite, {"sanity.present.content.alpha_modes", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::AlphaChecks::alpha_modes});
    guarded(suite, {"sanity.present.timing.vsync_interval_equals_refresh", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 300, present::TimingChecks::vsync_interval_equals_refresh});
    guarded(suite, {"sanity.present.timing.interval_two", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "exclusive", "pacing"}, true, 300, present::TimingChecks::interval_two});
    guarded(suite, {"sanity.present.timing.immediate_faster_than_refresh", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 300, present::TimingChecks::immediate_ordering});
    guarded(suite, {"sanity.present.timing.frame_latency_limits", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::TimingChecks::frame_latency_limits});
    guarded(suite, {"sanity.present.timing.present_stats_vs_capture", {"present", "dwm", "dxgi", "d3d9", "d3d11", "d3d12", "exclusive", "pacing"}, true, 180, present::TimingChecks::present_stats_vs_capture});
    guarded(suite, {"sanity.present.timing.high_refresh_rates", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 300, present::TimingChecks::high_refresh_rates});
    guarded(suite, {"sanity.present.timing.stutter_per_path", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 180, present::PacingChecks::stutter_per_path});
    guarded(suite, {"sanity.present.timing.pacing_under_gpu_load", {"present", "dwm", "d3d11", "vulkan", "exclusive", "pacing"}, true, 180, present::PacingChecks::pacing_under_gpu_load});
    guarded(suite, {"sanity.present.timing.pacing_with_parallel_presenters", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 180, present::PacingChecks::pacing_with_parallel_presenters});
    guarded(suite, {"sanity.present.latency.present_to_display", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 180, present::PacingChecks::present_to_display});
    guarded(suite, {"sanity.present.mirror.host_cadence", {"present", "dwm", "dxgi", "d3d11", "host", "exclusive", "pacing"}, true, 120, present::PacingChecks::mirror_host_cadence});
    guarded(suite, {"sanity.present.window.resize_during_presentation", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 300, present::WindowChecks::resize_during_presentation});
    guarded(suite, {"sanity.present.window.occlusion", {"present", "dwm", "d3d9", "d3d11", "d3d12", "exclusive", "pacing"}, true, 180, present::WindowChecks::occlusion});
    guarded(suite, {"sanity.present.window.minimize_restore", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 300, present::WindowChecks::minimize_restore});
    guarded(suite, {"sanity.present.window.fullscreen_exclusive", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::WindowChecks::fullscreen_exclusive});
    guarded(suite, {"sanity.present.window.borderless_fullscreen_flip", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::WindowChecks::borderless_fullscreen_flip});
    guarded(suite, {"sanity.present.window.many_windows", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 180, present::WindowChecks::many_windows});
    guarded(suite, {"sanity.present.window.many_processes", {"present", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "exclusive", "pacing"}, true, 240, present::ProcessChecks::many_processes});
    guarded(suite, {"sanity.present.window.multi_monitor", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::WindowChecks::multi_monitor});
    guarded(suite, {"sanity.present.window.destroy_while_presenting", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::WindowChecks::destroy_while_presenting});
    guarded(suite, {"sanity.present.mirror.host_receives_sequence", {"present", "dwm", "dxgi", "d3d11", "host", "exclusive", "pacing"}, true, 120, present::DisplayChecks::host_receives_sequence});
    guarded(suite, {"sanity.present.display.output_stays_active", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::DisplayChecks::output_stays_active});
    guarded(suite, {"sanity.present.display.idle_then_present", {"present", "dwm", "d3d11", "d3d12", "gl", "exclusive", "pacing"}, true, 180, present::DisplayChecks::idle_then_present});
    guarded(suite, {"sanity.present.latency.input_to_photon_monotonic", {"present", "dwm", "dxgi", "d3d11", "exclusive", "pacing"}, true, 120, present::DisplayChecks::input_to_photon_monotonic});
    guarded(suite, {"sanity.capture.dxgi.desktop_duplication_content", {"capture", "dxgi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, capture::DupChecks::desktop_duplication_content});
    guarded(suite, {"sanity.capture.dxgi.dirty_rects_cover_changes", {"capture", "dxgi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, capture::DupChecks::dirty_rects_cover_changes});
    guarded(suite, {"sanity.capture.dxgi.pointer_info", {"capture", "dxgi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, capture::DupChecks::pointer_info});
    guarded(suite, {"sanity.capture.wgc.each_api_equals_presented", {"capture", "wgc", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "dwm", "exclusive", "pacing"}, true, 300, capture::WgcChecks::each_api_equals_presented});
    guarded(suite, {"sanity.capture.wgc.frames_in_order", {"capture", "wgc", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, capture::WgcChecks::frames_in_order});
    guarded(suite, {"sanity.capture.wgc.resize_and_close", {"capture", "wgc", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, capture::WgcChecks::resize_and_close});
    guarded(suite, {"sanity.capture.wgc.cadence_matches_present", {"capture", "wgc", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "dwm", "exclusive", "pacing"}, true, 180, capture::CadenceChecks::wgc_cadence_matches_present});
    guarded(suite, {"sanity.capture.dxgi.duplication_cadence", {"capture", "dxgi", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "dwm", "exclusive", "pacing"}, true, 180, capture::CadenceChecks::duplication_cadence});
    guarded(suite, {"sanity.capture.latency_present_to_capture", {"capture", "wgc", "dxgi", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "dwm", "exclusive", "pacing"}, true, 180, capture::CadenceChecks::latency_present_to_capture});
    guarded(suite, {"sanity.capture.steady_no_drift", {"capture", "wgc", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 120, capture::CadenceChecks::steady_no_drift});
    guarded(suite, {"sanity.capture.dwm.thumbnail_each_api", {"capture", "dwm", "d3d9", "d3d11", "d3d12", "gl", "vulkan", "present", "exclusive", "pacing"}, true, 300, capture::ThumbnailChecks::each_api});
    guarded(suite, {"sanity.capture.dwm.thumbnail_updates", {"capture", "dwm", "d3d11", "present", "exclusive", "pacing"}, true, 120, capture::ThumbnailChecks::updates});
    guarded(suite, {"sanity.err.vk_wrong_queue_family", {"err", "robust", "vulkan", "exclusive"}, true, 240, err::FamilyChecks::vk_wrong_queue_family});
    guarded(suite, {"sanity.err.gpu_wait_never_signalled", {"err", "robust", "vulkan", "d3d12", "gl", "exclusive"}, true, 420, err::WaitChecks::never_signalled});
    guarded(suite, {"sanity.err.vk_host_event_gate", {"err", "robust", "vulkan", "exclusive"}, true, 240, err::WaitChecks::vk_host_event_gate});
    guarded(suite, {"sanity.err.long_running_shader", {"err", "robust", "slow", "compute", "d3d11", "d3d12", "vulkan", "exclusive"}, true, 900, err::ShaderChecks::long_running});
    guarded(suite, {"sanity.err.infinite_shader", {"err", "robust", "compute", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 600, err::ShaderChecks::infinite});
    guarded(suite, {"sanity.err.kill_mid_submit", {"err", "robust", "slow", "d3d9", "d3d11", "d3d12", "vulkan", "opencl", "gl", "exclusive"}, true, 900, err::KillChecks::mid_submit});
    guarded(suite, {"sanity.err.oom_every_api", {"err", "robust", "mem", "slow", "d3d9", "d3d11", "d3d12", "vulkan", "opencl", "gl", "exclusive"}, true, 900, err::OomChecks::every_api});
    guarded(suite, {"sanity.sync.order.readback_after_signal", {"sync", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 900, sync::OrderChecks::readback_after_signal});
    guarded(suite, {"sanity.sync.order.flush_makes_visible", {"sync", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, sync::OrderChecks::flush_makes_visible});
    guarded(suite, {"sanity.sync.order.queue_order_in_order", {"sync", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, sync::OrderChecks::queue_order_in_order});
    guarded(suite, {"sanity.sync.order.out_of_order_events", {"sync", "opencl"}, false, 120, sync::OrderChecks::out_of_order_events});
    guarded(suite, {"sanity.sync.wait.timeout_honoured", {"sync", "robust", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 180, sync::WaitChecks::timeout_honoured});
    guarded(suite, {"sanity.sync.wait.zero_timeout_poll", {"sync", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, sync::WaitChecks::zero_timeout_poll});
    guarded(suite, {"sanity.sync.wait.signal_before_wait", {"sync", "d3d11", "d3d12", "vulkan", "opencl"}, false, 180, sync::WaitChecks::signal_before_wait});
    guarded(suite, {"sanity.sync.thread.many_threads_one_device", {"sync", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 300, sync::ThreadChecks::many_threads_one_device});
    guarded(suite, {"sanity.sync.thread.multi_queue_vulkan", {"sync", "vulkan"}, false, 120, sync::ThreadChecks::multi_queue_vulkan});
    guarded(suite, {"sanity.sync.thread.d3d12_multi_queue", {"sync", "d3d12"}, false, 120, sync::ThreadChecks::d3d12_multi_queue});
    guarded(suite, {"sanity.sync.thread.immediate_context_serialised", {"sync", "d3d11"}, false, 120, sync::ThreadChecks::immediate_context_serialised});
    guarded(suite, {"sanity.sync.process.many_processes", {"sync", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 420, sync::ProcessChecks::many_processes});
    guarded(suite, {"sanity.sync.process.shared_fence_ping_pong", {"sync", "d3d12"}, false, 420, sync::ProcessChecks::shared_fence_ping_pong});
    guarded(suite, {"sanity.sync.cross.d3d11_d3d12_fence", {"sync", "interop", "d3d11", "d3d12"}, false, 180, sync::CrossChecks::d3d11_d3d12_fence});
    guarded(suite, {"sanity.sync.cross.vulkan_d3d12_semaphore", {"sync", "interop", "vulkan", "d3d12"}, false, 180, sync::CrossChecks::vulkan_d3d12_semaphore});
    guarded(suite, {"sanity.sync.cross.gl_d3d11_interop", {"sync", "interop", "gl", "d3d11"}, false, 240, sync::CrossChecks::gl_d3d11_interop});
    guarded(suite, {"sanity.sync.deadlock.contention_no_deadlock", {"sync", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, sync::FairChecks::contention_no_deadlock});
    guarded(suite, {"sanity.sync.fairness.low_rate_not_starved", {"sync", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 300, sync::FairChecks::low_rate_not_starved});
    guarded(suite, {"sanity.sync.fairness.equal_load_share", {"sync", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 300, sync::FairChecks::equal_load_share});
    guarded(suite, {"sanity.iso.mem.fresh_alloc_zero_or_unspecified", {"iso", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 900, iso::FreshChecks::fresh_alloc_zero_or_unspecified});
    guarded(suite, {"sanity.iso.mem.fresh_vram_pattern", {"iso", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 1800, iso::FreshChecks::fresh_vram_pattern});
    guarded(suite, {"sanity.iso.mem.fresh_host_visible", {"iso", "mem", "d3d11", "d3d12", "vulkan", "opencl"}, false, 600, iso::FreshChecks::fresh_host_visible});
    guarded(suite, {"sanity.iso.handle.no_access_without_handle", {"iso", "robust", "d3d12"}, false, 420, iso::HandleChecks::no_access_without_handle});
    guarded(suite, {"sanity.iso.handle.closed_handle_dead", {"iso", "d3d12"}, false, 420, iso::HandleChecks::closed_handle_dead});
    guarded(suite, {"sanity.iso.fault.crash_leaves_others", {"iso", "robust", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 420, iso::FaultChecks::crash_leaves_others});
    guarded(suite, {"sanity.iso.fault.device_loss_leaves_others", {"iso", "robust", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, iso::FaultChecks::device_loss_leaves_others});
    guarded(suite, {"sanity.iso.fault.oom_leaves_others", {"iso", "robust", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, iso::FaultChecks::oom_leaves_others});
    guarded(suite, {"sanity.iso.fault.hang_leaves_others", {"iso", "robust", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, iso::FaultChecks::hang_leaves_others});
    guarded(suite, {"sanity.iso.fault.invalid_commands_contained", {"iso", "robust", "d3d11", "d3d12", "vulkan", "opencl"}, false, 600, iso::FaultChecks::invalid_commands_contained});
    guarded(suite, {"sanity.iso.stall.heavy_load_other_finishes", {"iso", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 600, iso::StallChecks::heavy_load_other_finishes});
    guarded(suite, {"sanity.iso.stall.huge_upload_other_progress", {"iso", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 600, iso::StallChecks::huge_upload_other_progress});
    guarded(suite, {"sanity.iso.write.out_of_bounds_contained", {"iso", "robust", "d3d11", "d3d12", "vulkan", "opencl"}, false, 600, iso::FaultChecks::out_of_bounds_contained});
    guarded(suite, {"sanity.iso.share.read_only_enforced", {"iso", "d3d11"}, false, 120, iso::HandleChecks::read_only_enforced});
    guarded(suite, {"sanity.iso.session.service_session_separate", {"iso", "d3d12", "exclusive"}, true, 300, iso::HandleChecks::service_session_separate});
    guarded(suite, {"sanity.err.arg.d3d11_invalid_params", {"err", "d3d11"}, false, 60, err::ArgChecks::d3d11_invalid_params});
    guarded(suite, {"sanity.err.arg.d3d12_invalid_params", {"err", "d3d12"}, false, 60, err::ArgChecks::d3d12_invalid_params});
    guarded(suite, {"sanity.err.arg.d3d9_invalid_params", {"err", "d3d9"}, false, 60, err::ArgChecks::d3d9_invalid_params});
    guarded(suite, {"sanity.err.arg.vulkan_invalid_handles_null", {"err", "vulkan"}, false, 60, err::ArgChecks::vulkan_invalid_handles_null});
    guarded(suite, {"sanity.err.arg.opencl_error_codes", {"err", "opencl"}, false, 60, err::ArgChecks::opencl_error_codes});
    guarded(suite, {"sanity.err.arg.gl_error_flags", {"err", "gl"}, false, 60, err::ArgChecks::gl_error_flags});
    guarded(suite, {"sanity.err.arg.dxgi_invalid_swapchain", {"err", "dxgi", "d3d11"}, false, 60, err::ArgChecks::dxgi_invalid_swapchain});
    guarded(suite, {"sanity.err.unsupported.caps_match_use", {"err", "d3d11", "d3d12", "vulkan"}, false, 300, err::UnsupportedChecks::caps_match_use});
    guarded(suite, {"sanity.err.unsupported.formats", {"err", "formats", "d3d11", "d3d12"}, false, 300, err::UnsupportedChecks::formats});
    guarded(suite, {"sanity.err.unsupported.extensions_list_vs_entry", {"err", "vulkan", "gl", "opencl"}, false, 300, err::UnsupportedChecks::extensions_list_vs_entry});
    guarded(suite, {"sanity.err.unsupported.opencl_version_coherent", {"err", "opencl"}, false, 120, err::UnsupportedChecks::opencl_version_coherent});
    guarded(suite, {"sanity.err.fault.device_removed_reason", {"err", "robust", "d3d11", "d3d12", "exclusive"}, true, 240, err::RecoveryChecks::device_removed_reason});
    guarded(suite, {"sanity.err.fault.device_lost_vulkan", {"err", "robust", "vulkan", "exclusive"}, true, 240, err::RecoveryChecks::device_lost_vulkan});
    guarded(suite, {"sanity.err.fault.tdr_recovers", {"err", "robust", "d3d11", "d3d12", "vulkan", "exclusive"}, true, 420, err::RecoveryChecks::tdr_recovers});
    guarded(suite, {"sanity.err.fault.process_crash_cleanup", {"err", "robust", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 1200, err::RecoveryChecks::process_crash_cleanup});
    guarded(suite, {"sanity.err.fault.oom_clean", {"err", "robust", "mem", "slow", "d3d9", "d3d11", "d3d12", "vulkan", "opencl", "gl", "exclusive"}, true, 900, err::RecoveryChecks::oom_clean});
    guarded(suite, {"sanity.err.fault.kill_during_map", {"err", "robust", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 1200, err::RecoveryChecks::kill_during_map});
    guarded(suite, {"sanity.err.limit.handle_count", {"err", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 900, err::LimitChecks::handle_count});
    guarded(suite, {"sanity.err.limit.max_texture_dims", {"err", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, err::LimitChecks::max_texture_dims});
    guarded(suite, {"sanity.err.limit.max_threads_workgroup", {"err", "compute", "d3d11", "d3d12", "vulkan", "opencl"}, false, 120, err::LimitChecks::max_threads_workgroup});
    guarded(suite, {"sanity.err.limit.many_contexts_per_process", {"err", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 600, err::LimitChecks::many_contexts_per_process});
    guarded(suite, {"sanity.life.dwm.restart_apps_survive", {"life", "dwm", "d3d11", "vulkan", "present", "exclusive", "pacing"}, true, 420, life::LifeChecks::dwm_restart_apps_survive});
    guarded(suite, {"sanity.life.dwm.restart_desktop_recovers", {"life", "dwm", "present", "exclusive", "pacing"}, true, 240, life::LifeChecks::dwm_restart_desktop_recovers});
    guarded(suite, {"sanity.life.mode.change_apps_survive", {"life", "life", "d3d11", "dxgi", "present", "exclusive", "pacing"}, true, 240, life::LifeChecks::mode_change_apps_survive});
    guarded(suite, {"sanity.life.mode.refresh_rate_change", {"life", "life", "d3d11", "dxgi", "present", "exclusive", "pacing"}, true, 240, life::LifeChecks::mode_refresh_rate_change});
    guarded(suite, {"sanity.life.sleep.resume_contents", {"life", "life", "d3d11", "robust", "exclusive"}, true, 600, life::LifeChecks::sleep_resume_contents});
    guarded(suite, {"sanity.life.agent.restart_apps_unaffected", {"life", "life", "d3d11", "vulkan", "present", "exclusive", "pacing"}, true, 420, life::LifeChecks::agent_restart_apps_unaffected});
    guarded(suite, {"sanity.life.agent.restart_new_apps_work", {"life", "life", "d3d11", "exclusive"}, true, 300, life::LifeChecks::agent_restart_new_apps_work});
    guarded(suite, {"sanity.life.update.live_driver_update", {"life", "life", "d3d11", "vulkan", "present", "robust", "exclusive", "pacing"}, true, 600, life::LifeChecks::live_driver_update});
    guarded(suite, {"sanity.life.session.logoff_logon", {"life", "life", "d3d11", "exclusive"}, true, 300, life::LifeChecks::session_logoff_logon});
    guarded(suite, {"sanity.life.display.output_off_on", {"life", "life", "present", "exclusive", "pacing"}, true, 300, life::LifeChecks::display_output_off_on});
    guarded(suite, {"sanity.kmd.query.adapter_info_stable", {"kmd", "kmt"}, false, 120, kmd::QueryChecks::adapter_info_stable});
    guarded(suite, {"sanity.kmd.query.size_semantics", {"kmd", "kmt"}, false, 60, kmd::QueryChecks::size_semantics});
    guarded(suite, {"sanity.kmd.query.unsupported_type_status", {"kmd", "kmt"}, false, 60, kmd::QueryChecks::unsupported_type_status});
    guarded(suite, {"sanity.kmd.query.segment_info_consistent", {"kmd", "kmt", "dxgi"}, false, 60, kmd::QueryChecks::segment_info_consistent});
    guarded(suite, {"sanity.kmd.query.driver_caps_self_consistent", {"kmd", "kmt", "dxgi", "d3d11"}, false, 60, kmd::QueryChecks::driver_caps_self_consistent});
    guarded(suite, {"sanity.kmd.query.open_adapter_from_hdc_and_name", {"kmd", "kmt", "gdi"}, false, 60, kmd::QueryChecks::open_adapter_from_hdc_and_name});
    guarded(suite, {"sanity.kmd.query.close_adapter_twice", {"kmd", "kmt"}, false, 60, kmd::QueryChecks::close_adapter_twice});
    guarded(suite, {"sanity.kmd.escape.malformed_rejected", {"kmd", "kmt", "robust"}, false, 60, kmd::QueryChecks::escape_malformed_rejected});
    guarded(suite, {"sanity.kmd.escape.unknown_command_status", {"kmd", "kmt", "robust", "d3d11"}, false, 60, kmd::QueryChecks::escape_unknown_command_status});
    guarded(suite, {"sanity.kmd.display.vblank_wait_cadence", {"kmd", "kmt", "gdi", "exclusive", "pacing"}, true, 120, kmd::DisplayChecks::vblank_wait_cadence});
    guarded(suite, {"sanity.kmd.display.scanline_monotonic", {"kmd", "kmt", "gdi", "exclusive", "pacing"}, true, 120, kmd::DisplayChecks::scanline_monotonic});
    guarded(suite, {"sanity.kmd.display.vblank_vs_dwm_timing", {"kmd", "kmt", "dwm", "exclusive", "pacing"}, true, 120, kmd::DisplayChecks::vblank_vs_dwm_timing});
    guarded(suite, {"sanity.kmd.display.source_visible_while_composed", {"kmd", "kmt", "gdi", "dwm", "exclusive"}, true, 120, kmd::DisplayChecks::source_visible_while_composed});
    guarded(suite, {"sanity.kmd.display.mode_set_restore", {"kmd", "gdi", "exclusive"}, true, 300, kmd::DisplayChecks::mode_set_restore});
    guarded(suite, {"sanity.kmd.display.mode_list_all_settable", {"kmd", "gdi", "exclusive"}, true, 180, kmd::DisplayChecks::mode_list_all_settable});
    guarded(suite, {"sanity.kmd.display.cursor_shapes", {"kmd", "gdi"}, false, 120, kmd::DisplayChecks::cursor_shapes});
    guarded(suite, {"sanity.kmd.display.cursor_position_roundtrip", {"kmd", "gdi"}, false, 60, kmd::DisplayChecks::cursor_position_roundtrip});
    guarded(suite, {"sanity.kmd.display.gamma_ramp_roundtrip", {"kmd", "gdi", "exclusive"}, true, 60, kmd::DisplayChecks::gamma_ramp_roundtrip});
    guarded(suite, {"sanity.kmd.display.output_duplication_frame", {"kmd", "dxgi", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 180, kmd::DisplayChecks::output_duplication_frame});
    guarded(suite, {"sanity.kmd.alloc.lock_returns_written_data", {"kmd", "d3d12", "vulkan", "opencl", "d3d11"}, false, 180, kmd::SurrogateChecks::lock_returns_written_data});
    guarded(suite, {"sanity.kmd.alloc.lock_twice_status", {"kmd", "d3d11", "d3d12"}, false, 60, kmd::SurrogateChecks::lock_twice_status});
    guarded(suite, {"sanity.kmd.alloc.lock_pointer_stable_while_locked", {"kmd", "d3d12"}, false, 120, kmd::SurrogateChecks::lock_pointer_stable_while_locked});
    guarded(suite, {"sanity.kmd.alloc.discard_and_nooverwrite", {"kmd", "d3d11"}, false, 60, kmd::SurrogateChecks::discard_and_nooverwrite});
    guarded(suite, {"sanity.kmd.alloc.destroy_while_locked", {"kmd", "d3d11", "d3d12", "robust"}, false, 120, kmd::SurrogateChecks::destroy_while_locked});
    guarded(suite, {"sanity.kmd.alloc.nt_handle_share", {"kmd", "d3d12", "interop"}, false, 240, kmd::SurrogateChecks::nt_handle_share});
    guarded(suite, {"sanity.kmd.alloc.nt_handle_wrong_adapter", {"kmd", "d3d12", "interop"}, false, 120, kmd::SurrogateChecks::nt_handle_wrong_adapter});
    guarded(suite, {"sanity.kmd.alloc.share_requires_handle", {"kmd", "d3d12", "robust"}, false, 420, kmd::SurrogateChecks::share_requires_handle});
    guarded(suite, {"sanity.kmd.sync.monitored_fence_monotonic", {"kmd", "d3d12"}, false, 120, kmd::SurrogateChecks::monitored_fence_monotonic});
    guarded(suite, {"sanity.kmd.sync.wait_releases_on_signal", {"kmd", "d3d12"}, false, 60, kmd::SurrogateChecks::wait_releases_on_signal});
    guarded(suite, {"sanity.kmd.sync.wait_timeout", {"kmd", "d3d12"}, false, 60, kmd::SurrogateChecks::wait_timeout});
    guarded(suite, {"sanity.kmd.sync.shared_fence_across_processes", {"kmd", "d3d12"}, false, 420, kmd::SurrogateChecks::shared_fence_across_processes});
    guarded(suite, {"sanity.kmd.sync.cpu_signal_visible_to_gpu", {"kmd", "d3d12"}, false, 60, kmd::SurrogateChecks::cpu_signal_visible_to_gpu});
    guarded(suite, {"sanity.kmd.recovery.tdr_restores_service", {"kmd", "robust", "d3d11", "d3d12", "vulkan", "exclusive"}, true, 420, kmd::SurrogateChecks::tdr_restores_service});
    guarded(suite, {"sanity.kmd.recovery.tdr_other_adapter_unaffected", {"kmd", "robust", "d3d12", "exclusive"}, true, 240, kmd::SurrogateChecks::tdr_other_adapter_unaffected});
    guarded(suite, {"sanity.kmd.recovery.power_transition_contents", {"kmd", "robust", "d3d11", "exclusive"}, true, 600, kmd::SurrogateChecks::power_transition_contents});
    guarded(suite, {"sanity.kmd.recovery.query_status_after_reset", {"kmd", "kmt", "robust", "d3d11", "exclusive"}, true, 240, kmd::SurrogateChecks::query_status_after_reset});
    guarded(suite, {"sanity.bits.parity.operations_same_result", {"bits", "cross-bitness", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, bits::ParityChecks::operations_same_result});
    guarded(suite, {"sanity.bits.parity.integer_widths", {"bits", "cross-bitness", "opencl", "vulkan", "d3d12"}, false, 180, bits::ParityChecks::integer_widths});
    guarded(suite, {"sanity.bits.parity.data_roundtrip", {"bits", "d3d11", "d3d12", "vulkan", "opencl", "slow"}, false, 600, bits::ParityChecks::data_roundtrip});
    guarded(suite, {"sanity.bits.share.cross_bitness_nt_handle", {"bits", "cross-bitness", "d3d12", "interop"}, false, 240, bits::ShareChecks::cross_bitness_nt_handle});
    guarded(suite, {"sanity.bits.share.cross_bitness_fence", {"bits", "cross-bitness", "d3d12", "interop"}, false, 300, bits::ShareChecks::cross_bitness_fence});
    guarded(suite, {"sanity.bits.share.cross_bitness_dxgi_keyed_mutex", {"bits", "cross-bitness", "d3d11", "interop"}, false, 300, bits::ShareChecks::cross_bitness_dxgi_keyed_mutex});
    guarded(suite, {"sanity.bits.addr.allocation_fails_cleanly", {"bits", "x86-only", "mem", "slow", "robust", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, bits::AddrChecks::allocation_fails_cleanly});
    guarded(suite, {"sanity.bits.addr.large_address_aware_on", {"bits", "x86-only", "cross-bitness", "mem", "slow", "robust", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, bits::AddrChecks::large_address_aware_on});
    guarded(suite, {"sanity.bits.addr.large_address_aware_off", {"bits", "x86-only", "cross-bitness", "mem", "slow", "robust", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, bits::AddrChecks::large_address_aware_off});
    guarded(suite, {"sanity.bits.wow64.icd_loading", {"bits", "x86-only", "dxgi", "vulkan", "opencl", "gl"}, false, 240, bits::AddrChecks::icd_loading});
    guarded(suite, {"sanity.bits.wow64.exit_cleanup", {"bits", "x86-only", "mem", "robust", "d3d11", "d3d12", "vulkan", "opencl", "exclusive"}, true, 900, bits::AddrChecks::exit_cleanup});
    guarded(suite, {"sanity.bits.parity.error_codes", {"bits", "cross-bitness", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, bits::ParityChecks::error_codes});
    guarded(suite, {"sanity.reg.cl.buffer_alive_while_in_use", {"reg", "opencl"}, false, 120, reg::RegChecks::cl_buffer_alive_while_in_use});
    guarded(suite, {"sanity.reg.d3d12.caps_enum_defined", {"reg", "d3d12"}, false, 60, reg::RegChecks::d3d12_caps_enum_defined});
    guarded(suite, {"sanity.reg.stats.transfer_counts_plausible", {"reg", "d3d11"}, false, 120, reg::RegChecks::stats_transfer_counts_plausible});
    guarded(suite, {"sanity.reg.upload.large_not_slower_than_small", {"reg", "d3d11", "exclusive"}, true, 120, reg::RegChecks::upload_large_not_slower_than_small});
    guarded(suite, {"sanity.reg.dwm.output_not_toggled", {"reg", "dwm", "gdi", "exclusive", "pacing"}, true, 120, reg::RegChecks::dwm_output_not_toggled});
    guarded(suite, {"sanity.reg.gl.core_context_works", {"reg", "gl"}, false, 120, reg::RegChecks::gl_core_context_works});
    guarded(suite, {"sanity.reg.objects.many_objects_work", {"reg", "mem", "slow", "d3d11", "d3d12", "vulkan", "opencl"}, false, 1200, reg::RegChecks::objects_many_objects_work});
    guarded(suite, {"sanity.reg.gl.legacy_context_version_string", {"reg", "gl"}, false, 60, reg::RegChecks::gl_legacy_context_version_string});
    guarded(suite, {"sanity.lsw.mirror.launched_app_appears", {"lsw", "gdi"}, false, 120, lsw::LswChecks::launched_app_appears});
    guarded(suite, {"sanity.lsw.mirror.relaunching_app", {"lsw", "gdi", "exclusive"}, true, 120, lsw::LswChecks::relaunching_app});
    guarded(suite, {"sanity.lsw.mirror.frames_equal_guest", {"lsw", "capture", "wgc", "d3d11", "present", "dwm", "exclusive", "pacing"}, true, 240, lsw::LswChecks::frames_equal_guest});
    guarded(suite, {"sanity.lsw.window.maximize_work_area", {"lsw", "gdi", "dwm", "exclusive"}, true, 120, lsw::LswChecks::maximize_work_area});
    guarded(suite, {"sanity.lsw.window.snap_restore", {"lsw", "gdi", "dwm", "exclusive"}, true, 120, lsw::LswChecks::snap_restore});
    guarded(suite, {"sanity.lsw.window.move_resize_guest_follows", {"lsw", "gdi", "dwm", "exclusive"}, true, 120, lsw::LswChecks::move_resize_guest_follows});
    guarded(suite, {"sanity.lsw.input.keyboard_to_right_window", {"lsw", "gdi", "exclusive"}, true, 120, lsw::LswChecks::keyboard_to_right_window});
    guarded(suite, {"sanity.lsw.input.mouse_position_mapping", {"lsw", "gdi", "exclusive"}, true, 120, lsw::LswChecks::mouse_position_mapping});
    guarded(suite, {"sanity.lsw.input.focus_follows", {"lsw", "gdi", "exclusive"}, true, 120, lsw::LswChecks::focus_follows});
    guarded(suite, {"sanity.lsw.status.numbers_match_activity", {"lsw", "d3d11"}, false, 60, lsw::LswChecks::status_numbers_match_activity});
    guarded(suite, {"sanity.lsw.update.preserves_requirements", {"lsw", "env", "gdi", "exclusive"}, true, 180, lsw::LswChecks::update_preserves_requirements});
    guarded(suite, {"sanity.lsw.close.window_close_ends_app", {"lsw", "gdi"}, false, 120, lsw::LswChecks::window_close_ends_app});
    guarded(suite, {"sanity.reg.lsw.maximized_not_at_drop_point", {"lsw", "gdi", "dwm", "exclusive"}, true, 120, lsw::LswChecks::maximized_not_at_drop_point});
    guarded(suite, {"sanity.reg.lsw.first_launch_mirrored", {"lsw", "gdi"}, false, 60, lsw::LswChecks::first_launch_mirrored});
    guarded(suite, {"sanity.diag.transfer.splits", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::transfer_splits});
    guarded(suite, {"sanity.diag.transfer.arena_size", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::transfer_arena_size});
    guarded(suite, {"sanity.diag.transfer.refusals", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::transfer_refusals});
    guarded(suite, {"sanity.diag.transfer.slice_waits", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::transfer_slice_waits});
    guarded(suite, {"sanity.diag.memory.lent_pages", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::memory_lent_pages});
    guarded(suite, {"sanity.diag.memory.host_window_use", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::memory_host_window_use});
    guarded(suite, {"sanity.diag.stats.channel_counters", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::stats_channel_counters});
    guarded(suite, {"sanity.diag.registry.object_table", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::registry_object_table});
    guarded(suite, {"sanity.diag.sched.queue_depth", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::sched_queue_depth});
    guarded(suite, {"sanity.diag.latency.roundtrip_percentiles", {"diag", "measure", "d3d11", "d3d12", "vulkan", "opencl"}, false, 300, diag::DiagChecks::latency_roundtrip_percentiles});
}

}  // namespace lsw::suites::sanity
