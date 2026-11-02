#pragma once

#include <QObject>
#include <QString>

extern "C" {
typedef struct _virConnect* virConnectPtr;
}

namespace lsw::host::vm {
class LibvirtConnection : public QObject {
    Q_OBJECT
public:
    explicit LibvirtConnection(QObject* parent = nullptr);
    ~LibvirtConnection() override;

    LibvirtConnection(const LibvirtConnection&) = delete;
    LibvirtConnection& operator=(const LibvirtConnection&) = delete;

    [[nodiscard]] int open();

    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept { return conn_ != nullptr; }

    [[nodiscard]] virConnectPtr raw() const noexcept { return conn_; }

    [[nodiscard]] QString last_error() const;

private:
    virConnectPtr conn_ = nullptr;
};

}
