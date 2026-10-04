#pragma once

#include <QString>

namespace lsw::host::provision::install {

// The key the answer file gives Setup, by the image's EDITIONID.
//
// Setup's product-key step does not pick the image by <InstallFrom>
// alone: it matches the key's edition against the images in the
// install.wim, and a key no image matches is the same as no key, so it
// stops on its "Product key" page. A Pro key on Home-only media
// (trimmed media holds one edition) is exactly that.
//
// These are Microsoft's public keys: the KMS client keys where the
// edition has one, the generic install keys for the Home editions,
// which have none. They install, they do not activate; the user brings
// their own key afterwards or stays unactivated.
class ProductKeys {
public:
    ProductKeys() = delete;

    // The key for an EDITIONID such as "Professional" or "Core" (case
    // does not matter), or empty for one this table does not know.
    [[nodiscard]] static QString for_edition(const QString& edition_id);

    // The key used when the edition was never read, as on the Microsoft
    // download path: Pro's. On media with several editions it also
    // chooses the Pro image, which is the one LSW wants there.
    [[nodiscard]] static QString fallback();
};

}  // namespace lsw::host::provision::install
