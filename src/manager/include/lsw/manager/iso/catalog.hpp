#pragma once

#include <QList>
#include <QString>

namespace lsw::manager::iso {

// One Windows edition Microsoft is currently advertising on its
// download portal. Populated at runtime by ``IsoSourceFetcher`` —
// scraped straight from microsoft.com so the wizard tracks whatever
// edition list MS happens to publish today.
struct WindowsEdition {
    QString display;                 // wizard combo box label
    QString version;                 // "Windows 11" / "Windows 10"
    QString release;                 // unused for live entries
    QList<int> product_edition_ids;  // ProductEditionId values
};

// Static helpers that don't require a network round trip.
class IsoCatalog {
public:
    [[nodiscard]] static QString virtio_win_url();
};

}  // namespace lsw::manager::iso
