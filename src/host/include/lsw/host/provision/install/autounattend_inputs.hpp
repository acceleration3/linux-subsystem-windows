#pragma once

#include <QString>

namespace lsw::host::provision::install {
struct AutounattendInputs {
    QString computer_name = "LSW-VM";
    QString locale = "en-US";
    QString timezone = "UTC";
    QString user_name = "lsw";
    QString user_password = "lsw";
    bool win11_bypass = true;
    QString product_key;
    int image_index = 1;
    QString install_from_path;
    QString release;
    QString version;
};

}
