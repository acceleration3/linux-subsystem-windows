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
    // Microsoft's public KMS *install* key for Win11 Pro — lets setup
    // skip the "Insert product key" screen without activating the OS.
    // Documented at learn.microsoft.com/en-us/windows-server/get-started/kms-client-activation-keys.
    // The user activates with their own key (or stays unactivated)
    // after install completes.
    QString product_key = "W269N-WFGWX-YVC9B-4J6C9-T83GX";
    // 1-based WIM image index to install (which edition). Windows media
    // ships several editions in sources/install.wim; Setup is told which
    // via <ImageInstall>/<OSImage>/<InstallFrom>. Resolve with
    // WimEditionLister.
    int image_index = 1;
};

}  // namespace lsw::host::provision::install
