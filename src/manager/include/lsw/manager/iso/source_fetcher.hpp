#pragma once

#include "lsw/manager/iso/catalog.hpp"

#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

namespace lsw::manager::iso {

// Fetches the live list of Windows editions straight from Microsoft.
//
// The /software-download/windows11 and /software-download/windows10ISO
// pages each render a ``<select id="product-edition">`` whose
// ``<option>`` values are exactly the ``productEditionId`` integers
// Fido needs in stage 2. Scraping the HTML keeps us in lockstep with
// whatever Microsoft is currently advertising — when MS rolls a new
// release we get the new IDs without any code change.
//
// Async by design: emit ``ready`` with the consolidated list once both
// page fetches return, ``failed`` when both fail. Partial success is
// passed through (whichever pages did return contribute their entries).
class IsoSourceFetcher : public QObject {
    Q_OBJECT
public:
    explicit IsoSourceFetcher(QObject* parent = nullptr);

    // Kick off the fetches. Re-entrant — calling again abandons any
    // pending replies and starts fresh.
    void fetch();

Q_SIGNALS:
    void ready(const QList<WindowsEdition>& editions);
    void failed(const QString& message);

private Q_SLOTS:
    void on_page_finished();

private:
    void start_one(const QString& url, const QString& version_label);
    void maybe_emit();

    QNetworkAccessManager nam_;
    int pending_ = 0;
    QList<WindowsEdition> collected_;
    QStringList errors_;
};

}  // namespace lsw::manager::iso
