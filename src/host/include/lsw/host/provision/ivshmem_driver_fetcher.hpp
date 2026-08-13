#pragma once

#include <QObject>
#include <QString>

namespace lsw::host::provision {

// Gets a usable ivshmem driver onto the HOST, before the payload ISO is
// built.
//
// It has to be the host, and that is the whole point of this class. The
// shared-memory device IS the transport: a driver fetched by the guest
// arrives over the channel it is supposed to provide, which only works
// if a second transport exists to carry it — the TCP fallback we are
// trying to retire. Fetching it here, before the VM exists, is what
// breaks that circle, and it is what turns "the device mysteriously
// isn't there after install" into an ordinary download failure the
// wizard can show and retry.
//
// Everything downstream already worked: ``IvshmemDriver::locate`` scans
// the data dir for exactly this package, ``extract_from_package``
// unpacks it, and ``PayloadIsoStager`` stages the result. Nothing was
// putting a package there.
//
// Not fatal when it fails. Provisioning continues without the device,
// exactly as it did before, because a machine with no extractor
// installed should still be able to install Windows — it just cannot
// mirror windows or carry a command stream until the driver is
// supplied. The failure is reported rather than hidden.
class IvshmemDriverFetcher : public QObject {
    Q_OBJECT
public:
    explicit IvshmemDriverFetcher(QObject* parent = nullptr);

    // If a driver is already locatable, finish immediately; otherwise
    // download the Looking Glass host package into ``cache_dir`` and
    // unpack it. Emits exactly one of ``finished`` / ``failed``.
    void fetch(const QString& cache_dir);

Q_SIGNALS:
    // ``fraction`` is negative when the total size is unknown.
    void progress(double fraction, const QString& detail);
    // Absolute path to an unpacked driver directory.
    void finished(const QString& driver_dir);
    void failed(const QString& message);

private:
    void download_package(const QString& cache_dir);
};

}  // namespace lsw::host::provision
