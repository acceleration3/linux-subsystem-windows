#include "lsw/host/platform/host_clipboard.hpp"

#include <QBuffer>
#include <QImage>

namespace lsw::host::platform {

QByteArray HostClipboard::png_of(const QImage& image) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (image.isNull() || !buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return bytes;
}

}
