#pragma once

#include "lsw/host/services/usb_status.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

namespace lsw::host::services {
class StatusHub;
class UsbPassthrough;
}

namespace lsw::host::manager {
class UsbDevicesPanel : public QWidget {
    Q_OBJECT
public:
    UsbDevicesPanel(services::StatusHub& status, services::UsbPassthrough& usb,
                    QWidget* parent = nullptr);

private:
    void rebuild();
    [[nodiscard]] static QString where(const services::UsbStatus::Device& device);
    [[nodiscard]] bool confirm_input_device(const services::UsbStatus::Device& device);
    [[nodiscard]] bool confirm_access(const services::UsbStatus::Device& device);

    services::StatusHub& status_;
    services::UsbPassthrough& usb_;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

}
