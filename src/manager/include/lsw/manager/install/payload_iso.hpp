#pragma once

#include <QString>
#include <QStringList>

namespace lsw::manager::install {

// Configures what goes onto the secondary ISO mounted alongside the
// Windows ISO. The autounattend.xml at the root drives the install
// (including all post-install bootstrap via its specialize-pass
// RunSynchronous block); the rest of the files live alongside it on
// the ISO root and are copied to ``C:\ProgramData\LSW\`` by the
// specialize block while the CDs are still mounted.
struct PayloadInputs {
    QString output_iso_path;     // where to write the resulting .iso
    QString autounattend_xml;    // already-rendered XML (string)
    QString lsw_msi_path;        // optional; empty = skip
    QString dxvk_archive_path;   // optional .tar.gz
    QString vkd3d_archive_path;  // optional .tar.gz
    QString agent_exe_path;      // optional; cross-built lsw-guest-agent.exe
};

// Builds the secondary install ISO. Uses ``xorriso`` (mkisofs) — small
// runtime dep, available everywhere libvirt is. Returns 0/-1 (logged).
class PayloadIsoBuilder {
public:
    [[nodiscard]] static int build(const PayloadInputs& in);

    // Visible for testing — what we put on the ISO.
    [[nodiscard]] static QStringList stage_files(const PayloadInputs& in,
                                                 const QString& staging_dir);
};

}  // namespace lsw::manager::install
