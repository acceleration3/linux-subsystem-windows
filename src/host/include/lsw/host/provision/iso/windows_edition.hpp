#pragma once

#include "lsw/host/provision/iso/windows_edition.hpp"
#include <QList>
#include <QString>

namespace lsw::host::provision::iso {

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

}  // namespace lsw::host::provision::iso
