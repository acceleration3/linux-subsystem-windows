#pragma once

#include "lsw/host/services/usb_status.hpp"

#include <QWidget>

class QFrame;
class QLabel;

namespace lsw::host::services {
class StatusHub;
class UsbPassthrough;
}
namespace lsw::host::ui {
class ItemList;
class ScrollPage;
}

namespace lsw::host::manager {

class WarningStrip;

class UsbPage : public QWidget {
    Q_OBJECT
public:
    UsbPage(services::StatusHub& status, services::UsbPassthrough& usb, QWidget* parent = nullptr);

    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();
    void start_requested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    struct Section {
        QFrame* frame = nullptr;
        ui::ItemList* list = nullptr;
    };

    [[nodiscard]] Section make_section(const QString& name, const QString& caption,
                                       const QString& foot = {});
    void rebuild();
    void add_device(ui::ItemList* list, const services::UsbStatus::Device& device);
    void add_plain(ui::ItemList* list, const QString& text);
    [[nodiscard]] QWidget* action_for(const services::UsbStatus::Device& device);
    [[nodiscard]] bool confirm_input_device(const services::UsbStatus::Device& device);
    [[nodiscard]] bool confirm_access(const services::UsbStatus::Device& device);

    services::StatusHub& status_;
    services::UsbPassthrough& usb_;
    WarningStrip* off_strip_ = nullptr;
    Section guest_;
    Section host_;
    Section remembered_;
};

}
