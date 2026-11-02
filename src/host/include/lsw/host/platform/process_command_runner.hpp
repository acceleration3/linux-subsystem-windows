#pragma once

#include "lsw/host/platform/command_runner.hpp"

namespace lsw::host::platform {
class ProcessCommandRunner : public CommandRunner {
public:
    [[nodiscard]] bool available(const QString& program) const override;

    [[nodiscard]] Output run(const QString& program, const QStringList& arguments) override;
};

}  // namespace lsw::host::platform
