#include "support/cl_rig.hpp"

#include "support/gl_identity.hpp"
#include "support/target_adapter.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace lsw::suites {

namespace {

std::string text_param(const ClApi& api, cl_device_id device, cl_device_info param) {
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

struct Candidate {
    cl_platform_id platform = nullptr;
    cl_device_id device = nullptr;
    std::string name;
    std::uint32_t vendor_id = 0;
    bool luid_valid = false;
    LUID luid{};
    bool claimed = false;
};

}  // namespace

ClRig::~ClRig() {
    if (api.Finish != nullptr) {
        drain();
    }
    if (queue != nullptr) {
        api.ReleaseCommandQueue(queue);
    }
    if (context != nullptr) {
        api.ReleaseContext(context);
    }
}

bool ClRig::list_devices(ClApi& api, ClDeviceList& out, std::string& error) {
    if (api.GetPlatformIDs == nullptr && !api.load(error)) {
        return false;
    }
    std::vector<AvAdapter> adapters;
    const bool any = std::getenv("LSW_SUITES_DEV_ANY") != nullptr;
    if (any) {
        std::vector<AvAdapter> all;
        if (!AvAdapters::all(all, error)) {
            return false;
        }
        for (AvAdapter& adapter : all) {
            if (!adapter.software) {
                adapters.push_back(std::move(adapter));
            }
        }
        if (adapters.empty()) {
            error = "no hardware adapter";
            return false;
        }
    } else if (!AvAdapters::accel(adapters, error)) {
        return false;
    }
    out.adapters = adapters.size();
    cl_uint platform_count = 0;
    const cl_int listed = api.GetPlatformIDs(0, nullptr, &platform_count);
    if (listed != CL_SUCCESS || platform_count == 0) {
        error = "clGetPlatformIDs reports no platform: " + ClTools::error_name(listed);
        return false;
    }
    std::vector<cl_platform_id> platforms(platform_count);
    if (api.GetPlatformIDs(platform_count, platforms.data(), nullptr) != CL_SUCCESS) {
        error = "clGetPlatformIDs (fill) failed";
        return false;
    }
    std::vector<Candidate> candidates;
    for (cl_platform_id platform : platforms) {
        cl_uint count = 0;
        if (api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, nullptr, &count) != CL_SUCCESS || count == 0) {
            continue;
        }
        std::vector<cl_device_id> devices(count);
        if (api.GetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, count, devices.data(), nullptr) != CL_SUCCESS) {
            continue;
        }
        for (cl_device_id device : devices) {
            Candidate candidate;
            candidate.platform = platform;
            candidate.device = device;
            candidate.name = text_param(api, device, CL_DEVICE_NAME);
            cl_uint vendor = 0;
            api.GetDeviceInfo(device, CL_DEVICE_VENDOR_ID, sizeof(vendor), &vendor, nullptr);
            candidate.vendor_id = vendor;
            cl_bool valid = CL_FALSE;
            cl_uchar bytes[CL_LUID_SIZE_KHR] = {};
            if (api.GetDeviceInfo(device, CL_DEVICE_LUID_VALID_KHR, sizeof(valid), &valid, nullptr) == CL_SUCCESS && valid != CL_FALSE &&
                api.GetDeviceInfo(device, CL_DEVICE_LUID_KHR, sizeof(bytes), bytes, nullptr) == CL_SUCCESS) {
                candidate.luid_valid = true;
                std::memcpy(&candidate.luid, bytes, sizeof(candidate.luid));
            }
            candidates.push_back(std::move(candidate));
        }
    }
    for (const AvAdapter& adapter : adapters) {
        for (Candidate& candidate : candidates) {
            if (!candidate.claimed && candidate.luid_valid && AvAdapters::key(candidate.luid) == adapter.luid_key()) {
                candidate.claimed = true;
                out.refs.push_back({candidate.platform, candidate.device, candidate.name, adapter, true});
                break;
            }
        }
    }
    const bool any_luid = std::any_of(candidates.begin(), candidates.end(), [](const Candidate& c) { return c.luid_valid; });
    if (!any_luid) {
        for (const AvAdapter& adapter : adapters) {
            for (Candidate& candidate : candidates) {
                if (!candidate.claimed && candidate.vendor_id == adapter.vendor &&
                    GlIdentity::names_match(candidate.name, adapter.name)) {
                    candidate.claimed = true;
                    out.refs.push_back({candidate.platform, candidate.device, candidate.name, adapter, false});
                    break;
                }
            }
        }
    }
    if (out.refs.empty() && any && !candidates.empty()) {
        out.refs.push_back({candidates.front().platform, candidates.front().device, candidates.front().name, adapters.front(), false});
    }
    if (out.refs.empty()) {
        error = "no OpenCL device matches an accel-virt adapter by CL_DEVICE_LUID_KHR or by vendor and name (" +
                std::to_string(candidates.size()) + " OpenCL device(s) seen)";
        return false;
    }
    return true;
}

