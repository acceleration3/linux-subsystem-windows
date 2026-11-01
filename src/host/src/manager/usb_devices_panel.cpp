#include "lsw/host/manager/usb_devices_panel.hpp"

#include "lsw/host/services/status_hub.hpp"
#include "lsw/host/services/usb_passthrough.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/toggle_switch.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace lsw::host::manager {

namespace {

using services::UsbStatus;
using ui::Theme;
using ui::Widgets;

constexpr int kColDevice = 0;
constexpr int kColWhere = 1;
constexpr int kColAlways = 2;
constexpr int kColAction = 3;

// What a cell widget is, so focus can be put back on the same one after a
// rebuild.
constexpr const char* kFocusKey = "usbFocus";

// Centred at its own height, as the adapter table does its combos.
QWidget* held(QWidget* inner) {
    auto* holder = new QWidget;
    auto* line = new QHBoxLayout(holder);
    line->setContentsMargins(0, 0, 0, 0);
    line->addWidget(inner, 0, Qt::AlignVCenter);
    return holder;
}

QPushButton* action_button(const QString& text, bool enabled) {
    auto* button = new QPushButton(text);
    button->setCursor(Qt::PointingHandCursor);
    button->setEnabled(enabled);
    button->setMinimumWidth(120);
    return button;
}

}  // namespace

UsbDevicesPanel::UsbDevicesPanel(services::StatusHub& status, services::UsbPassthrough& usb,
                                 QWidget* parent)
    : QWidget(parent), status_(status), usb_(usb) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* frame = new QFrame;
    Theme::surface(frame, Theme::Role::kPanel);
    outer->addWidget(frame);
    auto* column = new QVBoxLayout(frame);
    column->setContentsMargins(1, 1, 1, 1);
    column->setSpacing(0);

    table_ = new QTableWidget(0, 4);
    Theme::rows_table(table_, {QStringLiteral("DEVICE"), QStringLiteral("WHERE"),
                               QStringLiteral("ALWAYS"), QString{}});
    table_->verticalHeader()->setDefaultSectionSize(48);
    table_->setProperty("controls", true);
    auto* head = table_->horizontalHeader();
    head->setSectionResizeMode(kColDevice, QHeaderView::Stretch);
    // Fixed: a column of cell widgets is not sized by what is in it.
    for (const int col : {kColWhere, kColAlways, kColAction}) {
        head->setSectionResizeMode(col, QHeaderView::Fixed);
    }
    table_->setColumnWidth(kColWhere, 170);
    table_->setColumnWidth(kColAlways, 80);
    table_->setColumnWidth(kColAction, 150);
    column->addWidget(table_);

    auto* foot = new QWidget;
    Theme::surface(foot, Theme::Role::kListFoot);
    auto* foot_row = new QVBoxLayout(foot);
    foot_row->setContentsMargins(10, 8, 10, 8);
    foot_ = Widgets::meta(QString{}, true);
    foot_row->addWidget(foot_);
    column->addWidget(foot);

    connect(&status_, &services::StatusHub::changed, this,
            [this](services::StatusHub::Area area) {
                if (area == services::StatusHub::Area::Usb) {
                    rebuild();
                }
            });
    connect(&usb_, &services::UsbPassthrough::changed, this, &UsbDevicesPanel::rebuild);
    rebuild();
}

QString UsbDevicesPanel::where(const UsbStatus::Device& device) {
    switch (device.place) {
    case UsbStatus::Place::Absent:
        return QStringLiteral("Not plugged in");
    case UsbStatus::Place::Guest:
        return device.present ? QStringLiteral("In the guest")
                              : QStringLiteral("In the guest, unplugged");
    case UsbStatus::Place::Host:
        break;
    }
    if (device.mounted) {
        return QStringLiteral("Mounted here");
    }
    if (!device.accessible) {
        return QStringLiteral("Needs access");
    }
    return QStringLiteral("On this machine");
}

bool UsbDevicesPanel::confirm_input_device(const UsbStatus::Device& device) {
    return QMessageBox::question(
               this, QStringLiteral("Pass a keyboard or mouse"),
               QStringLiteral("%1 looks like a keyboard, a mouse or another input device. While "
                              "it is in the guest it stops working on this machine, until you "
                              "return it.\n\nPass it anyway?")
                   .arg(device.name)) == QMessageBox::Yes;
}

