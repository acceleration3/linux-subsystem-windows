#pragma once

#include <QString>
#include <QStringList>

namespace lsw::host::platform {
class CommandRunner {
public:
    struct Output {
        bool ok = false;
        QString text;
    };

    CommandRunner() = default;
    virtual ~CommandRunner() = default;
    CommandRunner(const CommandRunner&) = delete;
    CommandRunner& operator=(const CommandRunner&) = delete;

    [[nodiscard]] virtual bool available(const QString& program) const = 0;

    [[nodiscard]] virtual Output run(const QString& program, const QStringList& arguments) = 0;
};

}  // namespace lsw::host::platform
