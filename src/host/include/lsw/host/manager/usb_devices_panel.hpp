#pragma once

#include "lsw/host/services/usb_status.hpp"

#include <QString>
#include <QWidget>

class QLabel;
class QTableWidget;

namespace lsw::host::services {
class StatusHub;
class UsbPassthrough;
}  // namespace lsw::host::services

namespace lsw::host::manager {

// The settings page's USB devices: what is plugged in, where each device
// is, and what can be done with it.
//
// Rebuilt from StatusHub's USB snapshot whenever that changes, and from
// UsbPassthrough's when an error appears or goes; it asks for nothing
// itself. Every action goes to UsbPassthrough, the one place that acts on
// devices.
class UsbDevicesPanel : public QWidget {
    Q_OBJECT
public:
    UsbDevicesPanel(services::StatusHub& status, services::UsbPassthrough& usb,
                    QWidget* parent = nullptr);

private:
    void rebuild();
    // What the "Where" column says, and whether it is a problem.
    [[nodiscard]] static QString where(const services::UsbStatus::Device& device);
    // Asks before a keyboard or mouse leaves this machine; true on a yes.
    [[nodiscard]] bool confirm_input_device(const services::UsbStatus::Device& device);
    // Asks before a device type is opened to this session; a disk's warning
    // says what raw access to a disk is.
    [[nodiscard]] bool confirm_access(const services::UsbStatus::Device& device);

    services::StatusHub& status_;
    services::UsbPassthrough& usb_;
    QTableWidget* table_ = nullptr;
    QLabel* foot_ = nullptr;
};

}  // namespace lsw::host::manager
