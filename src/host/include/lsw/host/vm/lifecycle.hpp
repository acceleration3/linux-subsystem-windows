#pragma once

#include "lsw/host/config/vm_state.hpp"
#include "lsw/host/vm/domain_description.hpp"
#include "lsw/host/vm/domain_handle.hpp"
#include "lsw/host/vm/domain_xml_builder.hpp"
#include "lsw/host/vm/libvirt_connection.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QUuid>
#include <functional>
#include <optional>

namespace lsw::host::vm {
class Lifecycle : public QObject {
    Q_OBJECT
public:
    explicit Lifecycle(LibvirtConnection& conn, QObject* parent = nullptr);

    void set_before_start(std::function<void()> hook) { before_start_ = std::move(hook); }

    struct GpuLogEnv {
        QString dxvk;
        QString vkd3d;
    };
    void set_gpu_log_env(std::function<GpuLogEnv()> provider) { gpu_log_env_ = std::move(provider); }

    void set_avgpu_log_path(QString path) { avgpu_log_ = std::move(path); }

    [[nodiscard]] int create_disk(const QString& path, int size_gib);

    [[nodiscard]] static bool grow_disk(const QString& path, int size_gib, QString* error_out);

    [[nodiscard]] int define_and_start(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    [[nodiscard]] const QString& last_error() const noexcept { return last_error_; }

    [[nodiscard]] int start(const QUuid& uuid, bool first_boot_done = false);

    [[nodiscard]] int redefine(const config::VmRecord& vm, const DomainXmlInputs& inputs);

    [[nodiscard]] int shutdown(const QUuid& uuid);

    [[nodiscard]] int force_off(const QUuid& uuid);

    [[nodiscard]] int undefine(const QUuid& uuid);

    [[nodiscard]] bool remove_existing(const QString& name, QString* error_out);

    [[nodiscard]] int domain_id(const QUuid& uuid) const;

    [[nodiscard]] bool is_running(const QUuid& uuid) const;

    [[nodiscard]] QString domain_xml(const QUuid& uuid, bool stored = false) const;

    [[nodiscard]] int vnc_port(const QUuid& uuid) const;

    void eject_install_media(const QUuid& uuid);

    [[nodiscard]] bool set_display_modes(const QUuid& uuid, const QString& modes,
                                         QString* error_out);

    [[nodiscard]] std::optional<QString> display_modes(const QUuid& uuid,
                                                       QString* error_out) const;

    [[nodiscard]] bool set_display_refresh(const QUuid& uuid, quint32 millihz, QString* error_out);

    [[nodiscard]] std::optional<quint32> display_refresh(const QUuid& uuid,
                                                         QString* error_out) const;

    [[nodiscard]] static QString usb_hostdev_xml(const DomainDescription::UsbDevice& device);

    [[nodiscard]] bool attach_usb(const QUuid& uuid, const DomainDescription::UsbDevice& device,
                                  QString* error_out);
    [[nodiscard]] bool detach_usb(const QUuid& uuid, const DomainDescription::UsbDevice& device,
                                  QString* error_out);

private:
    int fail(const char* call);
    QString last_error_;
    std::function<void()> before_start_;
    std::function<GpuLogEnv()> gpu_log_env_;
    QString avgpu_log_;

    [[nodiscard]] DomainHandle lookup(const QUuid& uuid) const;
    [[nodiscard]] std::optional<QJsonValue> qmp(const QUuid& uuid, const QJsonObject& command,
                                                QString* error_out) const;
    [[nodiscard]] bool fill_accel_virt(DomainXmlInputs& inputs);
    [[nodiscard]] bool retarget_host(const QUuid& uuid);

    LibvirtConnection& conn_;
};

}
