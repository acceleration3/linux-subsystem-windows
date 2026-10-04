#pragma once

#include <QString>

namespace lsw::host::provision::install {

// Inputs the wizard captures and bakes into ``autounattend.xml``.
struct AutounattendInputs {
    QString computer_name = "LSW-VM";
    QString locale = "en-US";
    QString timezone = "UTC";
    QString user_name = "lsw";
    QString user_password = "lsw";  // local user, dev convenience
    bool win11_bypass = true;       // LabConfig + reg overrides for Win11 hardware checks
    // The key Setup is given, which must match the edition installed
    // (ProductKeys). Empty leaves <ProductKey> out, and Setup asks.
    QString product_key;
    // 1-based WIM image index to install (which edition). Windows media
    // ships several editions in sources/install.wim; Setup is told which
    // via <ImageInstall>/<OSImage>/<InstallFrom>. Resolve with
    // WimEditionLister.
    int image_index = 1;
    // Guest path of the trimmed install.wim on the payload CD, or empty
    // to let Setup use the image on its own media. Empty is the right
    // default: an <InstallFrom><Path> naming a file that is not there
    // fails the install outright, with no fallback.
    QString install_from_path;
};

}  // namespace lsw::host::provision::install
