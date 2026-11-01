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

    // One file argument as a local absolute path: a ``file://`` URL
    // through QUrl::toLocalFile, a relative path against ``cwd`` (the call
    // may be forwarded to a manager that is somewhere else). Empty, with
    // the reason in ``error``, for a URL of another kind or a path that
    // does not exist.
    [[nodiscard]] static QString normalise_file(const QString& argument, const QString& cwd,
                                                QString* error);
};

}  // namespace lsw::host::app
