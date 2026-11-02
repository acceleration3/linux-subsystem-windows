#pragma once

#include "lsw/host/app/cli_options.hpp"

#include <QStringList>

namespace lsw::host::app {
class CliParser {
public:
    CliParser() = delete;

    [[nodiscard]] static CliOptions parse(const QStringList& argv);

    [[nodiscard]] static QString normalise_file(const QString& argument, const QString& cwd,
                                                QString* error);
};

}
