#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <optional>

class QImage;

namespace lsw::host::platform {
struct ClipboardPayload {
    std::optional<QString> text;
    QByteArray png;
    bool secret = false;

    [[nodiscard]] bool empty() const noexcept { return !text && png.isEmpty(); }
};

class HostClipboard : public QObject {
    Q_OBJECT
public:
    static constexpr qsizetype kMaxTextBytes = 4 * 1024 * 1024;
    static constexpr qsizetype kMaxImageBytes = 8 * 1024 * 1024;
    static constexpr qsizetype kMaxMessageChars = 15 * 1024 * 1024;
    static constexpr const char* kMarkerMime = "application/x-lsw-clipboard";
    static constexpr const char* kSecretMime = "x-kde-passwordManagerHint";

    using QObject::QObject;

    virtual void set(const ClipboardPayload& payload) = 0;
    virtual void clear_if_owned() = 0;

    [[nodiscard]] virtual bool working() const = 0;
    [[nodiscard]] virtual bool limited() const = 0;

    [[nodiscard]] static QByteArray png_of(const QImage& image);

Q_SIGNALS:
    void changed(const lsw::host::platform::ClipboardPayload& payload);
    void cleared();
    void state_changed();
};

}
