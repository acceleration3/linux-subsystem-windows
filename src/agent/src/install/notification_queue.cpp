#include "lsw/agent/install/notification_queue.hpp"

#include <utility>

namespace lsw::agent::install {

void NotificationQueue::post(JobNotification note) {
    const std::lock_guard<std::mutex> lock(mu_);
    if (note.kind == JobNotificationKind::Progress) {
        std::size_t progress_count = 0;
        for (const auto& p : pending_) {
            if (p.kind == JobNotificationKind::Progress) {
                ++progress_count;
            }
        }
        if (progress_count >= kMaxPendingProgress) {
            return;
        }
    }
    pending_.push_back(std::move(note));
}

std::vector<JobNotification> NotificationQueue::collect() {
    const std::lock_guard<std::mutex> lock(mu_);
    std::vector<JobNotification> out;
    out.swap(pending_);
    return out;
}

void NotificationQueue::requeue_front(std::vector<JobNotification> notes) {
    if (notes.empty()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mu_);
    // Prepend, preserving order: [notes...] + [whatever arrived since].
    notes.reserve(notes.size() + pending_.size());
    for (auto& p : pending_) {
        notes.push_back(std::move(p));
    }
    pending_.clear();
    std::size_t progress_count = 0;
    for (auto& n : notes) {
        if (n.kind == JobNotificationKind::Progress) {
            if (progress_count >= kMaxPendingProgress) {
                continue;
            }
            ++progress_count;
        }
        pending_.push_back(std::move(n));
    }
}

}  // namespace lsw::agent::install
