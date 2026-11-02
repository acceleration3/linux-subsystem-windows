#pragma once

#include <QString>
#include <QVector>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::services {
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

        [[nodiscard]] bool running() const noexcept { return started && problem_code == 0; }
        [[nodiscard]] bool no_driver() const noexcept {
            return problem_code == kNoDriverInstalled || (!started && service.isEmpty());
        }
    };

    static constexpr quint32 kNoDriverInstalled = 28;

    bool valid = false;
    QVector<Adapter> adapters;
    bool test_signing = false;
    bool driver_search_off = false;
    std::optional<bool> user_mode_submission;
    std::optional<bool> native_fences;

    [[nodiscard]] static GuestGpuStatus from_json(const nlohmann::json& body);

    [[nodiscard]] static QString explain(quint32 problem_code, bool test_signing);
};

}