bool UsbDevicesPanel::confirm_access(const UsbStatus::Device& device) {
    // The rule is per kind of device and the helper's prompt asks once for it.
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

void UsbDevicesPanel::rebuild() {
    const UsbStatus& snapshot = status_.usb();

    // The same control focused again afterwards, and the table where it was.
    QString focused_key;
    QString focused_part;
    if (QWidget* focus = QApplication::focusWidget(); focus != nullptr && table_->isAncestorOf(focus)) {
        focused_part = focus->property(kFocusKey).toString();
        focused_key = focus->property("usbKey").toString();
    }
    const int scrolled = table_->verticalScrollBar()->value();

    if (!snapshot.read) {
        // Not read yet is not "nothing plugged in".
        table_->setRowCount(1);
        table_->setItem(0, kColDevice, new QTableWidgetItem(QStringLiteral("Reading the USB bus")));
        for (int col = kColWhere; col <= kColAction; ++col) {
            table_->setCellWidget(0, col, nullptr);
            table_->setItem(0, col, new QTableWidgetItem(QStringLiteral("-")));
        }
        foot_->setText(QStringLiteral("-"));
        Theme::fit_rows(table_);
        return;
    }
    const bool running = snapshot.guest_running;
    const int rows = static_cast<int>(snapshot.devices.size());
    table_->clearContents();
    table_->setRowCount(rows == 0 ? 1 : rows);
    if (rows == 0) {
        table_->setItem(0, kColDevice, new QTableWidgetItem(QStringLiteral("No USB devices found")));
        Theme::fit_rows(table_);
    }
    for (int row = 0; row < rows; ++row) {
        const UsbStatus::Device& device = snapshot.devices.at(row);
        const QString identity = device.identity;
        const int bus = device.bus;
        const int number = device.device;

        auto* name = new QLabel(QStringLiteral("%1<br><span style='font-family:monospace;"
                                               "color:%2;font-size:11px'>%3</span>")
                                    .arg(device.name.toHtmlEscaped(),
                                         QString::fromLatin1(Theme::Color::kFgFaint),
                                         identity.toHtmlEscaped()));
        name->setTextFormat(Qt::RichText);
        table_->setCellWidget(row, kColDevice, held(name));

        auto* place = new QWidget;
        auto* place_column = new QVBoxLayout(place);
        place_column->setContentsMargins(0, 0, 0, 0);
        place_column->setSpacing(1);
        place_column->addWidget(new QLabel(where(device)));
        const QString error = usb_.error_for(identity, bus, number);
        if (!error.isEmpty()) {
            auto* problem = Widgets::meta(error, false);
            problem->setProperty(Theme::Tone::kProperty, Theme::Tone::kBad);
            problem->setToolTip(error);
            problem->setMaximumWidth(150);
            // Full text in the tooltip; the row keeps its height.
            problem->setText(problem->fontMetrics().elidedText(error, Qt::ElideRight, 150));
            place_column->addWidget(problem);
        }
        table_->setCellWidget(row, kColWhere, held(place));

        auto* always = new ui::ToggleSwitch;
        always->setCheckable(true);
        always->setChecked(device.always);
        always->setProperty(kFocusKey, QStringLiteral("always"));
        always->setProperty("usbKey", device.key());
        // "Pass it to the guest whenever it is plugged in and the guest is running."
        always->setToolTip(QStringLiteral("Pass it to the guest whenever it is plugged in and "
                                          "the guest is running"));
        connect(always, &QAbstractButton::clicked, this,
                [this, identity, device](bool on) {
                    // Queued: the setting's change rebuilds the table this
                    // switch is in.
                    if (on && device.is_hid && !confirm_input_device(device)) {
                        QMetaObject::invokeMethod(this, &UsbDevicesPanel::rebuild, Qt::QueuedConnection);
                        return;
                    }
                    QMetaObject::invokeMethod(this, [this, identity, on] { usb_.set_always(identity, on); },
                                              Qt::QueuedConnection);
                });
        table_->setCellWidget(row, kColAlways, held(always));

        QPushButton* button = nullptr;
        if (device.place == UsbStatus::Place::Absent) {
            button = action_button(QStringLiteral("✕"), true);
            button->setObjectName(QString::fromLatin1(Theme::Role::kQuiet));
            button->setToolTip(QStringLiteral("Forget it"));
            connect(button, &QPushButton::clicked, this, [this, identity] {
                QMetaObject::invokeMethod(this, [this, identity] { usb_.set_always(identity, false); },
                                          Qt::QueuedConnection);
            });
        } else if (device.attached()) {
            button = action_button(QStringLiteral("Return"), true);
            connect(button, &QPushButton::clicked, this, [this, identity, bus, number] {
                QMetaObject::invokeMethod(this, [this, identity, bus, number] { usb_.detach(identity, bus, number); },
                                          Qt::QueuedConnection);
            });
        } else if (!device.accessible && !device.mounted) {
            const bool waiting = usb_.granting(identity, bus, number);
            button = action_button(waiting ? QStringLiteral("Waiting") : QStringLiteral("Allow"), !waiting);
            button->setToolTip(QStringLiteral("Asks for your password once for this kind of device"));
            connect(button, &QPushButton::clicked, this, [this, identity, bus, number, device] {
                if (!confirm_access(device)) {
                    return;
                }
                QMetaObject::invokeMethod(this, [this, identity, bus, number] { usb_.grant_access(identity, bus, number); },
                                          Qt::QueuedConnection);
            });
        } else {
            button = action_button(QStringLiteral("Pass to guest"), running && !device.mounted);
            if (device.mounted) {
                button->setToolTip(QStringLiteral("Something on it is mounted on this machine"));
            }
            connect(button, &QPushButton::clicked, this, [this, identity, bus, number, device] {
                if (device.is_hid && !confirm_input_device(device)) {
                    return;
                }
                QMetaObject::invokeMethod(this, [this, identity, bus, number] { usb_.attach(identity, bus, number); },
                                          Qt::QueuedConnection);
            });
        }
        button->setProperty(kFocusKey, QStringLiteral("action"));
        button->setProperty("usbKey", device.key());
        table_->setCellWidget(row, kColAction, held(button));

        if (device.key() == focused_key) {
            QWidget* target = focused_part == QLatin1String("always") ? static_cast<QWidget*>(always)
                                                                      : static_cast<QWidget*>(button);
            if (target->isEnabled()) {
                target->setFocus();
            }
        }
    }
    if (rows != 0) {
        Theme::fit_rows(table_);
    }
    table_->verticalScrollBar()->setValue(scrolled);
    foot_->setText(running ? QStringLiteral("Always: pass it to the guest whenever it is plugged in "
                                            "and the guest is running. Allow asks for your password "
                                            "once per device type.")
                           : QStringLiteral("Start the guest to pass a device."));
}

}  // namespace lsw::host::manager
