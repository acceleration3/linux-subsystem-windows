#pragma once

#include <QString>

namespace lsw::host::platform {
class GuestBookmarkCleanup {
public:
    GuestBookmarkCleanup() = delete;

    static void run();

    static bool remove_from_places(const QString& path);

    static bool remove_from_gtk(const QString& path);
};

}  // namespace lsw::host::platform
