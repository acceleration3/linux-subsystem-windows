#include "sanity/enumeration/identity_checks.hpp"

#include "core/text.hpp"
#include "support/av_adapters.hpp"
#include "support/display_devices.hpp"
#include "support/kmt_api.hpp"
#include "support/opencl_api.hpp"
#include "support/opengl_context.hpp"
#include "support/problems.hpp"
#include "support/test_window.hpp"
#include "support/vulkan_api.hpp"

#include <set>

namespace lsw::suites::sanity::enumeration {

namespace {

std::string key_text(std::uint64_t key) {
    return Text::hex(key >> 32, 8) + ":" + Text::hex(key & 0xffffffffu, 8);
}

std::uint64_t luid_key(const LUID& luid) {
    return AvAdapters::key(luid);
}

std::string vendor_device(std::uint32_t vendor, std::uint32_t device) {
    return Text::hex(vendor, 4) + ":" + Text::hex(device, 4);
}

bool printable(const std::wstring& text) {
    for (wchar_t c : text) {
        if (c < 0x20 || c == 0x7f || (c >= 0x80 && c <= 0x9f) || c == 0xfffd) {
            return false;
        }
    }
    return true;
}

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    std::size_t start = 0;
    while (start < text.size() && (text[start] == ' ' || text[start] == '\t')) {
        ++start;
    }
    return text.substr(start);
}

bool name_compatible(const std::string& a, std::size_t max_a, const std::string& b,
                     std::size_t max_b) {
    const std::string ua = Text::upper(trim(a));
    const std::string ub = Text::upper(trim(b));
    if (ua == ub) {
        return true;
    }
    if (ub.compare(0, ua.size(), ua) == 0) {
        return max_a != 0 && ua.size() + 1 >= max_a;
    }
    if (ua.compare(0, ub.size(), ub) == 0) {
        return max_b != 0 && ub.size() + 1 >= max_b;
    }
    return false;
}

}  // namespace

Outcome IdentityChecks::dxgi_vs_kmt_luid(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    KmtApi kmt;
    std::vector<KmtAdapter> kmt_adapters;
    if (!kmt.load(error) || !kmt.enum_adapters(kmt_adapters, error)) {
        return Outcome::fail(error);
    }
    std::set<std::uint64_t> dxgi;
    std::set<std::uint64_t> from_kmt;
    for (const AvAdapter& adapter : adapters) {
        if (!adapter.software) {
            dxgi.insert(adapter.luid_key());
        }
    }
    for (const KmtAdapter& adapter : kmt_adapters) {
        from_kmt.insert(luid_key(adapter.luid));
    }
    context.measure("dxgi.count", dxgi.size());
    context.measure("kmt.count", from_kmt.size());
    Problems problems;
    if (dxgi.empty()) {
        problems.add("DXGI lists no hardware adapter");
    }
    for (std::uint64_t key : dxgi) {
        if (from_kmt.count(key) == 0) {
            problems.add("LUID " + key_text(key) + " is in DXGI but not in D3DKMT");
        }
    }
    for (std::uint64_t key : from_kmt) {
        if (dxgi.count(key) == 0) {
            problems.add("LUID " + key_text(key) + " is in D3DKMT but not in DXGI");
        }
    }
    return problems.outcome();
}

