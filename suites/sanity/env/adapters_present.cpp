#include "sanity/env/adapters_present.hpp"

#include "core/dxgi_adapters.hpp"
#include "core/text.hpp"
#include "sanity/env/av_display_devices.hpp"

#include <algorithm>

namespace lsw::suites::sanity::env {

Outcome AdaptersPresent::run(Context& context) {
    std::string error;
    std::vector<DxgiAdapter> adapters;
    if (!DxgiAdapters::enumerate(adapters, error)) {
        return Outcome::fail(error);
    }
    std::vector<DxgiAdapter> av;
    for (const DxgiAdapter& adapter : adapters) {
        if (adapter.subsystem == AvDisplayDevices::kSubsystem) {
            av.push_back(adapter);
        }
    }
    for (std::size_t i = 0; i < av.size(); ++i) {
        const std::string key = "adapter." + std::to_string(i) + ".";
        context.measure(key + "name", av[i].name);
        context.measure(key + "luid", av[i].luid());
        context.measure(key + "vendor", Text::hex(av[i].vendor, 4));
        context.measure(key + "device", Text::hex(av[i].device, 4));
    }
    context.measure("dxgi.adapters", adapters.size());
    const std::vector<AvDisplayDevice> devices = AvDisplayDevices::enumerate();
    context.measure("devices", devices.size());
    if (devices.empty()) {
        return Outcome::fail("no accel-virt display device (SUBSYS_52561AF4) is present");
    }
    std::string problems;
    for (const AvDisplayDevice& device : devices) {
        if (!device.status_known) {
            problems += device.name + ": device status unreadable; ";
        } else if (device.problem_code != 0) {
            problems += device.name + ": problem code " + std::to_string(device.problem_code) + "; ";
        } else if (!device.started) {
            problems += device.name + ": not started; ";
        }
    }
    if (av.size() < devices.size()) {
        problems += std::to_string(devices.size()) + " accel-virt device(s) but " +
                    std::to_string(av.size()) + " DXGI adapter(s); ";
    }
    if (!problems.empty()) {
        return Outcome::fail(problems.substr(0, problems.size() - 2));
    }
    return Outcome::ok();
}

}  // namespace lsw::suites::sanity::env
