#pragma once

#include <QString>

namespace lsw::host::platform {

// Registers a "Windows" entry in KDE Dolphin's Places sidebar that
// points at the guest's ``C$`` admin share via KIO's userspace
// ``smb://`` worker. This intentionally avoids the kernel cifs.ko
// path: a real CIFS mount surfaces in /proc/self/mountinfo, and
// the moment any file manager walks the mounted directory the
// kernel auto-submounts every Windows junction (Documents and
// Settings, ProgramData\Application Data, ...) as a fresh CIFS
// mount of its own. Each submount then pollutes Dolphin's Remote
// sidebar with the junction name. kio_smb handles SMB ops in
// userspace on each click, so nothing reaches mountinfo and there
// is exactly one entry, titled "Windows".
//
// Connectivity preconditions (provided by other parts of LSW):
//   * domain.xml's hostfwd ``tcp:127.0.0.1:5445-:445`` exposes
//     the guest's SMB endpoint on the host loopback.
//   * The autounattend specialize pass writes
//     ``LocalAccountTokenFilterPolicy=1`` so the wizard user
//     retains admin on remote logons (otherwise ``C$`` returns
//     STATUS_ACCESS_DENIED) and opens TCP/445 in the firewall.
//
// XBEL file: ``~/.local/share/user-places.xbel`` (KDE-specific).
// The entry is idempotent — re-running the wizard never produces
// duplicates because we match on URL prefix before adding.
class DolphinPlaces {
public:
    // Adds an ``smb://<username>:<password>@127.0.0.1:5445/C$``
    // bookmark titled "Windows" to user-places.xbel. Idempotent.
    // Returns true on success (or when the entry already exists).
    [[nodiscard]] static bool register_windows_bookmark(const QString& username,
                                                        const QString& password,
                                                        const QString& share
                                                        = QStringLiteral("C$"));

    // Takes it out again, for when the setting is turned off. A
    // bookmark that outlives the thing it points at is worse than never
    // having written one.
    static void remove_windows_bookmark();
};

}  // namespace lsw::host::platform
