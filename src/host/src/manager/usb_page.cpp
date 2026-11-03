#include "lsw/host/manager/usb_page.hpp"

#include "lsw/host/manager/warning_strip.hpp"
#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/services/usb_passthrough.hpp"
#include "lsw/host/ui/item_list.hpp"
#include "lsw/host/ui/item_row.hpp"
#include "lsw/host/ui/panel.hpp"
#include "lsw/host/ui/scroll_page.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using services::UsbStatus;
using ui::ItemRow;
using ui::LineIcon;
using ui::Theme;
using ui::Widgets;

constexpr int kActionWidth = 132;
constexpr const char* kFocusKey = "usbFocus";
constexpr const char* kDeviceKey = "usbKey";

LineIcon::Shape icon_for(const UsbStatus::Device& device) {
    if (device.is_hid) {
        return LineIcon::Shape::Hid;
    }
    return device.is_mass_storage ? LineIcon::Shape::Disk : LineIcon::Shape::Usb;
}

}

UsbPage::UsbPage(services::StatusHub& status, services::UsbPassthrough& usb, QWidget* parent)
    : QWidget(parent), status_(status), usb_(usb) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    off_strip_ = new WarningStrip;
    off_strip_->set_message(false, QStringLiteral("The guest is off."),
                            QStringLiteral("Start it to pass a device; Always devices go to it as "
                                           "it starts."));
    connect(off_strip_->add_button(QStringLiteral("Start the guest"), Theme::Role::kGo),
            &QPushButton::clicked, this, &UsbPage::start_requested);
    root->addWidget(off_strip_);

    auto* scroll = new ui::ScrollPage;
    root->addWidget(scroll, 1);
    auto* column = scroll->column();

    guest_ = make_section(QStringLiteral("In Windows"),
                          QStringLiteral("gone from this machine until you return it"));
    host_ = make_section(QStringLiteral("On this machine"), QStringLiteral("plugged in now"));
    remembered_ = make_section(
        QStringLiteral("Remembered"), QStringLiteral("not plugged in"),
        QStringLiteral("Always passes a device to Windows whenever it is plugged in and the guest "
                       "is running."));
    column->addWidget(guest_.frame);
    column->addWidget(host_.frame);
    column->addWidget(remembered_.frame);
    column->addStretch(1);

    connect(&status_, &services::StatusHub::changed, this, [this](services::StatusHub::Area area) {
        if (area == services::StatusHub::Area::Usb) {
            rebuild();
            Q_EMIT summary_changed();
        }
    });
    connect(&usb_, &services::UsbPassthrough::changed, this, &UsbPage::rebuild);
    rebuild();
}

void UsbPage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    status_.set_detail(services::StatusHub::Area::Usb, true);
}

void UsbPage::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    status_.set_detail(services::StatusHub::Area::Usb, false);
}

UsbPage::Section UsbPage::make_section(const QString& name, const QString& caption,
                                       const QString& foot) {
    auto panel = ui::Panel::area(name, caption);
    panel.body->setContentsMargins(0, 0, 0, 0);
    panel.body->setSpacing(0);
    Section section;
    section.frame = panel.frame;
    section.list = new ui::ItemList;
    panel.body->addWidget(section.list);
    if (!foot.isEmpty()) {
        panel.add_foot(foot);
    }
    return section;
}

QString UsbPage::summary() const {
    const UsbStatus& snapshot = status_.usb();
    if (!snapshot.read) {
        return QStringLiteral("-");
    }
    int in_guest = 0;
    int on_host = 0;
    int remembered = 0;
    for (const auto& device : snapshot.devices) {
        in_guest += device.place == UsbStatus::Place::Guest ? 1 : 0;
        on_host += device.place == UsbStatus::Place::Host && device.present ? 1 : 0;
        remembered += device.place == UsbStatus::Place::Absent ? 1 : 0;
    }
    return QStringLiteral("%1 in Windows · %2 on this machine · %3 remembered")
        .arg(in_guest)
        .arg(on_host)
        .arg(remembered);
}

