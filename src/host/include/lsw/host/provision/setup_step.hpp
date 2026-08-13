#pragma once

#include <QString>

namespace lsw::host::provision {

// What a single setup step does inside the guest. Each maps 1:1 to an
// agent RPC.
enum class StepKind { DownloadInstall, RunScript, DownloadExtract };

// One unit of post-install setup work.
//
// No per-step elevation flag: the agent runs as the elevated-admin
// wizard user in-session, so every step already has the rights that
// msiexec / HKLM writes need.
struct SetupStep {
    QString name;
    StepKind kind = StepKind::DownloadInstall;
    QString url;         // download_install / download_extract
    QString args;        // download_install only
    QString script;      // run_script / download_extract (optional)
    QString extract_to;  // download_extract only — empty = temp dir
};

}  // namespace lsw::host::provision
