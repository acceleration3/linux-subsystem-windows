#include "support/target_adapter.hpp"

#include "core/text.hpp"
#include "support/vulkan_api.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lsw::suites {

namespace {

std::string lower(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

std::string hex_digits(std::string text) {
    std::string out;
    for (char c : text) {
        if (c != ':' && c != '-' && c != ' ') {
            out += c;
        }
    }
    return lower(out);
}

std::string uuid_text(const std::uint8_t* bytes) {
    std::string out;
    char text[4];
    for (int i = 0; i < 16; ++i) {
        std::snprintf(text, sizeof text, "%02x", bytes[i]);
        out += text;
    }
    return out;
}

bool is_zero(const std::uint8_t* bytes) {
    for (int i = 0; i < 16; ++i) {
        if (bytes[i] != 0) {
            return false;
        }
    }
    return true;
}

bool parse_hex64(const std::string& digits, std::uint64_t& out) {
    if (digits.empty() || digits.size() > 16) {
        return false;
    }
    char* end = nullptr;
    out = std::strtoull(digits.c_str(), &end, 16);
    return end != nullptr && *end == '\0';
}

void set_env(const char* name, const std::string& value) {
    _putenv_s(name, value.c_str());
    SetEnvironmentVariableA(name, value.c_str());
}

}  // namespace

std::string TargetAdapter::spec() {
    const char* env = std::getenv("LSW_SUITES_ADAPTER");
    return env != nullptr && *env != '\0' ? env : "first";
}

std::string TargetAdapter::vulkan_uuid(const AvAdapter& adapter) {
    VulkanApi api;
    std::vector<VulkanDevice> devices;
    std::string error;
    if (!api.load(error) || !api.enumerate_fresh(devices, error)) {
        return {};
    }
    for (const VulkanDevice& device : devices) {
        if (device.luid_valid && AvAdapters::key(device.luid) == adapter.luid_key() && !is_zero(device.uuid)) {
            return uuid_text(device.uuid);
        }
    }
    return {};
}

std::string TargetAdapter::gl_unavailable(const AvAdapter& adapter) {
    for (const AvOutput& output : adapter.outputs) {
        if (output.attached) {
            return {};
        }
    }
    return "OpenGL through WGL can only target the adapter that drives a window's monitor, and " + adapter.name + " (LUID " + adapter.luid_text() +
           ") drives no display";
}

bool TargetAdapter::resolve(const std::string& spec, AvAdapter& out, std::size_t& index, std::string& error) {
    std::vector<AvAdapter> adapters;
    if (!AvAdapters::accel(adapters, error)) {
        return false;
    }
    const std::string value = spec.empty() ? "first" : spec;
    const auto take = [&](std::size_t i) {
        index = i;
        out = std::move(adapters[i]);
        return true;
    };
    if (value == "first") {
        return take(0);
    }
    const std::size_t colon = value.find(':');
    const std::string kind = colon == std::string::npos ? std::string() : value.substr(0, colon);
    const std::string arg = colon == std::string::npos ? value : value.substr(colon + 1);
    if (kind.empty()) {
        char* end = nullptr;
        const unsigned long n = std::strtoul(arg.c_str(), &end, 10);
        if (end == arg.c_str() || *end != '\0') {
            error = "--adapter needs first, N, luid:HEX, uuid:HEX or name:SUBSTR, got " + value;
            return false;
        }
        if (n >= adapters.size()) {
            error = "adapter index " + std::to_string(n) + " is out of range, the guest has " + std::to_string(adapters.size()) + " accel adapters";
            return false;
        }
        return take(n);
    }
    if (kind == "luid") {
        std::uint64_t key = 0;
        if (!parse_hex64(hex_digits(arg), key)) {
            error = "--adapter luid: needs hexadecimal digits, got " + arg;
            return false;
        }
        for (std::size_t i = 0; i < adapters.size(); ++i) {
            if (adapters[i].luid_key() == key) {
                return take(i);
            }
        }
        error = "no accel adapter has the LUID " + arg;
        return false;
    }
    if (kind == "name") {
        const std::string needle = Text::upper(arg);
        for (std::size_t i = 0; i < adapters.size(); ++i) {
            if (Text::upper(adapters[i].name).find(needle) != std::string::npos) {
                return take(i);
            }
        }
        error = "no accel adapter has a name containing \"" + arg + "\"";
        return false;
    }
    if (kind == "uuid") {
        const std::string wanted = hex_digits(arg);
        for (std::size_t i = 0; i < adapters.size(); ++i) {
            if (vulkan_uuid(adapters[i]) == wanted) {
                return take(i);
            }
        }
        error = "no accel adapter reports the Vulkan device UUID " + arg;
        return false;
    }
    error = "--adapter needs first, N, luid:HEX, uuid:HEX or name:SUBSTR, got " + value;
    return false;
}

bool TargetAdapter::pick(AvAdapter& out, std::string& error) {
    if (std::getenv("LSW_SUITES_DEV_ANY") != nullptr) {
        std::vector<AvAdapter> adapters;
        if (!AvAdapters::all(adapters, error)) {
            return false;
        }
        for (AvAdapter& adapter : adapters) {
            if (!adapter.software) {
                out = std::move(adapter);
                return true;
            }
        }
        error = "no hardware adapter";
        return false;
    }
    std::size_t index = 0;
    return resolve(spec(), out, index, error);
}

void TargetAdapter::export_identity(const AvAdapter& adapter, std::size_t index) {
    set_env("LSW_SUITES_ADAPTER", "luid:" + Text::hex(static_cast<std::uint32_t>(adapter.luid.HighPart), 8) + Text::hex(adapter.luid.LowPart, 8));
    set_env("LSW_SUITES_ADAPTER_INDEX", std::to_string(index));
    set_env("LSW_SUITES_ADAPTER_NAME", adapter.name);
    set_env("LSW_SUITES_ADAPTER_LUID", adapter.luid_text());
    set_env("LSW_SUITES_ADAPTER_UUID", vulkan_uuid(adapter));
    set_env("LSW_SUITES_ADAPTER_PCI", Text::hex(adapter.vendor, 4) + ":" + Text::hex(adapter.device, 4));
}

int TargetAdapter::list() {
    std::vector<AvAdapter> adapters;
    std::string error;
    if (!AvAdapters::accel(adapters, error)) {
        std::fprintf(stderr, "lsw-suites: %s\n", error.c_str());
        return 1;
    }
    for (std::size_t i = 0; i < adapters.size(); ++i) {
        const AvAdapter& adapter = adapters[i];
        const std::string uuid = vulkan_uuid(adapter);
        std::printf("%zu luid=%s pci=%s:%s outputs=%zu uuid=%s name=%s\n", i, adapter.luid_text().c_str(), Text::hex(adapter.vendor, 4).c_str(),
                    Text::hex(adapter.device, 4).c_str(), adapter.outputs.size(), uuid.empty() ? "-" : uuid.c_str(), adapter.name.c_str());
    }
    return 0;
}

}  // namespace lsw::suites
