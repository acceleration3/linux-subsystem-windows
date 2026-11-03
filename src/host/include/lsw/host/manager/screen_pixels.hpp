#pragma once

#include <QSize>
#include <optional>

class QScreen;
class QWindow;

namespace lsw::host::services { class StatusHub; }

namespace lsw::host::manager {
class ScreenPixels {
public:
    explicit ScreenPixels(const services::StatusHub& status) noexcept : status_(status) {}

    [[nodiscard]] std::optional<QSize> of(const QScreen* screen, const QWindow* window) const;

private:
    const services::StatusHub& status_;
};

}