bool UsbPage::confirm_input_device(const UsbStatus::Device& device) {
    return QMessageBox::question(
               this, QStringLiteral("Pass a keyboard or mouse"),
               QStringLiteral("%1 looks like a keyboard, a mouse or another input device. While "
                              "it is in the guest it stops working on this machine, until you "
                              "return it.\n\nPass it anyway?")
                   .arg(device.name)) == QMessageBox::Yes;
}

bool UsbPage::confirm_access(const UsbStatus::Device& device) {
    QString text = QStringLiteral("Letting LSW open %1 (%2) asks for your password, and applies to "
                                  "every device of this kind from now on.")
                       .arg(device.name, device.type_id());
    if (device.is_mass_storage) {
        text += QStringLiteral("\n\nThis is a disk. Access gives this desktop session raw access to "
                               "every disk of this type, without a mount: anything it reads or "
                               "writes bypasses this machine's file permissions.");
    }
    return QMessageBox::question(this, QStringLiteral("Allow access"), text) == QMessageBox::Yes;
}

void UsbPage::add_plain(ui::ItemList* list, const QString& text) {
    auto* row = new ItemRow(LineIcon::Shape::Usb);
    row->set_name(text);
    row->set_dim(true);
    list->add(row);
}

QWidget* UsbPage::action_for(const UsbStatus::Device& device) {
    const QString identity = device.identity;
    const int bus = device.bus;
    const int number = device.device;
    const bool running = status_.usb().guest_running;

    auto* button = new QPushButton;
    button->setCursor(Qt::PointingHandCursor);
    if (device.place == UsbStatus::Place::Absent) {
        button->setText(QStringLiteral("Forget it"));
        button->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
        connect(button, &QPushButton::clicked, this, [this, identity] {
            QMetaObject::invokeMethod(
                this, [this, identity] { usb_.set_always(identity, false); }, Qt::QueuedConnection);
        });
    } else if (usb_.busy(identity, bus, number)) {
        button->setText(device.attached() ? QStringLiteral("Returning...")
                                          : QStringLiteral("Passing..."));
        button->setEnabled(false);
    } else if (device.attached()) {
        button->setText(QStringLiteral("Return"));
        connect(button, &QPushButton::clicked, this, [this, identity, bus, number] {
            QMetaObject::invokeMethod(
                this, [this, identity, bus, number] { usb_.detach(identity, bus, number); },
                Qt::QueuedConnection);
        });
    } else if (!device.accessible && !device.mounted) {
        const bool waiting = usb_.granting(identity, bus, number);
        button->setText(waiting ? QStringLiteral("Waiting") : QStringLiteral("Allow"));
        button->setEnabled(!waiting);
        connect(button, &QPushButton::clicked, this, [this, identity, bus, number, device] {
            if (!confirm_access(device)) {
                return;
            }
            QMetaObject::invokeMethod(
                this, [this, identity, bus, number] { usb_.grant_access(identity, bus, number); },
                Qt::QueuedConnection);
        });
    } else {
        button->setText(QStringLiteral("Pass to Windows"));
        button->setEnabled(running && !device.mounted);
        connect(button, &QPushButton::clicked, this, [this, identity, bus, number, device] {
            if (device.is_hid && !confirm_input_device(device)) {
                return;
            }
            QMetaObject::invokeMethod(
                this, [this, identity, bus, number] { usb_.attach(identity, bus, number); },
                Qt::QueuedConnection);
        });
    }
    button->setProperty(kFocusKey, QStringLiteral("action"));
    button->setProperty(kDeviceKey, device.key());
    return button;
}

