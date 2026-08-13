#include "lsw/agent/transport/installer_event_source.hpp"

#include "lsw/agent/install/installer.hpp"

#include "lsw/linkproto/commands.hpp"

#include <nlohmann/json.hpp>
#include <utility>

namespace lsw::agent::transport {

std::vector<rpc::Event> InstallerEventSource::collect_pending() {
    batch_ = installer_->collect_pending();

    std::vector<rpc::Event> events;
    events.reserve(batch_.size());
    for (const auto& n : batch_) {
        const bool done = (n.kind == install::JobNotificationKind::Done);
        nlohmann::json payload = {
            {"job_id", n.job_id},
            {"name", n.name},
            {"phase", n.phase},
            {"bytes", n.bytes},
            {"total_bytes", n.total_bytes},
        };
        if (done) {
            payload["ok"] = n.ok;
            payload["exit_code"] = n.exit_code;
        }
        if (!n.message.empty()) {
            payload["message"] = n.message;
        }
        events.emplace_back(done ? linkproto::topic::kSetupDone
                                 : linkproto::topic::kSetupProgress,
                            std::move(payload));
    }
    return events;
}

void InstallerEventSource::undeliverable(std::size_t first_undelivered) {
    if (first_undelivered >= batch_.size()) {
        batch_.clear();
        return;
    }
    // Put the tail back at the FRONT of the installer's queue so the next
    // connection sees these before anything newer — a ``setup_done`` that
    // arrives after the progress ticks of the following step would be
    // read as the wrong step finishing.
    std::vector<install::JobNotification> undelivered(
        std::make_move_iterator(batch_.begin()
                                + static_cast<std::ptrdiff_t>(first_undelivered)),
        std::make_move_iterator(batch_.end()));
    batch_.clear();
    installer_->requeue_front(std::move(undelivered));
}

}  // namespace lsw::agent::transport
