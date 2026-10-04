#include "lsw/host/services/guest_sessions.hpp"

#include <nlohmann/json.hpp>

namespace lsw::host::services {

GuestSessions GuestSessions::from_json(const nlohmann::json& body) {
    GuestSessions out;
    if (!body.is_object()) {
        return out;
    }
    out.valid = true;
    if (const auto it = body.find("sessions"); it != body.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_number_integer()) {
                continue;
            }
            Session session;
            session.id = entry["id"].get<int>();
            session.user = QString::fromStdString(entry.value("user", std::string{}));
            session.state = QString::fromStdString(entry.value("state", std::string{}));
            session.console = entry.value("console", false);
            session.helper = entry.value("helper", false);
            out.sessions.push_back(std::move(session));
        }
    }
    if (const auto it = body.find("default"); it != body.end() && it->is_number_integer()) {
        out.default_id = it->get<int>();
    }
    return out;
}

}  // namespace lsw::host::services
