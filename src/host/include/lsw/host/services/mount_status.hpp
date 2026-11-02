#pragma once

#include <QString>

namespace lsw::host::services {
struct MountStatus {
    enum class State {
        Off,
        NoGuest,
        Waiting,
        Mounting,
        Mounted,
        Unmounting,
        Unavailable,
        Failed,
    };

    State state = State::Off;
    QString path;
    QString detail;

    [[nodiscard]] bool mounted() const noexcept { return state == State::Mounted; }

    [[nodiscard]] QString text() const;
    [[nodiscard]] bool operator==(const MountStatus&) const = default;
};

}  // namespace lsw::host::services
