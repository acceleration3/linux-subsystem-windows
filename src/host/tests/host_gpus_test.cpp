// Which host GPU a setting names. The guest's adapter is bound by a
// Vulkan device UUID or by "auto", and the manager has to agree with
// QEMU's device about what "auto" means — the first device the loader
// lists, whatever kind it is — or the settings page shows one GPU and
// the guest runs on another. A pure function over a list, so no GPU is
// needed; enumerate() itself is exercised by whatever this host has.

#include "lsw/host/platform/host_gpus.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <cstdio>
#include <cstdlib>

using lsw::host::platform::HostGpu;
using lsw::host::platform::HostGpus;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

HostGpu gpu(const char* name, const char* uuid, bool cpu = false) {
    HostGpu out;
    out.name = QString::fromLatin1(name);
    out.uuid = QString::fromLatin1(uuid);
    out.cpu = cpu;
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app{argc, argv};

    // llvmpipe first on purpose: the device's auto takes index 0 even
    // when it is a CPU device, and so must this.
    const QVector<HostGpu> gpus = {
        gpu("llvmpipe (LLVM 20.1.0, 256 bits)", "6d657361-3230-2e31-2e30-000000000000", true),
        gpu("AMD Radeon RX 9070 XT (RADV GFX1201)", "00000000-0300-0000-0000-000000000000"),
        gpu("Intel(R) Graphics (ARL)", "8680a07d-0300-0000-0002-000000000000"),
    };

    check(HostGpus::resolve(gpus, QStringLiteral("auto"))->uuid == gpus.at(0).uuid,
          "auto is the first device, whatever its type");
    check(HostGpus::resolve(gpus, QString{})->uuid == gpus.at(0).uuid, "empty is auto");
    check(HostGpus::resolve(gpus, QStringLiteral("8680a07d-0300-0000-0002-000000000000"))->name ==
              gpus.at(2).name,
          "a UUID names its GPU");
    check(
        HostGpus::resolve(gpus, QStringLiteral("8680A07D-0300-0000-0002-000000000000")).has_value(),
        "a UUID in upper case still names it");
    check(!HostGpus::resolve(gpus, QStringLiteral("11111111-2222-3333-4444-555555555555"))
               .has_value(),
          "a UUID no GPU has is nothing");
    check(!HostGpus::resolve({}, QStringLiteral("auto")).has_value(), "auto on no GPUs is nothing");
    check(!HostGpus::resolve(gpus, QStringLiteral("AUTO")).has_value(),
          "only the device's own spelling of auto is auto");

    // Whatever this host has, each UUID is in the form the device's
    // host-gpu= takes.
    static const QRegularExpression form(
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    const QVector<HostGpu> host = HostGpus::enumerate();
    bool all_formed = true;
    for (const auto& g : host) {
        std::printf("       %s %04x:%04x %s%s\n", qPrintable(g.uuid), g.vendor_id, g.device_id,
                    qPrintable(g.name), g.cpu ? " (cpu)" : "");
        all_formed = all_formed && form.match(g.uuid).hasMatch();
    }
    check(all_formed, "every enumerated UUID is 8-4-4-4-12 lowercase hex");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
