#pragma once

#include <QString>
#include <QTableWidgetItem>
#include <optional>

class QTableWidget;

namespace lsw::host::ui {
class SortItem : public QTableWidgetItem {
public:
    explicit SortItem(const QString& text, std::optional<double> value = std::nullopt, int natural = 0,
                      const QString& key = {});

    static SortItem* put(QTableWidget* table, int row, int column, const QString& text,
                         std::optional<double> value = std::nullopt, const QString& key = {});

    bool operator<(const QTableWidgetItem& other) const override;

private:
    std::optional<double> value_;
    QString key_;
    int natural_;
};

}
