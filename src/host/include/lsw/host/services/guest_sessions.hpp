#pragma once

#include <QString>
#include <QVector>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::services {

// The guest's answer to ``list_sessions``: the Windows sessions a command
// can be run in. The agent is a session-0 service; a console, a launch or
// a script goes to whichever of these it is asked for, and to
// ``default_id`` — the console user's — when it is not.
struct GuestSessions {
    struct Session {
        int id = 0;
        QString user;
        QString state;  // "active", "connected" or "disconnected"
        bool console = false;
        // Whether the agent's helper runs there: what window, UI
        // Automation and DPI commands need in that session.
        bool helper = false;
    };

    // False until the agent has answered, and again once it stops
    // answering: "no sessions" and "not asked" read differently.
    bool valid = false;
    QVector<Session> sessions;
    std::optional<int> default_id;

    [[nodiscard]] static GuestSessions from_json(const nlohmann::json& body);
};

}  // namespace lsw::host::services
