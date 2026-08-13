#pragma once

#include <QString>

namespace lsw::host::provision::install {

// Locates the guest's IVSHMEM driver on the HOST, so it can be staged
// onto the payload ISO and injected into the offline image.
//
// Why it has to happen here rather than as a guest setup step: the
// shared-memory channel is the transport. A driver delivered over the
// channel it provides is a bootstrap that only closes if a second
// transport exists to carry it — which is exactly the TCP fallback we
// are removing. The device has to be live on the first boot, before the
// agent starts.
//
// Where the files come from, in order:
//
//   1. ``$LSW_IVSHMEM_DRIVER_DIR`` — an unpacked driver directory. The
//      escape hatch: distributions, offline builds, and anyone who
//      already has the signed package.
//   2. The cache under the LSW data dir, populated by a previous run.
//   3. Extraction from a Looking Glass host package, if one is present
//      in the cache and an extractor is installed.
//
// Red Hat's virtio-win iso does not ship ivshmem.sys; the signed binary
// is bundled in the Looking Glass host package, which is an NSIS
// installer — hence needing an extractor rather than just unzipping.
class IvshmemDriver {
public:
    IvshmemDriver() = delete;

    // Absolute path to a directory containing ivshmem.inf / .cat / .sys,
    // or an empty string when the driver could not be found. Empty is
    // not fatal to provisioning: the install proceeds without the
    // shared-memory device, and the reason is logged once, loudly.
    [[nodiscard]] static QString locate(const QString& cache_dir);

    // Where ``locate`` caches an unpacked driver, and where a
    // hand-placed one is picked up: ``<cache_dir>/ivshmem``.
    [[nodiscard]] static QString cache_path(const QString& cache_dir);

    // True when ``dir`` holds the files an offline injection needs. A
    // .inf alone does not install; the catalog and the binary have to be
    // beside it.
    [[nodiscard]] static bool looks_like_driver_dir(const QString& dir);

    // Unpack a Looking Glass host installer into ``dest_dir`` and keep
    // the driver files. Returns false when no extractor is available
    // (logged) — NSIS needs 7z or innoextract; neither is a hard
    // dependency of LSW.
    [[nodiscard]] static bool extract_from_package(const QString& package_path,
                                                   const QString& dest_dir);
};

}  // namespace lsw::host::provision::install
