#include "sanity/cl/platform_checks.hpp"

#include "sanity/cl/kit.hpp"

#include <algorithm>
#include <set>

namespace lsw::suites::sanity::cl {

namespace {

std::string platform_text(const ClApi& api, cl_platform_id platform, cl_platform_info param, cl_int& code) {
    std::size_t size = 0;
    code = api.GetPlatformInfo(platform, param, 0, nullptr, &size);
    if (code != CL_SUCCESS) {
        return {};
    }
    std::vector<char> blob(size);
    code = api.GetPlatformInfo(platform, param, size, blob.data(), nullptr);
    if (code != CL_SUCCESS) {
        return {};
    }
    return ClTools::text_of(blob);
}

std::string device_text(const ClApi& api, cl_device_id device, cl_device_info param) {
    std::size_t size = 0;
    if (api.GetDeviceInfo(device, param, 0, nullptr, &size) != CL_SUCCESS || size == 0) {
        return {};
    }
    std::vector<char> blob(size);
    if (api.GetDeviceInfo(device, param, size, blob.data(), nullptr) != CL_SUCCESS) {
        return {};
    }
    return ClTools::text_of(blob);
}

bool version_string(const std::string& text, ClVersion& version) {
    std::string rest;
    return ClTools::parse_version(text, "OpenCL ", version, &rest) && (rest.empty() || rest[0] == ' ');
}

ClVersion decode_numeric(cl_version value) {
    ClVersion version;
    version.major = static_cast<int>(value >> 22);
    version.minor = static_cast<int>((value >> 12) & 0x3ff);
    return version;
}

}  // namespace

Outcome PlatformChecks::query_consistent(Context& context) {
    ClApi api;
    std::string error;
    if (!api.load(error)) {
        return Kit::open_failed(error);
    }
    Problems problems;
    cl_uint count = 0;
    cl_int code = api.GetPlatformIDs(0, nullptr, &count);
    if (code != CL_SUCCESS || count == 0) {
        return Outcome::fail("clGetPlatformIDs(0, NULL, &count) returned " + ClTools::error_name(code) + " with " + std::to_string(count) + " platform(s)");
    }
    Kit::expect(problems, "clGetPlatformIDs(0, NULL, NULL)", api.GetPlatformIDs(0, nullptr, nullptr), {CL_INVALID_VALUE});
    cl_platform_id probe = nullptr;
    Kit::expect(problems, "clGetPlatformIDs(1, NULL, NULL)", api.GetPlatformIDs(1, nullptr, nullptr), {CL_INVALID_VALUE});
    Kit::expect(problems, "clGetPlatformIDs(0, &id, NULL)", api.GetPlatformIDs(0, &probe, nullptr), {CL_INVALID_VALUE});
    std::vector<cl_platform_id> platforms(count);
    code = api.GetPlatformIDs(count, platforms.data(), nullptr);
    if (code != CL_SUCCESS) {
        return Outcome::fail("clGetPlatformIDs(" + std::to_string(count) + ", ids) returned " + ClTools::error_name(code));
    }
    for (std::size_t p = 0; p < platforms.size(); ++p) {
        const cl_platform_id platform = platforms[p];
        const std::string tag = "platform " + std::to_string(p);
        cl_int info_code = CL_SUCCESS;
        const std::string profile = platform_text(api, platform, CL_PLATFORM_PROFILE, info_code);
        if (info_code != CL_SUCCESS || (profile != "FULL_PROFILE" && profile != "EMBEDDED_PROFILE")) {
            problems.add(tag + ": CL_PLATFORM_PROFILE is '" + profile + "' (" + ClTools::error_name(info_code) + ")");
        }
        const std::string version_text = platform_text(api, platform, CL_PLATFORM_VERSION, info_code);
        ClVersion version;
        if (info_code != CL_SUCCESS || !version_string(version_text, version)) {
            problems.add(tag + ": CL_PLATFORM_VERSION '" + version_text + "' is not 'OpenCL <major>.<minor> <information>'");
            continue;
        }
        const std::string name = platform_text(api, platform, CL_PLATFORM_NAME, info_code);
        if (info_code != CL_SUCCESS || name.empty()) {
            problems.add(tag + ": CL_PLATFORM_NAME is empty (" + ClTools::error_name(info_code) + ")");
        }
        const std::string vendor = platform_text(api, platform, CL_PLATFORM_VENDOR, info_code);
        if (info_code != CL_SUCCESS || vendor.empty()) {
            problems.add(tag + ": CL_PLATFORM_VENDOR is empty (" + ClTools::error_name(info_code) + ")");
        }
        const std::string extensions = platform_text(api, platform, CL_PLATFORM_EXTENSIONS, info_code);
        if (info_code != CL_SUCCESS) {
            problems.add(tag + ": CL_PLATFORM_EXTENSIONS returned " + ClTools::error_name(info_code));
        }
        std::set<std::string> platform_extensions;
        for (const std::string& word : ClTools::split(extensions)) {
            if (!platform_extensions.insert(word).second) {
                problems.add(tag + ": extension " + word + " is listed twice");
            }
            if (word.compare(0, 3, "cl_") != 0 && word.compare(0, 5, "cles_") != 0) {
                problems.add(tag + ": extension name '" + word + "' does not start with cl_ or cles_");
            }
        }
        if (platform_extensions.count("cl_khr_icd") == 0) {
            problems.add(tag + ": a platform returned by the ICD loader does not list cl_khr_icd");
        } else {
            const std::string suffix = platform_text(api, platform, CL_PLATFORM_ICD_SUFFIX_KHR, info_code);
            if (info_code != CL_SUCCESS || suffix.empty()) {
                problems.add(tag + ": CL_PLATFORM_ICD_SUFFIX_KHR is empty (" + ClTools::error_name(info_code) + ")");
            }
        }
        Kit::expect(problems, tag + " clGetPlatformInfo(invalid parameter)", api.GetPlatformInfo(platform, 0x7777, 0, nullptr, nullptr), {CL_INVALID_VALUE});
        char tiny = 0;
        Kit::expect(problems, tag + " clGetPlatformInfo(CL_PLATFORM_VERSION, size 1)", api.GetPlatformInfo(platform, CL_PLATFORM_VERSION, 1, &tiny, nullptr), {CL_INVALID_VALUE});
        if (version.at_least(3, 0)) {
            cl_version numeric = 0;
            code = api.GetPlatformInfo(platform, CL_PLATFORM_NUMERIC_VERSION, sizeof(numeric), &numeric, nullptr);
            if (code != CL_SUCCESS) {
                problems.add(tag + ": CL_PLATFORM_NUMERIC_VERSION returned " + ClTools::error_name(code));
            } else {
                const ClVersion decoded = decode_numeric(numeric);
                if (decoded.major != version.major || decoded.minor != version.minor) {
                    problems.add(tag + ": CL_PLATFORM_NUMERIC_VERSION is " + decoded.text() + " but the version string says " + version.text());
                }
            }
            std::size_t size = 0;
            code = api.GetPlatformInfo(platform, CL_PLATFORM_EXTENSIONS_WITH_VERSION, 0, nullptr, &size);
            if (code != CL_SUCCESS || size % sizeof(cl_name_version) != 0) {
                problems.add(tag + ": CL_PLATFORM_EXTENSIONS_WITH_VERSION size query returned " + ClTools::error_name(code) + " with " + std::to_string(size) + " bytes");
            } else {
                std::vector<cl_name_version> versioned(size / sizeof(cl_name_version));
                if (api.GetPlatformInfo(platform, CL_PLATFORM_EXTENSIONS_WITH_VERSION, size, versioned.data(), nullptr) == CL_SUCCESS) {
                    std::set<std::string> names;
                    for (const cl_name_version& entry : versioned) {
                        names.insert(std::string(entry.name, strnlen(entry.name, CL_NAME_VERSION_MAX_NAME_SIZE)));
                    }
                    if (names != platform_extensions) {
                        problems.add(tag + ": CL_PLATFORM_EXTENSIONS_WITH_VERSION lists " + std::to_string(names.size()) + " extension(s) but CL_PLATFORM_EXTENSIONS lists " + std::to_string(platform_extensions.size()));
                    }
                }
            }
        }
        Kit::expect(problems, tag + " clGetDeviceIDs(invalid type)", api.GetDeviceIDs(platform, 0xdead0000u, 0, nullptr, &count), {CL_INVALID_DEVICE_TYPE});
        cl_device_id one = nullptr;
        Kit::expect(problems, tag + " clGetDeviceIDs(0 entries, devices set)", api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, &one, nullptr), {CL_INVALID_VALUE});
        Kit::expect(problems, tag + " clGetDeviceIDs(no output)", api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 1, nullptr, nullptr), {CL_INVALID_VALUE});
        cl_uint device_count = 0;
        code = api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &device_count);
        if (code != CL_SUCCESS || device_count == 0) {
            problems.add(tag + ": clGetDeviceIDs(ALL) returned " + ClTools::error_name(code) + " with " + std::to_string(device_count) + " device(s)");
            continue;
        }
        std::vector<cl_device_id> devices(device_count);
        if (api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, device_count, devices.data(), nullptr) != CL_SUCCESS) {
            problems.add(tag + ": clGetDeviceIDs(ALL, fill) failed");
            continue;
        }
        for (std::size_t d = 0; d < devices.size(); ++d) {
            const std::string device_tag = tag + " device " + std::to_string(d);
            const std::string device_version_text = device_text(api, devices[d], CL_DEVICE_VERSION);
            ClVersion device_version;
            if (!version_string(device_version_text, device_version)) {
                problems.add(device_tag + ": CL_DEVICE_VERSION '" + device_version_text + "' is not 'OpenCL <major>.<minor> <information>'");
                continue;
            }
            if (version < device_version) {
                problems.add(device_tag + ": device version " + device_version.text() + " is above the platform version " + version.text());
            }
            cl_platform_id owner = nullptr;
            api.GetDeviceInfo(devices[d], CL_DEVICE_PLATFORM, sizeof(owner), &owner, nullptr);
            if (owner != platform) {
                problems.add(device_tag + ": CL_DEVICE_PLATFORM names a different platform");
            }
            const std::string device_extensions = device_text(api, devices[d], CL_DEVICE_EXTENSIONS);
            for (const std::string& word : platform_extensions) {
                if (!ClTools::has_word(device_extensions, word)) {
                    problems.add(device_tag + ": platform extension " + word + " is missing from CL_DEVICE_EXTENSIONS");
                }
            }
            if (device_version.at_least(3, 0)) {
                cl_version numeric = 0;
                if (api.GetDeviceInfo(devices[d], CL_DEVICE_NUMERIC_VERSION, sizeof(numeric), &numeric, nullptr) != CL_SUCCESS) {
                    problems.add(device_tag + ": CL_DEVICE_NUMERIC_VERSION query failed");
                } else {
                    const ClVersion decoded = decode_numeric(numeric);
                    if (decoded.major != device_version.major || decoded.minor != device_version.minor) {
                        problems.add(device_tag + ": CL_DEVICE_NUMERIC_VERSION is " + decoded.text() + " but the version string says " + device_version.text());
                    }
                }
            }
        }
        context.measure("cl.platform" + std::to_string(p) + ".version", version_text);
    }
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::cl
