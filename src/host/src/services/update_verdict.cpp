#include "lsw/host/services/update_verdict.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_reply.hpp"

#include <QDateTime>
#include <QString>

#include <cstdint>

namespace lsw::host::services {

std::string UpdateVerdict::finished_of(const nlohmann::json& body) {
    if (!body.is_object()) {
        return {};
    }
    const auto& last = body.value("last_update", nlohmann::json{});
    return last.is_object() ? last.value("finished", std::string{}) : std::string{};
}

bool UpdateVerdict::is_mine(const nlohmann::json& last, const std::string& baseline) {
    if (!last.is_object()) {
        return false;
    }
    const std::string finished = last.value("finished", std::string{});
    return !finished.empty() && finished != baseline;
}

bool UpdateVerdict::moved_on(const link::AgentReply& info, const std::string& baseline) {
    return !info.ok || is_mine(info.body.value("last_update", nlohmann::json{}), baseline);
}

bool UpdateVerdict::started_after(const link::AgentReply& info, const std::string& finished) {
    constexpr qint64 kUnixEpochAsFiletimeMs = 11'644'473'600'000;
    constexpr qint64 kTicksPerMs = 10'000;
    constexpr qint64 kSlackMs = 1000;
    const QDateTime at = QDateTime::fromString(QString::fromStdString(finished), Qt::ISODateWithMs);
    const std::uint64_t started = info.body.value("agent_started", std::uint64_t{0});
    if (!at.isValid() || started == 0) {
        return false;
    }
    const auto finished_ticks =
        static_cast<std::uint64_t>((at.toMSecsSinceEpoch() + kSlackMs + kUnixEpochAsFiletimeMs) * kTicksPerMs);
    return started > finished_ticks;
}

UpdateVerdict UpdateVerdict::judge(const link::AgentReply& info, const std::string& baseline,
                                   const std::string& shipped, const std::string& shipped_config) {
    UpdateVerdict verdict;
    if (!info.ok) {
        return verdict;
    }
    const auto& last = info.body.value("last_update", nlohmann::json{});
    if (!is_mine(last, baseline)) {
        return verdict;
    }
    verdict.last = last;
    if (!last.value("ok", false)) {
        verdict.kind = Kind::Failed;
        verdict.message = last.value("message", std::string{"-"});
        LSW_DEBUG("update verdict: failed: {}", verdict.message);
        return verdict;
    }
    if (info.body.value("release", std::string{}) != shipped
        || info.body.value("release_config", std::string{}) != shipped_config) {
        return verdict;
    }
    verdict.kind = !last.value("restart", false) || started_after(info, last.value("finished", std::string{}))
                       ? Kind::Succeeded
                       : Kind::RestartPending;
    LSW_DEBUG("update verdict: {}", verdict.kind == Kind::Succeeded ? "succeeded" : "restart pending");
    return verdict;
}

}
