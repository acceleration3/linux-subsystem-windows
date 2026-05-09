#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace lsw::manager::wizard {

// Setup-step types. The wizard parses an embedded ``setup-steps.json``
// into one or more Stages; stages run sequentially, every Step inside
// a single stage is dispatched in parallel. Lives outside ``SetupPage``
// so the Pipeline can own parsing/dispatch and the UI page consumes
// the parsed plan via a signal.

enum class StepKind { DownloadInstall, RunScript, DownloadExtract };

struct SetupStep {
    QString name;
    StepKind kind = StepKind::DownloadInstall;
    QString url;         // download_install / download_extract
    QString args;        // download_install only
    QString script;      // run_script / download_extract (optional)
    QString extract_to;  // download_extract only — empty = temp dir
    // (No per-step elevation flags: the agent runs as the elevated-
    // admin wizard user in-session, so every step already has the
    // rights msiexec / HKLM writes need.)
};

struct SetupStage {
    QVector<SetupStep> steps;
};

// Parse a setup-steps.json blob into a stage list. Returns empty list
// on parse error; ``error_out`` (if non-null) receives the message.
[[nodiscard]] QVector<SetupStage> parse_setup_plan(const QByteArray& json,
                                                    QString* error_out = nullptr);

// Resolve ``script_ref`` (e.g. "tune-windows") to the embedded .cmd
// body. Returns an empty string for unknown refs.
[[nodiscard]] QString resolve_script_ref(const QString& ref);

}  // namespace lsw::manager::wizard
