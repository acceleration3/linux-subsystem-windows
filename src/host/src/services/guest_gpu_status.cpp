#include "lsw/host/services/guest_gpu_status.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::services {

namespace {

QString text(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? QString::fromStdString(it->get<std::string>())
                                                 : QString{};
}

std::optional<bool> flag(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return std::nullopt;
    }
    return it->get<bool>();
}

quint32 number(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_unsigned() ? it->get<quint32>() : 0U;
}

}  // namespace

GuestGpuStatus GuestGpuStatus::from_json(const nlohmann::json& body) {
    GuestGpuStatus out;
    if (!body.is_object()) {
        return out;
    }
    out.valid = true;
    if (const auto it = body.find("adapters"); it != body.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object()) {
                continue;
            }
            Adapter adapter;
            adapter.name = text(entry, "name");
            adapter.hardware_id = text(entry, "hardware_id");
            adapter.vendor_id = number(entry, "vendor_id");
            adapter.device_id = number(entry, "device_id");
            adapter.location = text(entry, "location");
            adapter.service = text(entry, "service");
            adapter.driver_version = text(entry, "driver_version");
            adapter.problem_code = number(entry, "problem_code");
            adapter.started = flag(entry, "started").value_or(false);
            out.adapters.push_back(adapter);
        }
    }
    out.test_signing = flag(body, "test_signing").value_or(false);
    out.driver_search_off = flag(body, "driver_search_off").value_or(false);
    out.user_mode_submission = flag(body, "user_mode_submission");
    out.native_fences = flag(body, "native_fences");
    out.driver_manifest = text(body, "driver_manifest");
    return out;
}

QString GuestGpuStatus::explain(quint32 problem_code, bool test_signing) {
    switch (problem_code) {
    case 52:
        return test_signing
                   ? QStringLiteral("Windows refused the driver's signature, so its certificate "
                                    "is not trusted")
                   : QStringLiteral("test signing is off, so Windows refused the driver's "
                                    "signature");
    case kNoDriverInstalled:
        return QStringLiteral("Windows has no driver installed for it");
    case 10:
        return QStringLiteral("the driver failed to start the device; the driver log says why");
    case 31:
    case 43:
        return QStringLiteral("the driver reported a failure; the driver log says why");
    case 22:
        return QStringLiteral("the device is disabled in Device Manager");
    default:
        break;
    }
    return QStringLiteral("Device Manager has the details");
}

}  // namespace lsw::host::services
