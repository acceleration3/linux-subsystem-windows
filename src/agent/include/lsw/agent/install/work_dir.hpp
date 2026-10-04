#pragma once

#include <string>

namespace lsw::agent::install {

// Where staged job files live: downloaded installers, extracted
// archives, generated .cmd scripts.
//
// Deliberately NOT %TEMP%. The agent runs as SYSTEM, for which
// GetTempPath returns a directory (C:\Windows\Temp, or under
// ``C:\Windows\System32\config\systemprofile``) that a job started as
// the session's user cannot read, so cmd.exe and tar would fail with
// access denied. ``C:\ProgramData\LSW`` carries an inheritable
// Users:Modify ACE granted by the autounattend, so a work subdirectory
// under it is readable and writable by both.
class WorkDir {
public:
    WorkDir() = delete;

    // Path with a trailing separator. Creates the directory chain
    // best-effort on every call (idempotent).
    [[nodiscard]] static std::wstring path();

    // ``<work>/lsw-<job_id><suffix>`` — the naming every job stages under.
    [[nodiscard]] static std::wstring staged_path(const std::string& job_id,
                                                  const std::wstring& suffix);
};

}  // namespace lsw::agent::install
