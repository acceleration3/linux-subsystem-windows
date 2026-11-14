#include "lsw/agent/apps/window_announcements.hpp"

#include "lsw/linkproto/commands.hpp"

#include <algorithm>
#include <utility>

namespace lsw::agent::apps {

void WindowAnnouncements::appeared(const win::DiscoveredWindow& window) {
    if (!window.has_caption || window.owner != 0) {
        return;
    }
    const std::lock_guard lock(mu_);
    std::erase_if(pending_, [&](const win::DiscoveredWindow& known) { return known.hwnd == window.hwnd; });
    if (pending_.size() >= kMostPending) {
        pending_.erase(pending_.begin());
    }
    pending_.push_back(window);
}

std::vector<rpc::Event> WindowAnnouncements::collect_pending() {
    std::vector<win::DiscoveredWindow> windows;
    {
        const std::lock_guard lock(mu_);
        windows = std::exchange(pending_, {});
    }
    std::vector<rpc::Event> out;
    out.reserve(windows.size());
    for (const auto& window : windows) {
        out.emplace_back(linkproto::topic::kWindowAppeared, window.row());
    }
    return out;
}

}
