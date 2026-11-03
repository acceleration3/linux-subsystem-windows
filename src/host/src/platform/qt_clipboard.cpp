#include "lsw/host/platform/qt_clipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>

namespace lsw::host::platform {

QtClipboard::QtClipboard(QObject* parent)
    : HostClipboard(parent), clipboard_(QGuiApplication::clipboard()),
      limited_(QGuiApplication::platformName() != QLatin1String("xcb")) {
    connect(clipboard_, &QClipboard::dataChanged, this, [this] { on_data_changed(); });
}

void QtClipboard::set(const ClipboardPayload& payload) {
    auto* data = new QMimeData;
    if (payload.text) {
        data->setText(*payload.text);
    }
    if (!payload.png.isEmpty()) {
        data->setData(QStringLiteral("image/png"), payload.png);
    }
    data->setData(QString::fromLatin1(kMarkerMime), QByteArray("1"));
    if (payload.secret) {
        data->setData(QString::fromLatin1(kSecretMime), QByteArray("secret"));
    }
    owned_ = true;
    clipboard_->setMimeData(data, QClipboard::Clipboard);
}

void QtClipboard::clear_if_owned() {
    if (owned_) {
        owned_ = false;
        suppress_ = true;
        clipboard_->clear(QClipboard::Clipboard);
        suppress_ = false;
    }
}

void QtClipboard::on_data_changed() {
    if (suppress_) {
        return;
    }
    const QMimeData* data = clipboard_->mimeData(QClipboard::Clipboard);
    if (data == nullptr || data->formats().isEmpty()) {
        if (!owned_) {
            Q_EMIT cleared();
        }
        owned_ = false;
        return;
    }
    if (data->hasFormat(QString::fromLatin1(kMarkerMime))) {
        return;
    }
    owned_ = false;
    ClipboardPayload payload;
    payload.secret = data->data(QString::fromLatin1(kSecretMime)) == "secret";
    if (data->hasText()) {
        payload.text = data->text();
    }
    if (data->hasImage()) {
        payload.png = png_of(qvariant_cast<QImage>(data->imageData()));
        if (payload.png.size() > kMaxImageBytes) {
            payload.png.clear();
        }
    }
    if (payload.text && payload.text->toUtf8().size() > kMaxTextBytes) {
        payload.text.reset();
    }
    if (!payload.empty()) {
        Q_EMIT changed(payload);
    }
}

}
