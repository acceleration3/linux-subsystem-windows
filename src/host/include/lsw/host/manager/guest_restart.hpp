#pragma once

namespace lsw::host::manager {

// What asking for the guest to be restarted on a new definition came to.
enum class RestartResult {
    // Begun: the definition is rewritten, and the guest is stopping (or, off,
    // starting) on it.
    Started,
    // A restart is already under way; the definition is rewritten again
    // before it starts the guest, so this request's changes are in it too.
    Pending,
    // Nothing was done: there is no guest, libvirt refused the definition, or
    // would not start it. The reason is the log's and the caller's to show.
    Refused,
};

}  // namespace lsw::host::manager
