#pragma once

namespace lsw::host::services {
struct ClipboardStatus {
    bool enabled = false;
    bool working = false;
    bool limited = false;

    [[nodiscard]] bool operator==(const ClipboardStatus&) const = default;
};

}
