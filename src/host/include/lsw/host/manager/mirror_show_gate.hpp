#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>

namespace lsw::host::manager {

// Decides when a main mirror is allowed to appear, and gives it its
// desktop identity on the way.
//
// A mirror must not map until BOTH the guest's window properties and its
// icon have come back, and the per-window ``.desktop`` file must be
// written BEFORE the toplevel maps: the compositor reads app_id at map
// time to pick the icon and give the window its own task-grouping entry,
// so writing it afterwards loses the race and the mirror comes up
// generic, grouped under the manager.
//
// That is a small state machine with an ordering constraint, which is
// why it is a class and not four bools on the widget.
class MirrorShowGate {
public:
    // ``show`` is invoked exactly once, after the desktop entry exists.
    MirrorShowGate(std::uint64_t hwnd, std::function<void()> show);

    void props_arrived(const QString& window_title);
    void icon_arrived(const QString& window_title, QByteArray icon_png);

    [[nodiscard]] const QByteArray& icon_png() const noexcept { return icon_png_; }
    [[nodiscard]] bool shown() const noexcept { return shown_; }

    // Popups never wait: they are frameless, placed by their owner, and
    // sized by their frames.
    void bypass() { props_done_ = icon_done_ = true; }

private:
    void maybe_show(const QString& window_title);

    std::uint64_t hwnd_;
    std::function<void()> show_;
    QByteArray icon_png_;
    bool props_done_ = false;
    bool icon_done_ = false;
    bool shown_ = false;
};

}  // namespace lsw::host::manager
