#pragma once

#include <QByteArray>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>

namespace lsw::host::provision::install {

// What the Microsoft Update Catalog says each host GPU's current Windows
// display driver is, so the guest's user-mode driver can be stamped with a
// version applications accept (DXGI reports that file version, and a
// driver reading 0.0.0.0 is refused by some of them).
//
// Given the text AdapterNames::render produced, it gives back the same
// lines with the GPU's version appended as a fourth tab field:
// "<vendor>\t<device>\t<name>\t<version>\n". One search a distinct
// vendor:device. A search that fails, or finds no driver, leaves its
// lines as they were: provision.ps1 then stamps the version of its own
// table. A lookup is one use of the object.
class DriverCatalog : public QObject {
    Q_OBJECT
public:
    // Gets ``url`` and calls ``done`` once, on the GUI thread, with the page
    // or, when it could not be had, an empty page and why.
    using Done = std::function<void(const QByteArray& page, const QString& error)>;
    using Fetch = std::function<void(const QUrl& url, Done done)>;

    // ``fetch`` empty is the catalog over the network.
    explicit DriverCatalog(Fetch fetch = {}, QObject* parent = nullptr);

    // The highest version among the page's result rows (<tr id="<guid>_R<n>">),
    // each row's title ending in its four-part version, compared number by
    // number; nullopt with no rows. Four-part numbers elsewhere on the page
    // are not drivers.
    [[nodiscard]] static std::optional<QString> newest(const QByteArray& page);

    // The catalog search for the PCI function ``vendor``:``device``, each
    // four hex digits.
    [[nodiscard]] static QUrl search_url(const QString& vendor, const QString& device);

    // Starts the lookup for ``adapter_names``; ``completed`` follows, once,
    // with the lines versioned. At once, from here, for no lines.
    void lookup(const QString& adapter_names);

Q_SIGNALS:
    void completed(const QString& versioned);

private:
    void answer(const QString& key, const QByteArray& page, const QString& error);
    void complete();

    Fetch fetch_;
    QNetworkAccessManager nam_;
    QString names_;
    // The distinct "<vendor>:<device>" not yet answered, and the versions found.
    QHash<QString, bool> pending_;
    QHash<QString, QString> versions_;
    bool started_ = false;
    bool completed_ = false;
};

}  // namespace lsw::host::provision::install
