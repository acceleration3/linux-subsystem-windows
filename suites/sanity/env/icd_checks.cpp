#include "sanity/env/icd_checks.hpp"

#include "core/child_mode.hpp"
#include "core/text.hpp"
#include "support/api_snapshot.hpp"
#include "support/av_adapters.hpp"
#include "support/child_runs.hpp"
#include "support/display_devices.hpp"
#include "support/driver_registry.hpp"
#include "support/opencl_api.hpp"
#include "support/opengl_context.hpp"
#include "support/problems.hpp"
#include "support/registry_reader.hpp"
#include "support/test_window.hpp"
#include "support/vulkan_api.hpp"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lsw::suites::sanity::env {

namespace {

std::vector<DisplayDevice> accel_devices() {
    std::vector<DisplayDevice> out;
    for (DisplayDevice& device : DisplayDevices::enumerate(true)) {
        if (device.accel) {
            out.push_back(std::move(device));
        }
    }
    return out;
}

std::vector<std::wstring> vendor_libraries(const wchar_t* key, REGSAM view) {
    std::vector<std::wstring> out;
    for (const RegistryValue& value : RegistryReader::values(key, view)) {
        if (value.number == 0) {
            out.push_back(value.name);
        }
    }
    return out;
}

void append_unique(std::vector<std::wstring>& into, const std::vector<std::wstring>& from) {
    for (const std::wstring& item : from) {
        if (std::find(into.begin(), into.end(), item) == into.end()) {
            into.push_back(item);
        }
    }
}

std::string manifest_library(const std::wstring& path) {
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    const std::string body = text.str();
    const std::size_t key = body.find("\"library_path\"");
    if (key == std::string::npos) {
        return {};
    }
    const std::size_t open = body.find('"', body.find(':', key) + 1);
    if (open == std::string::npos) {
        return {};
    }
    std::string value;
    for (std::size_t i = open + 1; i < body.size() && body[i] != '"'; ++i) {
        if (body[i] == '\\' && i + 1 < body.size()) {
            ++i;
        }
        value += body[i];
    }
    return value;
}

bool manifest_library_exists(const std::wstring& manifest, const std::string& library) {
    const std::wstring name = Text::wide(library);
    if (name.find(L':') != std::wstring::npos || name.compare(0, 2, L"\\\\") == 0) {
        return DriverRegistry::exists(name);
    }
    const std::size_t slash = manifest.find_last_of(L"\\/");
    const std::wstring dir = slash == std::wstring::npos ? L"" : manifest.substr(0, slash + 1);
    if (DriverRegistry::exists(dir + name)) {
        return true;
    }
    wchar_t found[MAX_PATH];
    return ::SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, found, nullptr) != 0;
}

void check_manifests(const std::wstring& label, const std::vector<std::wstring>& manifests,
                     Problems& problems) {
    const std::string tag = Text::utf8(label);
    if (manifests.empty()) {
        problems.add("no Vulkan driver manifest registered for " + tag);
    }
    for (const std::wstring& manifest : manifests) {
        if (!DriverRegistry::exists(manifest)) {
            problems.add(Text::utf8(manifest) + " (" + tag + ") does not exist");
            continue;
        }
        const std::string library = manifest_library(manifest);
        if (library.empty()) {
            problems.add(Text::utf8(manifest) + " has no library_path");
        } else if (!manifest_library_exists(manifest, library)) {
            problems.add(Text::utf8(manifest) + " names " + library + " which does not exist");
        }
    }
}

std::size_t accel_platform(const std::vector<OpenClPlatform>& platforms,
                           const std::vector<AvAdapter>& adapters) {
    for (std::size_t p = 0; p < platforms.size(); ++p) {
        for (const OpenClDevice& device : platforms[p].devices) {
            if (device.luid_valid && AvAdapters::find(adapters, AvAdapters::key(device.luid)) != nullptr) {
                return p;
            }
        }
    }
    return 0;
}

bool gl_strings(std::string& vendor, std::string& renderer, std::string& version,
                std::string& error) {
    TestWindow window;
    if (!window.create({}, error)) {
        return false;
    }
    OpenGlContext gl;
    if (!gl.create(window.dc(), error)) {
        return false;
    }
    vendor = gl.vendor();
    renderer = gl.renderer();
    version = gl.version();
    return true;
}

}  // namespace

