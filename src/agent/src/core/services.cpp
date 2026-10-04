#include "lsw/agent/core/services.hpp"

#include "lsw/agent/session/session_manager.hpp"
#include "lsw/agent/uia/uia.hpp"

#include <stdexcept>
#include <utility>

namespace lsw::agent::core {

AgentServices::AgentServices(Role role) : role_(role) {
    if (role_ == Role::SessionHelper) {
        uia_ = std::make_unique<uia::Uia>();
    }
}

AgentServices::~AgentServices() = default;

uia::Uia& AgentServices::uia() {
    if (!uia_) {
        throw std::logic_error("UI Automation lives in the session helper, not the service");
    }
    return *uia_;
}

session::SessionManager& AgentServices::sessions() {
    if (!sessions_) {
        throw std::logic_error("sessions are the service's, not a session helper's");
    }
    return *sessions_;
}

void AgentServices::attach_sessions(std::unique_ptr<session::SessionManager> sessions) {
    sessions_ = std::move(sessions);
}

void AgentServices::shutdown() {
    if (sessions_) {
        sessions_->stop();
    }
    if (uia_) {
        uia_->shutdown();
    }
}

}  // namespace lsw::agent::core
