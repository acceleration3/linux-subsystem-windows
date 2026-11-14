#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
class IProcessThreads {
public:
    struct Thread {
        QString name;
        int nice = 0;
        qint64 age_ms = 0;
    };

    virtual ~IProcessThreads() = default;

    [[nodiscard]] virtual QVector<qint64> list(qint64 pid) = 0;
    [[nodiscard]] virtual std::optional<Thread> read(qint64 pid, qint64 tid) = 0;
    [[nodiscard]] virtual int lowest_nice(qint64 pid) = 0;
    [[nodiscard]] virtual bool set_nice(qint64 tid, int nice) = 0;
};

class ProcProcessThreads : public IProcessThreads {
public:
    struct Schedule {
        int nice = 0;
        qint64 start_ticks = 0;
    };

    [[nodiscard]] static std::optional<Schedule> parse_stat(const QByteArray& stat);
    [[nodiscard]] static int lowest_nice_of(const QByteArray& limits);

    [[nodiscard]] QVector<qint64> list(qint64 pid) override;
    [[nodiscard]] std::optional<Thread> read(qint64 pid, qint64 tid) override;
    [[nodiscard]] int lowest_nice(qint64 pid) override;
    [[nodiscard]] bool set_nice(qint64 tid, int nice) override;
};

}  // namespace lsw::host::platform
