#pragma once

#include "lsw/host/config/port_forwards.hpp"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}

namespace lsw::host::manager {

class PortForwardEditor : public QWidget {
    Q_OBJECT
public:
    PortForwardEditor(config::ManagerSettings& settings, config::PendingChanges& pending,
                      QWidget* parent = nullptr);

    [[nodiscard]] QVector<config::PortForward> entries() const;

    QString add(const config::PortForward& entry);
    QString add_sunshine();
    void remove(const config::PortForward& entry);

private:
    void add_typed();
    void rebuild();
    void stage(const QVector<config::PortForward>& entries);
    void say(const QString& refusal);

    config::PendingChanges& pending_;
    QVBoxLayout* rows_ = nullptr;
    QLabel* empty_ = nullptr;
    QComboBox* protocol_ = nullptr;
    QLineEdit* host_port_ = nullptr;
    QLineEdit* guest_port_ = nullptr;
    QComboBox* reach_ = nullptr;
    QLabel* refusal_ = nullptr;
};

}
