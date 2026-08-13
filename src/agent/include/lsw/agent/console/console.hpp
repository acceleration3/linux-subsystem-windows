#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsw::agent::console {

// Manages an embedded ``cmd.exe`` session whose stdin / stdout /
// stderr are piped into the agent. Used by the manager's "Command
// Line" window: the manager opens a session, types lines (forwarded
// over the ``console_input`` RPC), and receives output bytes as
// pushed ``console_output`` events on the bidirectional channel.
//
// Multiple concurrent sessions are supported — each one is a
// separately-spawned cmd.exe process with its own pipes and reader
// thread. ``session_id`` strings are short opaque tokens generated
// internally; the manager echoes them back on input/close.
class Console {
public:
    // Notification kinds. ``output`` carries raw stdout/stderr bytes
    // (cmd.exe merges them since we point both pipes at the same
    // write handle). ``closed`` is emitted exactly once per session
    // when the cmd.exe process exits — after that, no more events
    // for that session_id and ``write`` returns false.
    enum class Kind {
        Output,
        Closed,
    };

    struct Note {
        Kind kind;
        std::string session_id;
        std::vector<std::uint8_t> data;  // raw bytes for Output; empty for Closed
        int exit_code = 0;                // populated for Closed
    };


    // Spawn a fresh ``cmd.exe`` (the user's local %COMSPEC% if set,
    // otherwise the system one) with stdin/stdout/stderr redirected
    // to pipes. Returns the new session id, or an empty string on
    // failure (CreatePipe / CreateProcessW errors get logged).
    [[nodiscard]] std::string open();

    // Append ``data`` to the session's stdin. Newlines are passed
    // through verbatim — cmd.exe is line-buffered so the typical
    // pattern is "the user typed a line; send it + ``\r\n``".
    // Returns ``false`` if the session is gone (process exited).
    [[nodiscard]] bool write(const std::string& session_id, const std::string& data);

    // Terminate the session. Idempotent. Subsequent ``write`` /
    // ``close`` on the same id are no-ops.
    void close(const std::string& session_id);

    // Drain queued notifications. Host_endpoint calls this on its
    // per-tick event push.
    [[nodiscard]] std::vector<Note> collect_pending();

    Console();
    ~Console();
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

private:

    struct Impl;
    Impl* impl_;
};

}  // namespace lsw::agent::console
