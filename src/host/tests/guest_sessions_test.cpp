// The Console page's session list, as the agent's list_sessions reply
// decodes: what is listed, what is skipped, and "not asked" versus
// "nothing there".

#include "lsw/host/services/guest_sessions.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>

using lsw::host::services::GuestSessions;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

}  // namespace

int main() {
    const auto full = GuestSessions::from_json(nlohmann::json::parse(R"({
        "sessions": [
            {"id": 1, "user": "lsw", "state": "active", "console": true, "helper": true},
            {"id": 2, "user": "other", "state": "disconnected", "console": false, "helper": false},
            {"id": "3"},
            {"user": "no id"}
        ],
        "default": 1
    })"));
    check(full.valid, "an answer is valid");
    check(full.sessions.size() == 2, "entries without an integer id are skipped");
    check(full.sessions.size() == 2 && full.sessions[0].id == 1 &&
              full.sessions[0].user == QStringLiteral("lsw") && full.sessions[0].console &&
              full.sessions[0].helper,
          "a session's fields are read");
    check(full.sessions.size() == 2 && full.sessions[1].state == QStringLiteral("disconnected"),
          "the state is kept as the agent says it");
    check(full.default_id == 1, "the default session is read");

    const auto empty = GuestSessions::from_json(nlohmann::json::parse(R"({"sessions": []})"));
    check(empty.valid && empty.sessions.isEmpty() && !empty.default_id,
          "no sessions is a valid answer with no default");

    check(!GuestSessions::from_json(nlohmann::json::array()).valid,
          "a reply that is not an object is not an answer");
    check(!GuestSessions{}.valid, "not asked is not valid");

    std::printf("%s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