Outcome IcdChecks::opencl_registry(Context& context, int bitness) {
    if (context.bitness() != bitness) {
        return Context::skip(std::string(bitness == 64 ? "x64" : "x86") + " only");
    }
    Problems problems;
    const std::vector<DisplayDevice> devices = accel_devices();
    std::vector<std::wstring> libraries =
        vendor_libraries(L"SOFTWARE\\Khronos\\OpenCL\\Vendors", RegistryReader::own_view());
    for (const DisplayDevice& device : devices) {
        append_unique(libraries, DriverRegistry::read(device).opencl_own);
    }
    if (libraries.empty()) {
        problems.add("no OpenCL ICD library is registered for this bitness");
    }
    for (const std::wstring& raw : libraries) {
        const std::wstring path = DriverRegistry::resolve(raw, true);
        std::string library = Text::utf8(path);
        HMODULE module = ::LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!DriverRegistry::exists(path) && module == nullptr) {
            problems.add(library + " does not exist");
        } else if (module == nullptr) {
            problems.add(library + " does not load (error " + std::to_string(::GetLastError()) + ")");
        } else {
            ::FreeLibrary(module);
        }
    }
    OpenClApi api;
    std::string error;
    std::vector<OpenClPlatform> platforms;
    if (!api.load(error) || !api.platforms(platforms, error)) {
        problems.add(error);
        return problems.outcome();
    }
    std::vector<AvAdapter> adapters;
    static_cast<void>(AvAdapters::accel(adapters, error));
    if (platforms.empty()) {
        problems.add("clGetPlatformIDs returns no platform");
        return problems.outcome();
    }
    for (const OpenClPlatform& platform : platforms) {
        if (platform.name.empty() || platform.vendor.empty() || platform.version.empty()) {
            problems.add("a platform has an empty name, vendor or version");
        }
    }
    const OpenClPlatform& chosen = platforms[accel_platform(platforms, adapters)];
    context.measure("platform.name", chosen.name);
    context.measure("platform.vendor", chosen.vendor);
    context.measure("platform.version", chosen.version);
    context.measure("platform.count", platforms.size());
    if (bitness == 32) {
        std::vector<std::wstring> other = vendor_libraries(L"SOFTWARE\\Khronos\\OpenCL\\Vendors",
                                                           RegistryReader::kView64);
        for (const DisplayDevice& device : devices) {
            append_unique(other, DriverRegistry::read(device).opencl_other);
        }
        if (other.empty()) {
            problems.add("no 64-bit OpenCL ICD library is registered");
        }
        for (const std::wstring& raw : other) {
            if (!DriverRegistry::exists(DriverRegistry::resolve(raw, false))) {
                problems.add(Text::utf8(raw) + " (64-bit) does not exist");
            }
        }
        CheckResult sibling;
        if (!ChildRuns::run_sibling("sanity.env.icd.opencl_registry_x64", {}, sibling, error)) {
            problems.add(error);
        } else if (ChildRuns::text(sibling, "platform.name") != chosen.name ||
                   ChildRuns::text(sibling, "platform.vendor") != chosen.vendor) {
            problems.add("64-bit platform '" + ChildRuns::text(sibling, "platform.name") + "' / '" +
                         ChildRuns::text(sibling, "platform.vendor") + "' differs from 32-bit '" +
                         chosen.name + "' / '" + chosen.vendor + "'");
        }
    }
    return problems.outcome();
}

Outcome IcdChecks::opencl_registry_x64(Context& context) {
    return opencl_registry(context, 64);
}

Outcome IcdChecks::opencl_registry_x86(Context& context) {
    return opencl_registry(context, 32);
}

