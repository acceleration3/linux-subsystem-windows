#include "sanity/err/unsupported_checks.hpp"

#include "sanity/cl/device_checks.hpp"
#include "sanity/cl/platform_checks.hpp"
#include "sanity/d3d11/fl_checks.hpp"
#include "sanity/d3d12/dev_checks.hpp"
#include "sanity/d3d12/opt_checks.hpp"
#include "sanity/err/delegate.hpp"
#include "sanity/gl/ext_checks.hpp"
#include "sanity/vk/instance_checks.hpp"
#include "support/cl_rig.hpp"
#include "support/cl_tools.hpp"
#include "support/problems.hpp"

#include <cstdio>

namespace lsw::suites::sanity::err {

namespace {

bool parse_version(const std::string& text, int& major, int& minor) {
    return std::sscanf(text.c_str(), "OpenCL %d.%d", &major, &minor) == 2;
}

Outcome device_platform_versions(Context&) {
    ClApi api;
    std::string error;
    ClDeviceList list;
    if (!api.load(error) || !ClRig::list_devices(api, list, error) || list.refs.empty()) {
        return Context::skip("OpenCL is unavailable: " + error);
    }
    Problems problems;
    for (const ClDeviceRef& ref : list.refs) {
        char device_text[256] = {};
        char platform_text[256] = {};
        api.GetDeviceInfo(ref.device, CL_DEVICE_VERSION, sizeof(device_text), device_text, nullptr);
        api.GetPlatformInfo(ref.platform, CL_PLATFORM_VERSION, sizeof(platform_text), platform_text, nullptr);
        int dm = 0;
        int dn = 0;
        int pm = 0;
        int pn = 0;
        if (!parse_version(device_text, dm, dn) || !parse_version(platform_text, pm, pn)) {
            problems.add(ref.name + ": the version strings '" + device_text + "' and '" + platform_text + "' do not start with OpenCL major.minor");
            continue;
        }
        if (dm * 100 + dn > pm * 100 + pn) {
            problems.add(ref.name + ": CL_DEVICE_VERSION " + std::to_string(dm) + "." + std::to_string(dn) + " is above CL_PLATFORM_VERSION " + std::to_string(pm) + "." + std::to_string(pn));
        }
        std::size_t size = 0;
        const cl_int all_versions = api.GetDeviceInfo(ref.device, CL_DEVICE_OPENCL_C_ALL_VERSIONS, 0, nullptr, &size);
        const cl_int ext_versions = api.GetDeviceInfo(ref.device, CL_DEVICE_EXTENSIONS_WITH_VERSION, 0, nullptr, &size);
        if (dm >= 3 && (all_versions != CL_SUCCESS || ext_versions != CL_SUCCESS)) {
            problems.add(ref.name + ": a 3.0 device answered " + ClTools::error_name(all_versions) + " and " + ClTools::error_name(ext_versions) + " for the versioned queries");
        }
        if (dm < 3 && all_versions == CL_SUCCESS && dm < 2) {
            problems.add(ref.name + ": a pre-2.0 device answers the OpenCL 3.0 versioned query");
        }
        char extensions[8192] = {};
        api.GetDeviceInfo(ref.device, CL_DEVICE_EXTENSIONS, sizeof(extensions), extensions, nullptr);
        const std::string list_text = std::string(" ") + extensions + " ";
        if (dm * 100 + dn >= 200 && list_text.find(" cl_khr_il_program ") == std::string::npos && api.GetDeviceInfo(ref.device, CL_DEVICE_IL_VERSION, 0, nullptr, &size) == CL_SUCCESS && size > 1) {
            problems.add(ref.name + ": CL_DEVICE_IL_VERSION lists a language but cl_khr_il_program is not advertised");
        }
    }
    return problems.outcome();
}

}  // namespace

Outcome UnsupportedChecks::caps_match_use(Context& context) {
    return Delegate::all(context, {d3d11::FlChecks::checkfeaturesupport_options, d3d12::DevChecks::options_consistent, d3d12::OptChecks::raytracing,
                                   d3d12::OptChecks::mesh_shaders, d3d12::OptChecks::vrs_sampler_feedback, d3d12::OptChecks::work_graphs_dxr11,
                                   vk::InstanceChecks::props_features_consistent});
}

Outcome UnsupportedChecks::formats(Context& context) {
    return Delegate::all(context, {d3d11::FlChecks::format_support_vs_creation, d3d12::DevChecks::format_support});
}

Outcome UnsupportedChecks::extensions_list_vs_entry(Context& context) {
    return Delegate::all(context, {vk::InstanceChecks::extensions_match_functions, gl::ExtChecks::string_matches_entry_points, gl::ExtChecks::unadvertised_not_supported,
                                   cl::DeviceChecks::extensions_match_entry_points});
}

Outcome UnsupportedChecks::opencl_version_coherent(Context& context) {
    return Delegate::all(context, {cl::PlatformChecks::query_consistent, device_platform_versions});
}

}  // namespace lsw::suites::sanity::err
