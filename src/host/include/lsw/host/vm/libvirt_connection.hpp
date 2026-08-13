#pragma once

#include <QObject>
#include <QString>

extern "C" {
typedef struct _virConnect* virConnectPtr;
}

namespace lsw::host::vm {

// RAII handle around ``virConnectOpen("qemu:///session")``. Rootless on
// purpose: LSW never escalates to system libvirt — every VM lives in
// the user's home and is fully revertible without touching system
// directories. The connection is thread-affine: own one per thread or
// guard externally.
class LibvirtConnection : public QObject {
    Q_OBJECT
public:
    explicit LibvirtConnection(QObject* parent = nullptr);
    ~LibvirtConnection() override;

    LibvirtConnection(const LibvirtConnection&) = delete;
    LibvirtConnection& operator=(const LibvirtConnection&) = delete;

    // Connect to ``qemu:///session``. Returns 0 on success, -1 on
    // failure (logged). Subsequent calls are no-ops while open.
    [[nodiscard]] int open();

    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return conn_ != nullptr; }

    [[nodiscard]] virConnectPtr raw() const noexcept { return conn_; }

    // Last libvirt error message, or empty string if none.
    [[nodiscard]] QString last_error() const;

private:
    virConnectPtr conn_ = nullptr;
};

}  // namespace lsw::host::vm
