#pragma once

#include <QString>

namespace lsw::host::provision::iso {

// One language/SKU returned by ``getskuinformationbyproductedition``.
// Matches the JSON shape Microsoft sends — ``id`` is what feeds the
// next call (``GetProductDownloadLinksBySku``).
struct MsSku {
    QString id;
    QString language;      // "en-US"
    QString product_name;  // ``LocalizedProductDisplayName`` (whitespace-normalized)
    QString file_name;     // ``FriendlyFileNames[0]``
};

}  // namespace lsw::host::provision::iso
