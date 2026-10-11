#pragma once

#include <QString>
#include <QVector>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::services {

// The guest's answer to ``gpu_status``: every accel-virt adapter
// Windows has, and the machine state avgpu.sys depends on.
struct GuestGpuStatus {
    struct Adapter {
        QString name;
        QString hardware_id;
        quint32 vendor_id = 0;
        quint32 device_id = 0;
        QString location;
        QString service;
        QString driver_version;
        quint32 problem_code = 0;
        bool started = false;

        // Running avgpu.sys with no problem.
        [[nodiscard]] bool running() const noexcept { return started && problem_code == 0; }
        // Windows has no driver for it: problem 28, or no service at all.
        [[nodiscard]] bool no_driver() const noexcept {
            return problem_code == kNoDriverInstalled || (!started && service.isEmpty());
        }
    };

    // CM_PROB_FAILED_INSTALL: "the drivers for this device are not
    // installed", what an adapter added after the package reports.
    static constexpr quint32 kNoDriverInstalled = 28;

    // False until an answer has been read; everything else is then empty.
    bool valid = false;
    QVector<Adapter> adapters;
    bool test_signing = false;
    bool driver_search_off = false;
    std::optional<bool> user_mode_submission;
    std::optional<bool> native_fences;
    // The package.sha256 of the driver package the guest last installed
    // (C:\av\pristine\package), as text; empty when it has none.
    QString driver_manifest;

    // The reply's body. A missing or mistyped field reads as unknown
    // rather than failing the whole answer.
    [[nodiscard]] static GuestGpuStatus from_json(const nlohmann::json& body);

    // What a problem code means for this driver, in a clause: "test
    // signing is off, so Windows refused the driver's signature".
    [[nodiscard]] static QString explain(quint32 problem_code, bool test_signing);
};

}  // namespace lsw::host::services
