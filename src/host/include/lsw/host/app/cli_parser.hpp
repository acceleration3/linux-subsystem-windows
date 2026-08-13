#pragma once

#include "lsw/host/app/cli_options.hpp"

#include <QStringList>

namespace lsw::host::app {

// Turns argv into CliOptions. Separate from CliOptions so the option
// *shape* (a plain value type, trivially constructible in tests) is not
// entangled with the Qt parser that happens to fill it in today.
class CliParser {
public:
    CliParser() = delete;

    // Parse ``QApplication::arguments()``. On ``--help`` or a malformed
    // invocation the parser writes to stderr and exits the process via
    // QCommandLineParser::process — which is what a CLI tool wants.
    [[nodiscard]] static CliOptions parse(const QStringList& argv);
};

}  // namespace lsw::host::app
