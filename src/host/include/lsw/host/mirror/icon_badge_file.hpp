#pragma once

#include <QString>

namespace lsw::host::mirror {
class IconBadgeFile {
public:
    [[nodiscard]] const QString& path();

private:
    QString path_;
    bool tried_ = false;
};

}
