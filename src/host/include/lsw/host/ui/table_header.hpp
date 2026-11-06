#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class QHeaderView;

namespace lsw::host::ui {
class TableHeader : public QObject {
    Q_OBJECT
public:
    static constexpr int kMinWidth = 48;

    static TableHeader* attach(QHeaderView* header, const QString& key, const QVector<int>& widths,
                               const QVector<int>& unsortable = {});

    static void refresh(QHeaderView* header);

    [[nodiscard]] static bool natural(const QHeaderView* header);
    [[nodiscard]] static Qt::SortOrder order(const QHeaderView* header);

    ~TableHeader() override;

private:
    TableHeader(QHeaderView* header, const QString& key, const QVector<int>& widths,
                const QVector<int>& unsortable);

    void resized(int column, int now);
    void clicked(int column);
    void apply();
    void flush();
    void refit() const;

    QHeaderView* header_;
    QString key_;
    QVector<int> widths_;
    QVector<int> unsortable_;
    int column_ = -1;
    Qt::SortOrder order_ = Qt::AscendingOrder;
    bool dirty_widths_ = false;
    bool dirty_sort_ = false;
    bool refitting_ = false;
    QTimer save_;
};

}
