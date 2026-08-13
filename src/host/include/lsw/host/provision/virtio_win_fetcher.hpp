#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::provision {

// Obtains a verified ``virtio-win.iso``.
//
// Its drivers are injected into Windows Setup so the guest installs
// straight onto a virtio disk — get this wrong and Setup reports "no
// drives found" with no other clue.
//
// Which is exactly what used to happen: fedorapeople sits behind an
// anti-bot wall that serves a ~4 KB HTML challenge page to
// QNetworkAccessManager, and the manager cheerfully saved that page as
// virtio-win.iso. Hence two deliberate choices here — shell out to
// ``curl`` (whose default user agent passes the wall) and verify the
// ISO9660 magic before accepting the bytes.
class VirtioWinFetcher : public QObject {
    Q_OBJECT
public:
    explicit VirtioWinFetcher(QObject* parent = nullptr);

    // Reuse ``preferred_path`` if it is already a valid ISO, else a
    // cached copy in ``cache_dir``, else download. Emits exactly one of
    // ``finished`` / ``failed``.
    void fetch(const QString& preferred_path, const QString& cache_dir);

    // Size sane AND the ISO9660 primary-volume-descriptor magic
    // ("CD001" at 0x8001). Public because callers sometimes want to
    // validate a user-supplied path before starting anything.
    [[nodiscard]] static bool looks_like_iso(const QString& path);

Q_SIGNALS:
    // ``fraction`` is negative when the total size is unknown.
    void progress(double fraction, const QString& detail);
    void finished(const QString& iso_path);
    void failed(const QString& message);

private:
    // HEAD first, so the UI can show a real percentage rather than a
    // bare byte counter.
    void probe_size_then_download(const QString& url, const QString& dest);
    void download(const QString& url, const QString& dest, qint64 total_bytes);
};

}  // namespace lsw::host::provision
