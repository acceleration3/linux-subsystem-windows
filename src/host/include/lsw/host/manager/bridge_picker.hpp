#pragma once

#include "lsw/host/platform/host_addresses.hpp"
#include "lsw/host/platform/host_bridges.hpp"

#include <QHash>
#include <QWidget>

class QButtonGroup;
class QFrame;
class QLabel;
class QRadioButton;
class QVBoxLayout;

namespace lsw::host::ui {
class ItemList;
}

namespace lsw::host::manager {
class BridgePicker : public QWidget {
    Q_OBJECT
public:
    explicit BridgePicker(QWidget* parent = nullptr);

    void show_state(const platform::HostBridges::Facts& facts,
                    const QVector<platform::HostAddresses::Interface>& interfaces,
                    const QString& picked);

    [[nodiscard]] static QString facts_line(const platform::HostBridges::Bridge& bridge,
                                            const QStringList& addresses);

Q_SIGNALS:
    void chosen(const QString& bridge);

private:
    void rebuild_rows(const platform::HostBridges::Facts& facts,
                      const QVector<platform::HostAddresses::Interface>& interfaces,
                      const QString& picked);
    void rebuild_checks(const platform::HostBridges::Facts& facts, const QString& picked);

    QLabel* none_ = nullptr;
    ui::ItemList* list_ = nullptr;
    QFrame* rule_ = nullptr;
    QWidget* checks_ = nullptr;
    QVBoxLayout* checks_box_ = nullptr;
    QButtonGroup* group_ = nullptr;
    QHash<QString, QRadioButton*> radios_;
    QStringList rows_signature_;
    QStringList checks_signature_;
};

}  // namespace lsw::host::manager
