#pragma once

#include <QByteArray>

#include <string>

namespace lsw::host::link { class AgentRpcClient; }

namespace lsw::host::services {
class AgentRedeployer {
public:
    struct Outcome {
        bool ok = false;
        std::string message;
    };

    struct Shipped {
        QByteArray agent;
        QByteArray mirror;
        std::string error;
        std::string note;
    };
    [[nodiscard]] static Shipped shipped();

    [[nodiscard]] static std::string sha256(const QByteArray& bytes);

    explicit AgentRedeployer(link::AgentRpcClient& agent) noexcept : agent_(agent) {}

    [[nodiscard]] Outcome run();

private:
    [[nodiscard]] Outcome swap();

    link::AgentRpcClient& agent_;
};

}
