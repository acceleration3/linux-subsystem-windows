#include "lsw/host/manager/mirror_show_gate.hpp"

#include "lsw/host/manager/taskbar_entry.hpp"

#include <utility>

namespace lsw::host::manager {

MirrorShowGate::MirrorShowGate(std::uint64_t hwnd, QString desktop_id,
                               std::function<void()> show)
    : hwnd_(hwnd), desktop_id_(std::move(desktop_id)), show_(std::move(show)) {}

void MirrorShowGate::props_arrived(const QString& window_title) {
    props_done_ = true;
    maybe_show(window_title);
}

void MirrorShowGate::icon_arrived(const QString& window_title, QByteArray icon_png) {
    icon_png_ = std::move(icon_png);
    icon_done_ = true;
    maybe_show(window_title);
}

void MirrorShowGate::maybe_show(const QString& window_title) {
    if (shown_ || !props_done_ || !icon_done_) {
        return;
    }
    shown_ = true;
    // Desktop entry first — see the header for why the order is
    // load-bearing. Nothing to write when the mirror maps under its
    // application's launcher entry, which already describes it.
    if (desktop_id_ == TaskbarEntry::entry_id(hwnd_)) {
        TaskbarEntry::register_window(hwnd_, window_title, icon_png_);
    }
    if (show_) {
        show_();
    }
}

}  // namespace lsw::host::manager
