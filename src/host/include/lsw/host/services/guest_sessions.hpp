#pragma once

#include <QString>
#include <QVector>
#include <optional>

#include <nlohmann/json_fwd.hpp>

namespace lsw::host::services {
struct GuestSessions {
    struct Session {
        int id = 0;
        QString user;
        QString state;
        bool console = false;
        bool helper = false;
    };

    bool valid = false;
    QVector<Session> sessions;
    std::optional<int> default_id;

    [[nodiscard]] static GuestSessions from_json(const nlohmann::json& body);
};

}
