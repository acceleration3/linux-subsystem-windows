#pragma once

#include "lsw/host/provision/debloat/debloat_level.hpp"

#include <QObject>
#include <QString>

namespace lsw::host::provision {

// Produces the trimmed install.wim the guest is installed from.
//
// Three steps, all on this machine and all before the VM exists: pull
// ``sources/install.wim`` out of the Windows ISO, take the chosen level
// out of it, and hand back the path to the result. The pipeline stages
// that file onto the payload ISO and the answer file installs from it,
// so the Windows ISO is never modified — no bootable-image authoring,
// and a source ISO that stays byte-identical for the next VM.
//
// The extraction shells out to 7z, as WimEditionLister already does and
// for the same reason: Windows media is UDF, which libisofs and xorriso
// do not read. Everything after that is wimlib and hivex in-process.
//
// Runs on a worker thread. Rewriting several gigabytes of WIM would
// otherwise freeze the wizard for minutes.
class DebloatStager : public QObject {
    Q_OBJECT
public:
    explicit DebloatStager(QObject* parent = nullptr);
    ~DebloatStager() override;

    DebloatStager(const DebloatStager&) = delete;
    DebloatStager& operator=(const DebloatStager&) = delete;
    DebloatStager(DebloatStager&&) = delete;
    DebloatStager& operator=(DebloatStager&&) = delete;

    // Trim ``image_index`` of the install.wim inside ``iso_path`` and
    // write a new bootable ISO around it, into ``output_dir``. Emits
    // exactly one of ``finished`` / ``failed``.
    //
    // ``DebloatLevel::None`` still runs: the setup bypasses live in the
    // offline registry and this domain has no TPM, so an untouched
    // image is one Windows Setup refuses outright.
    // ``file_name`` names the result; empty keeps the historical
    // ``windows-trimmed.iso``. It is a separate argument from the
    // directory because the tool that drives this lets people put the
    // image anywhere, under any name, and joining the two here would
    // make "the folder" and "the file" one field in the UI as well.
    void trim(const QString& iso_path, const QString& output_dir, int image_index,
              debloat::DebloatLevel level, const QString& file_name = {});

Q_SIGNALS:
    // Path to the rebuilt Windows ISO, which the domain boots in place
    // of the original. Written into ``output_dir``, not into the scratch
    // directory, which is gone by the time this arrives.
    void finished(const QString& iso_path);
    void failed(const QString& message);
    void progress(const QString& detail, int percent);
};

}  // namespace lsw::host::provision