Outcome IdentityChecks::vulkan_luid_matches(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    VulkanApi api;
    std::vector<VulkanDevice> devices;
    if (!api.load(error) || !api.create_instance(error) || !api.devices(devices, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::size_t with_luid = 0;
    for (const VulkanDevice& device : devices) {
        context.measure("vk." + device.name + ".luid_valid", device.luid_valid);
        if (!device.luid_valid) {
            continue;
        }
        ++with_luid;
        const AvAdapter* adapter = AvAdapters::find(adapters, luid_key(device.luid));
        if (adapter == nullptr) {
            problems.add(device.name + ": LUID " + key_text(luid_key(device.luid)) + " matches no DXGI adapter");
        } else if (adapter->vendor != device.vendor || adapter->device != device.device) {
            problems.add(device.name + ": Vulkan ids " + vendor_device(device.vendor, device.device) +
                         " differ from DXGI " + vendor_device(adapter->vendor, adapter->device));
        }
    }
    if (devices.empty()) {
        problems.add("Vulkan reports no physical device");
    } else if (with_luid == 0) {
        problems.add("no Vulkan physical device reports a valid LUID");
    }
    return problems.outcome();
}

Outcome IdentityChecks::opencl_luid_matches(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    OpenClApi api;
    std::vector<OpenClPlatform> platforms;
    if (!api.load(error) || !api.platforms(platforms, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::size_t devices = 0;
    for (const OpenClPlatform& platform : platforms) {
        for (const OpenClDevice& device : platform.devices) {
            ++devices;
            context.measure("cl." + device.name + ".luid_extension", device.luid_extension);
            if (device.luid_extension) {
                if (device.luid_valid_query_error != 0) {
                    problems.add(device.name + ": cl_khr_device_uuid is listed but CL_DEVICE_LUID_VALID_KHR fails with " +
                                 std::to_string(device.luid_valid_query_error));
                } else if (device.luid_valid &&
                           AvAdapters::find(adapters, luid_key(device.luid)) == nullptr) {
                    problems.add(device.name + ": LUID " + key_text(luid_key(device.luid)) +
                                 " matches no DXGI adapter");
                }
            } else if (device.luid_valid || device.luid_query_error == 0) {
                problems.add(device.name + ": reports a LUID without listing cl_khr_device_uuid");
            }
        }
    }
    if (devices == 0) {
        problems.add("OpenCL exposes no device");
    }
    return problems.outcome();
}

Outcome IdentityChecks::gl_renderer_matches(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    std::size_t tested = 0;
    for (const AvAdapter& adapter : adapters) {
        if (adapter.software) {
            continue;
        }
        for (const AvOutput& output : adapter.outputs) {
            if (!output.attached) {
                continue;
            }
            TestWindowConfig config;
            config.x = output.rect.left + 20;
            config.y = output.rect.top + 20;
            TestWindow window;
            OpenGlContext gl;
            if (!window.create(config, error) || !gl.create(window.dc(), error)) {
                problems.add(output.device_name + ": " + error);
                continue;
            }
            ++tested;
            const std::string renderer = gl.renderer();
            context.measure(output.device_name + ".renderer", renderer);
            const std::string upper_renderer = Text::upper(renderer);
            const std::string upper_name = Text::upper(trim(adapter.name));
            if (renderer.empty() || (upper_renderer.find(upper_name) == std::string::npos &&
                                     upper_name.find(upper_renderer) == std::string::npos)) {
                problems.add(output.device_name + ": GL_RENDERER '" + renderer + "' does not name DXGI adapter '" +
                             adapter.name + "'");
            }
        }
    }
    if (tested == 0 && problems.empty()) {
        problems.add("no attached output on a hardware adapter to test");
    }
    return problems.outcome();
}

Outcome IdentityChecks::setupapi_matches(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    KmtApi kmt;
    if (!kmt.load(error)) {
        return Outcome::fail(error);
    }
    const std::vector<DisplayDevice> devices = DisplayDevices::enumerate(true);
    Problems problems;
    std::size_t checked = 0;
    for (const AvAdapter& adapter : adapters) {
        if (adapter.software) {
            continue;
        }
        ++checked;
        KmtAddress address;
        if (!kmt.address(adapter.luid, address)) {
            problems.add(adapter.name + ": D3DKMT cannot report the adapter's bus address");
            continue;
        }
        const DisplayDevice* found = nullptr;
        for (const DisplayDevice& device : devices) {
            if (device.bus == address.bus && (device.address >> 16) == address.device &&
                (device.address & 0xffff) == address.function) {
                found = &device;
                break;
            }
        }
        if (found == nullptr) {
            problems.add(adapter.name + ": no SetupAPI display device at bus " + std::to_string(address.bus) +
                         " device " + std::to_string(address.device) + " function " +
                         std::to_string(address.function));
            continue;
        }
        const std::wstring ven = L"VEN_" + Text::wide(Text::upper(Text::hex(adapter.vendor, 4)));
        const std::wstring dev = L"DEV_" + Text::wide(Text::upper(Text::hex(adapter.device, 4)));
        if (found->hardware_ids.find(ven) == std::wstring::npos ||
            found->hardware_ids.find(dev) == std::wstring::npos) {
            problems.add(adapter.name + ": SetupAPI hardware ids '" + Text::utf8(found->hardware_ids) +
                         "' lack " + Text::utf8(ven) + " and " + Text::utf8(dev));
        }
        context.measure(adapter.luid_text() + ".instance", Text::utf8(found->instance_id));
    }
    if (checked == 0) {
        problems.add("DXGI lists no hardware adapter");
    }
    return problems.outcome();
}

Outcome IdentityChecks::names_nonempty_printable(Context& context) {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::all(adapters, error)) {
        return Outcome::fail(error);
    }
    Problems problems;
    const auto validate = [&](const std::string& source, const std::wstring& text) {
        if (trim(Text::utf8(text)).empty()) {
            problems.add(source + ": name is empty");
        } else if (!printable(text)) {
            problems.add(source + ": name '" + Text::utf8(text) + "' has control or invalid characters");
        }
    };
    for (const AvAdapter& adapter : adapters) {
        const std::string source = "DXGI " + adapter.luid_text();
        if (adapter.raw_name.find(L'\0') == std::wstring::npos) {
            problems.add(source + ": description is not null-terminated within 128 characters");
        }
        validate(source, adapter.raw_name.substr(0, adapter.raw_name.find(L'\0')));
    }
    KmtApi kmt;
    if (kmt.load(error)) {
        for (const AvAdapter& adapter : adapters) {
            std::wstring text;
            if (!adapter.software && kmt.adapter_string(adapter.luid, text)) {
                validate("D3DKMT " + adapter.luid_text(), text);
            }
        }
    }
    for (const DisplayDevice& device : DisplayDevices::enumerate(true)) {
        validate("SetupAPI " + Text::utf8(device.instance_id), Text::wide(device.name));
    }
    VulkanApi vulkan;
    std::vector<VulkanDevice> vk_devices;
    if (vulkan.load(error) && vulkan.create_instance(error) && vulkan.devices(vk_devices, error)) {
        for (const VulkanDevice& device : vk_devices) {
            if (!device.name_terminated) {
                problems.add("Vulkan '" + device.name + "': deviceName is not null-terminated");
            }
            validate("Vulkan", Text::wide(device.name));
        }
    } else {
        problems.add(error);
    }
    OpenClApi opencl;
    std::vector<OpenClPlatform> platforms;
    if (opencl.load(error) && opencl.platforms(platforms, error)) {
        for (const OpenClPlatform& platform : platforms) {
            validate("OpenCL platform", Text::wide(platform.name));
            for (const OpenClDevice& device : platform.devices) {
                validate("OpenCL device", Text::wide(device.name));
            }
        }
    } else {
        problems.add(error);
    }
    for (const AvAdapter& adapter : adapters) {
        if (adapter.software) {
            continue;
        }
        const std::string name = Text::utf8(adapter.raw_name.substr(0, adapter.raw_name.find(L'\0')));
        for (const VulkanDevice& device : vk_devices) {
            if (device.luid_valid && luid_key(device.luid) == adapter.luid_key() &&
                !name_compatible(name, 127, device.name, 255)) {
                problems.add("DXGI '" + name + "' and Vulkan '" + device.name + "' name the same adapter differently");
            }
        }
        for (const OpenClPlatform& platform : platforms) {
            for (const OpenClDevice& device : platform.devices) {
                if (device.luid_valid && luid_key(device.luid) == adapter.luid_key() &&
                    !name_compatible(name, 127, device.name, 0)) {
                    problems.add("DXGI '" + name + "' and OpenCL '" + device.name +
                                 "' name the same adapter differently");
                }
            }
        }
    }
    context.measure("adapters", adapters.size());
    return problems.outcome();
}

}  // namespace lsw::suites::sanity::enumeration