Outcome IcdChecks::vulkan_manifest_both(Context& context) {
    Problems problems;
    VulkanApi api;
    std::string error;
    std::vector<VulkanDevice> devices;
    std::uint32_t version = 0;
    if (!api.load(error) || !api.instance_version(version, error) || !api.create_instance(error) ||
        !api.devices(devices, error)) {
        problems.add(error);
    } else if (devices.empty()) {
        problems.add("vkEnumeratePhysicalDevices returns no device");
    }
    context.measure("loader.version", version);
    context.measure("physical.devices", devices.size());

    const std::vector<DisplayDevice> display = accel_devices();
    std::vector<std::wstring> own =
        vendor_libraries(L"SOFTWARE\\Khronos\\Vulkan\\Drivers", RegistryReader::own_view());
    std::vector<std::wstring> other = vendor_libraries(
        L"SOFTWARE\\Khronos\\Vulkan\\Drivers",
        RegistryReader::own_view() == RegistryReader::kView64 ? RegistryReader::kView32
                                                              : RegistryReader::kView64);
    for (const DisplayDevice& device : display) {
        const DriverFiles files = DriverRegistry::read(device);
        append_unique(own, files.vulkan_own);
        append_unique(other, files.vulkan_other);
    }
    check_manifests(sizeof(void*) == 8 ? L"64-bit" : L"32-bit", own, problems);
    check_manifests(sizeof(void*) == 8 ? L"32-bit" : L"64-bit", other, problems);

    if (!ChildMode::active()) {
        CheckResult sibling;
        if (!ChildRuns::run_sibling("sanity.env.icd.vulkan_manifest_both", {}, sibling, error)) {
            problems.add(error);
        } else if (ChildRuns::number(sibling, "physical.devices") < 1) {
            problems.add("the other bitness enumerates no Vulkan physical device");
        }
    }
    return problems.outcome();
}

Outcome IcdChecks::opengl_icd_both(Context& context) {
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string error;
    if (!gl_strings(vendor, renderer, version, error)) {
        return Outcome::fail(error);
    }
    context.measure("gl.vendor", vendor);
    context.measure("gl.renderer", renderer);
    context.measure("gl.version", version);
    Problems problems;
    if (vendor.empty() || renderer.empty()) {
        problems.add("GL_VENDOR or GL_RENDERER is empty");
    }
    if (renderer == "GDI Generic") {
        problems.add("the context is the Microsoft software fallback, the ICD did not load");
    }
    if (!ChildMode::active()) {
        CheckResult sibling;
        if (!ChildRuns::run_sibling("sanity.env.icd.opengl_icd_both", {}, sibling, error)) {
            problems.add(error);
        } else if (ChildRuns::text(sibling, "gl.vendor") != vendor ||
                   ChildRuns::text(sibling, "gl.renderer") != renderer) {
            problems.add("other bitness reports '" + ChildRuns::text(sibling, "gl.vendor") + "' / '" +
                         ChildRuns::text(sibling, "gl.renderer") + "', this one '" + vendor + "' / '" +
                         renderer + "'");
        }
    }
    return problems.outcome();
}

Outcome IcdChecks::loader_independent_of_cwd(Context& context) {
    ApiSnapshot snapshot;
    std::string error;
    if (!ApiSnapshot::take(snapshot, error)) {
        return Outcome::fail(error);
    }
    std::string vendor;
    std::string renderer;
    std::string version;
    if (!gl_strings(vendor, renderer, version, error)) {
        return Outcome::fail(error);
    }
    const std::string discovery =
        snapshot.vulkan + snapshot.opencl + "gl vendor=" + vendor + " renderer=" + renderer + "\n";
    context.measure("discovery", discovery);
    if (ChildMode::active()) {
        return Outcome::ok();
    }
    std::error_code code;
    const std::filesystem::path decoys =
        std::filesystem::temp_directory_path() / ("lsw-suites-decoys-" + std::to_string(::GetCurrentProcessId()));
    std::filesystem::create_directories(decoys, code);
    for (const char* name : {"OpenCL.dll", "vulkan-1.dll", "opengl32.dll"}) {
        std::ofstream(decoys / name, std::ios::binary).close();
    }
    const std::pair<const char*, std::filesystem::path> directories[] = {
        {"the parent's directory", std::filesystem::current_path()},
        {"the temp directory", std::filesystem::temp_directory_path()},
        {"a directory with decoy loaders", decoys},
    };
    Problems problems;
    for (const auto& [label, directory] : directories) {
        ChildRunOptions options;
        options.cwd = directory;
        const CheckResult result = ChildRuns::run_self("sanity.env.icd.loader_independent_of_cwd", options);
        if (ChildRuns::text(result, "discovery") != discovery) {
            problems.add(std::string("discovery from ") + label + " differs: " +
                         (result.status == Status::Ok ? ChildRuns::text(result, "discovery") : result.reason));
        }
    }
    std::filesystem::remove_all(decoys, code);
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::env
