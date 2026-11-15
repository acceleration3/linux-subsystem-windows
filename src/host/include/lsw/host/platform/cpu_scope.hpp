#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>
#include <functional>
#include <optional>

namespace lsw::host::platform {
class ICpuScope {
public:
    using Done = std::function<void(bool done, const QString& error)>;

    virtual ~ICpuScope() = default;

    [[nodiscard]] virtual bool holds(qint64 pid) = 0;
    [[nodiscard]] virtual std::optional<int> weight(qint64 pid) = 0;
    virtual void enter(qint64 pid, int weight, Done done) = 0;
    virtual void set_weight(qint64 pid, int weight, Done done) = 0;
};

class SystemdCpuScope : public ICpuScope {
public:
    explicit SystemdCpuScope(QString prefix);

    [[nodiscard]] QString unit_name(qint64 pid) const;
    [[nodiscard]] static QString group_of(const QByteArray& cgroup);

    [[nodiscard]] bool holds(qint64 pid) override;
    [[nodiscard]] std::optional<int> weight(qint64 pid) override;
    void enter(qint64 pid, int weight, Done done) override;
    void set_weight(qint64 pid, int weight, Done done) override;

private:
    void call(const QString& method, const QVariantList& arguments, Done done);

    QString prefix_;
    QObject pending_;
};

}  // namespace lsw::host::platform
