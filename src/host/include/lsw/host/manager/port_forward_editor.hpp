#pragma once

#include "lsw/host/config/port_forwards.hpp"
#include "lsw/host/manager/network_state.hpp"
#include "lsw/host/platform/host_addresses.hpp"

#include <QHash>
#include <QWidget>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace lsw::host::config {
class ManagerSettings;
class PendingChanges;
}
namespace lsw::host::ui {
class FlowLayout;
class SegmentedControl;
}

namespace lsw::host::manager {

class PortForwardEditor : public QWidget {
    Q_OBJECT
public:
    PortForwardEditor(config::ManagerSettings& settings, config::PendingChanges& pending,
                      QWidget* parent = nullptr);

    [[nodiscard]] QVector<config::PortForward> entries() const;
    [[nodiscard]] NetworkState::ForwardState state_of(const config::PortForward& entry) const;

    QString add(const config::PortForward& entry);
    QString apply(const config::PortPreset& preset);
    void remove(const config::PortForward& entry);
    void remove_preset(const config::PortPreset& preset);
    void set_reach(const config::PortForward& entry, bool from_network);
    void undo();
    [[nodiscard]] bool can_undo() const noexcept { return undo_.has_value(); }

    void set_live(const NetworkState::Live& live);
    void set_host_addresses(const QVector<platform::HostAddresses::Address>& addresses);
    void check_ports();

Q_SIGNALS:
    void changed();

private:
    enum class Under { Host, Guest };

    void build_presets(QVBoxLayout* column);
    void build_table(QVBoxLayout* column);
    void build_notes(QVBoxLayout* column);
    [[nodiscard]] QWidget* row_for(const config::PortForward& entry, NetworkState::ForwardState state);
    void add_typed();
    void rebuild();
    void show_presets(const QVector<config::PortForward>& now);
    void show_notes(const QVector<config::PortForward>& now);
    void order_tabs();
    void stage(const QVector<config::PortForward>& entries);
    void say(const QString& refusal, Under under);
    [[nodiscard]] bool busy(const config::PortForward& entry) const;

    config::PendingChanges& pending_;
    NetworkState::Live live_;
    QVector<platform::HostAddresses::Address> addresses_;
    mutable QHash<QString, bool> busy_;
    std::optional<QVector<config::PortForward>> undo_;
    QString undo_text_;
    QVector<config::PortForward> shown_;
    QStringList rows_signature_;
    QStringList reach_signature_;
    bool staging_ = false;
    int offered_ = -1;
    int reach_width_ = 0;
    Under said_under_ = Under::Host;

    QVector<QPushButton*> chips_;
    QWidget* offer_ = nullptr;
    QLabel* offer_text_ = nullptr;
    QPushButton* offer_remove_ = nullptr;
    QPushButton* offer_keep_ = nullptr;
    QLabel* preset_refusal_ = nullptr;

    QWidget* rows_ = nullptr;
    QVBoxLayout* rows_box_ = nullptr;
    QLabel* empty_ = nullptr;
    QComboBox* protocol_ = nullptr;
    QLineEdit* host_port_ = nullptr;
    QLineEdit* guest_port_ = nullptr;
    ui::SegmentedControl* reach_ = nullptr;
    QPushButton* add_button_ = nullptr;
    QWidget* refusal_row_ = nullptr;
    QWidget* refusal_pad_ = nullptr;
    QLabel* refusal_ = nullptr;

    QWidget* notes_ = nullptr;
    QWidget* undo_line_ = nullptr;
    QLabel* undo_label_ = nullptr;
    QPushButton* undo_button_ = nullptr;
    QWidget* reach_line_ = nullptr;
    ui::FlowLayout* reach_flow_ = nullptr;
};

}