void UsbPage::add_device(ui::ItemList* list, const UsbStatus::Device& device) {
    auto* row = new ItemRow(icon_for(device));
    row->set_name(device.name);
    row->set_sub(device.type_id());
    row->set_dim(device.place == UsbStatus::Place::Absent);

    const QString error = usb_.error_for(device.identity, device.bus, device.device);
    if (!error.isEmpty()) {
        row->set_note(error, Theme::Tone::kBad);
    } else if (device.place == UsbStatus::Place::Host) {
        if (device.mounted) {
            row->set_note(QStringLiteral("Mounted here. Unmount it to pass it."));
        } else if (!device.accessible) {
            row->set_note(QStringLiteral("Needs access: Allow asks for your password once for this "
                                         "kind of device."));
        } else if (device.is_hid) {
            row->set_note(QStringLiteral("An input device. Passing it leaves this desktop without "
                                         "it until you return it."),
                          Theme::Tone::kWarm);
        }
    }

    auto* always_cell = new QWidget;
    auto* always_row = new QHBoxLayout(always_cell);
    always_row->setContentsMargins(0, 0, 0, 0);
    always_row->setSpacing(7);
    always_row->addWidget(Widgets::caption(QStringLiteral("Always")));
    auto* always = new ui::ToggleSwitch;
    always->setChecked(device.always);
    always->setProperty(kFocusKey, QStringLiteral("always"));
    always->setProperty(kDeviceKey, device.key());
    always->setToolTip(QStringLiteral("Pass it to the guest whenever it is plugged in and the "
                                      "guest is running"));
    connect(always, &QAbstractButton::clicked, this, [this, device](bool on) {
        if (on && device.is_hid && !confirm_input_device(device)) {
            QMetaObject::invokeMethod(this, &UsbPage::rebuild, Qt::QueuedConnection);
            return;
        }
        QMetaObject::invokeMethod(
            this, [this, identity = device.identity, on] { usb_.set_always(identity, on); },
            Qt::QueuedConnection);
    });
    always_row->addWidget(always);
    row->trailing()->addWidget(always_cell);

    auto* action_cell = new QWidget;
    action_cell->setFixedWidth(kActionWidth);
    auto* action_row = new QHBoxLayout(action_cell);
    action_row->setContentsMargins(0, 0, 0, 0);
    action_row->addStretch(1);
    action_row->addWidget(action_for(device));
    row->trailing()->addWidget(action_cell);
    list->add(row);
}

void UsbPage::rebuild() {
    const UsbStatus& snapshot = status_.usb();

    QString focused_key;
    QString focused_part;
    if (QWidget* focus = QApplication::focusWidget(); focus != nullptr && isAncestorOf(focus)) {
        focused_part = focus->property(kFocusKey).toString();
        focused_key = focus->property(kDeviceKey).toString();
    }

    for (Section* section : {&guest_, &host_, &remembered_}) {
        section->list->clear();
    }
    off_strip_->setVisible(snapshot.read && !snapshot.guest_running);

    if (!snapshot.read) {
        add_plain(host_.list, QStringLiteral("Reading the USB bus"));
        guest_.frame->setVisible(false);
        host_.frame->setVisible(true);
        remembered_.frame->setVisible(false);
        return;
    }
    for (const auto& device : snapshot.devices) {
        if (device.place == UsbStatus::Place::Guest) {
            add_device(guest_.list, device);
        } else if (device.place == UsbStatus::Place::Host && device.present) {
            add_device(host_.list, device);
        } else if (device.place == UsbStatus::Place::Absent) {
            add_device(remembered_.list, device);
        }
    }
    if (guest_.list->count() == 0 && host_.list->count() == 0 && remembered_.list->count() == 0) {
        add_plain(host_.list, QStringLiteral("No USB devices found"));
    }
    guest_.frame->setVisible(guest_.list->count() > 0);
    host_.frame->setVisible(host_.list->count() > 0);
    remembered_.frame->setVisible(remembered_.list->count() > 0);

    if (!focused_key.isEmpty()) {
        const auto widgets = findChildren<QWidget*>();
        for (QWidget* widget : widgets) {
            if (widget->property(kDeviceKey).toString() == focused_key &&
                widget->property(kFocusKey).toString() == focused_part && widget->isEnabled() && widget->isVisibleTo(this)) {
                widget->setFocus();
                break;
            }
        }
    }
}

}
