#pragma once

#include <QString>

namespace lsw::host::provision {

// What a single setup step does inside the guest. Each maps 1:1 to an
// agent RPC.
enum class StepKind { DownloadInstall, RunScript, DownloadExtract };

// One unit of post-install setup work.
//
// No per-step elevation flag: the agent is a LocalSystem service and
// runs a step as SYSTEM in session 0 unless asked for a session, so
// every step already has the rights msiexec and HKLM writes need. The
// flip side: a step must not write HKCU, which would be SYSTEM's hive —
// per-user defaults go in the autounattend's DefaultUser.ps1.
struct SetupStep {
    QString name;
    StepKind kind = StepKind::DownloadInstall;
    QString url;         // download_install / download_extract
    QString args;        // download_install only
    QString script;      // run_script / download_extract (optional)
    QString extract_to;  // download_extract only — empty = temp dir
};

}  // namespace lsw::host::provision
