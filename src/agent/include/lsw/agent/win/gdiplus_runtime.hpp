#pragma once

namespace lsw::agent::win {

// Process-wide GDI+ startup. Every PNG encode needs GDI+ initialised
// exactly once; nothing shuts it down, because it lives as long as the
// agent process does.
class GdiPlusRuntime {
public:
    GdiPlusRuntime() = delete;

    // Idempotent. Safe to call from any thread.
    static void ensure_started();
};

}  // namespace lsw::agent::win
