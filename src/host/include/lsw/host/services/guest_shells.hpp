#pragma once

#include "lsw/host/link/agent_rpc_client.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace lsw::host::services {

class StatusHub;

class GuestShells : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Opening,
        Open,
        Exited,
        Failed,
        Lost,
    };

    struct Shell {
        State state = State::Idle;
        QString console_id;
        QString user;
        bool elevated = false;
        int exit_code = 0;
        QString error;
        QString transcript;
        QStringList queued;
        std::uint64_t attempt = 0;
    };

    GuestShells(link::AgentRpcClient& rpc, StatusHub& status, QObject* parent = nullptr);
    ~GuestShells() override;

    void open(int session);

    void send(int session, const QString& line);

    void clear(int session);

    [[nodiscard]] const Shell* find(int session) const;

Q_SIGNALS:
    void output(int session, const QString& text);
    void changed(int session);

private:
    void on_agent(bool online);
    void on_output(const QString& console_id, const QByteArray& bytes);
    void on_closed(const QString& console_id, int exit_code);
    void append(int session, const QString& text);
    void write(const Shell& shell, const QString& line);
    [[nodiscard]] std::optional<int> session_of(const QString& console_id) const;

    link::AgentRpcClient& rpc_;
    StatusHub& hub_;
    bool agent_online_ = false;
    std::uint64_t next_attempt_ = 1;
    QHash<int, Shell> shells_;

    link::EventSubscription output_sub_;
    link::EventSubscription closed_sub_;
};

}
