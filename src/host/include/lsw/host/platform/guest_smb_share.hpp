#pragma once

#include <QString>

namespace lsw::host::platform {

// The guest's SMB share as this host reaches it, and the one place its
// address is spelled.
//
// domain.xml forwards host 127.0.0.1:5445 to the guest's :445, so every
// file-manager bookmark points there. Both bookmark writers also need
// to recognise their own entry again — to refresh a rotated password
// rather than add a second line — and they do it by the host and port,
// which stay the same whatever the credentials or the share.
class GuestSmbShare {
public:
    GuestSmbShare() = delete;

    // ``smb://<username>:<password>@127.0.0.1:5445/<share>``, encoded.
    [[nodiscard]] static QString url(const QString& username, const QString& password,
                                     const QString& share);

    // What identifies a bookmark URL as one of ours: "@127.0.0.1:5445/".
    [[nodiscard]] static QString signature();

    // The share name for the Files page's scope setting: the user
    // profiles for "profile", the whole of C: otherwise.
    [[nodiscard]] static QString share_for_scope(const QString& scope);
};

}  // namespace lsw::host::platform
