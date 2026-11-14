#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <optional>

namespace lsw::host::platform {
class ICpuScope {
public:
    virtual ~ICpuScope() = default;

    [[nodiscard]] virtual bool holds(qint64 pid) = 0;
    [[nodiscard]] virtual std::optional<int> weight(qint64 pid) = 0;
    [[nodiscard]] virtual bool enter(qint64 pid, int weight, QString* error_out) = 0;
    [[nodiscard]] virtual bool set_weight(qint64 pid, int weight, QString* error_out) = 0;
};

class SystemdCpuScope : public ICpuScope {
public:
    [[nodiscard]] static QString unit_name(qint64 pid);
    [[nodiscard]] static QString group_of(const QByteArray& cgroup);

    [[nodiscard]] bool holds(qint64 pid) override;
    [[nodiscard]] std::optional<int> weight(qint64 pid) override;
    [[nodiscard]] bool enter(qint64 pid, int weight, QString* error_out) override;
    [[nodiscard]] bool set_weight(qint64 pid, int weight, QString* error_out) override;
};

}
