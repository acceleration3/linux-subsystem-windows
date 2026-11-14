#pragma once

namespace lsw::host::services {
struct PriorityStatus {
    enum class Level { Normal, High, Highest };

    Level wanted = Level::Normal;
    bool vm_running = false;
    bool weight_raised = false;
    bool weight_refused = false;
    bool mirrors_refused = false;
    bool threads_raised = false;
    bool threads_refused = false;
    bool threads_unnamed = false;
    int nice = 0;
    int wanted_nice = 0;

    [[nodiscard]] bool clamped() const noexcept { return threads_raised && nice != wanted_nice; }

    [[nodiscard]] bool operator==(const PriorityStatus&) const = default;
};

}