bool ClRig::open(const ClRigOptions& options, std::string& error) {
    ClDeviceList list;
    if (!list_devices(api, list, error)) {
        return false;
    }
    std::size_t index = 0;
    if (options.device >= 0) {
        if (static_cast<std::size_t>(options.device) >= list.refs.size()) {
            error = "device index " + std::to_string(options.device) + " is out of range";
            return false;
        }
        index = static_cast<std::size_t>(options.device);
    } else {
        AvAdapter target;
        std::string pick_error;
        if (TargetAdapter::pick(target, pick_error)) {
            for (std::size_t i = 0; i < list.refs.size(); ++i) {
                if (list.refs[i].adapter.luid_key() == target.luid_key()) {
                    index = i;
                    break;
                }
            }
        }
    }
    return open_ref(list.refs[index], options, error);
}

bool ClRig::open_ref(const ClDeviceRef& selected, const ClRigOptions& options, std::string& error) {
    if (api.GetPlatformIDs == nullptr && !api.load(error)) {
        return false;
    }
    ref = selected;
    platform = ref.platform;
    device = ref.device;
    std::string version_text = text_info(CL_DEVICE_VERSION);
    if (!ClTools::parse_version(version_text, "OpenCL ", version)) {
        error = "CL_DEVICE_VERSION '" + version_text + "' is not in the form 'OpenCL x.y'";
        return false;
    }
    extensions = text_info(CL_DEVICE_EXTENSIONS);
    profile = text_info(CL_DEVICE_PROFILE);
    if (!options.context) {
        return true;
    }
    cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platform), 0};
    cl_int code = CL_SUCCESS;
    context = api.CreateContext(properties, 1, &device, nullptr, nullptr, &code);
    if (context == nullptr || code != CL_SUCCESS) {
        error = "clCreateContext failed: " + ClTools::error_name(code);
        context = nullptr;
        return false;
    }
    queue = create_queue(options.profiling ? CL_QUEUE_PROFILING_ENABLE : 0, code);
    if (queue == nullptr) {
        error = "creating the command queue failed: " + ClTools::error_name(code);
        return false;
    }
    return true;
}

cl_command_queue ClRig::create_queue(cl_command_queue_properties properties, cl_int& code, cl_device_id for_device) {
    cl_device_id target = for_device != nullptr ? for_device : device;
    cl_command_queue created = nullptr;
    if (version.at_least(2, 0) && api.CreateCommandQueueWithProperties != nullptr) {
        const cl_queue_properties list[] = {CL_QUEUE_PROPERTIES, properties, 0};
        created = api.CreateCommandQueueWithProperties(context, target, properties != 0 ? list : nullptr, &code);
    } else {
        created = api.CreateCommandQueue(context, target, properties, &code);
    }
    if (created == nullptr) {
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.push_back(created);
    return created;
}

void ClRig::forget_queue(cl_command_queue forgotten) {
    std::lock_guard<std::mutex> lock(mutex_);
    queues_.erase(std::remove(queues_.begin(), queues_.end(), forgotten), queues_.end());
}

void ClRig::drain() {
    std::vector<cl_command_queue> copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy = queues_;
    }
    for (cl_command_queue pending : copy) {
        api.Finish(pending);
    }
}

cl_uint ClRig::uint_info(cl_device_info param) const {
    cl_uint value = 0;
    api.GetDeviceInfo(device, param, sizeof(value), &value, nullptr);
    return value;
}

cl_ulong ClRig::ulong_info(cl_device_info param) const {
    cl_ulong value = 0;
    api.GetDeviceInfo(device, param, sizeof(value), &value, nullptr);
    return value;
}

std::size_t ClRig::size_info(cl_device_info param) const {
    std::size_t value = 0;
    api.GetDeviceInfo(device, param, sizeof(value), &value, nullptr);
    return value;
}

std::string ClRig::text_info(cl_device_info param) const {
    return text_param(api, device, param);
}

}  // namespace lsw::suites
