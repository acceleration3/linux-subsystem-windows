#pragma once

#include <QByteArray>

namespace lsw::host::apps {

// The Windows mark over a corner of a guest application's icon.
//
// The launcher entry's icon is also what the task manager shows for the
// application's mirrored windows (DesktopLauncher::entry_id), so with the
// mark a guest window is told apart at a glance from the host's own - the
// same Notepad, say, open on both sides.
class WindowsBadge {
public:
    // ``png`` drawn kSize square with the mark in its lower right corner,
    // as PNG bytes; empty for bytes that are no image.
    [[nodiscard]] static QByteArray applied(const QByteArray& png);

    static constexpr int kSize = 256;
};

}  // namespace lsw::host::apps
