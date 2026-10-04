#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace lsw::host::platform {

// Run a command-line tool to completion and say why when it does not
// complete. The blocking QProcess shape — start, wait, check the exit,
// quote stderr — that qemu-img, innoextract and the render server's
// device listing all need, written once so every one of them reports
// the same way.
//
// Blocks the calling thread for up to ``timeout_ms``; run it on a
// worker when that matters.
//
// ``stdout_out`` gets everything the tool printed on standard output,
// when given. ``error_out`` gets a sentence on every failure path —
// would not start, timed out (and was killed), crashed, or exited
// non-zero — ending with the last line of stderr when there was one.
[[nodiscard]] bool run_tool(const QString& program, const QStringList& args, int timeout_ms,
                            QByteArray* stdout_out, QString* error_out);

}  // namespace lsw::host::platform
