#pragma once

namespace lsw::host::config {
class Settings;
class VmState;
}

namespace lsw::host::vm {

class LibvirtConnection;

// Adopts an already-defined libvirt domain into the manager's VmState.
//
// libvirt is the source of truth for whether an LSW VM exists — there is
// no JSON manifest on disk. On every start-up the manager asks libvirt
// for the well-known domain name and, if it is there, reconstructs the
// VmRecord (UUID from libvirt, paths derived from Settings).
class DomainImporter {
public:
    DomainImporter() = delete;

    // Looks up the LSW domain and, if found, appends a VmRecord for it
    // to ``vms``. Returns true if a domain was adopted. A closed libvirt
    // connection or a missing domain is not an error — it just means the
    // wizard hasn't run yet.
    [[nodiscard]] static bool import_existing(LibvirtConnection& conn,
                                              const config::Settings& settings,
                                              config::VmState& vms);
};

}  // namespace lsw::host::vm
