#pragma once

namespace lsw::host::config {
class Settings;
class VmState;
}

namespace lsw::host::vm {

class LibvirtConnection;

class DomainImporter {
public:
    DomainImporter() = delete;

    [[nodiscard]] static bool import_existing(LibvirtConnection& conn,
                                              const config::Settings& settings,
                                              config::VmState& vms);
};

}
