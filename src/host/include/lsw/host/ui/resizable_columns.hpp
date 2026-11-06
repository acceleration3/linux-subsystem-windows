#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class QHeaderView;

namespace lsw::host::ui {
class ResizableColumns : public QObject {
    Q_OBJECT
public:
    static constexpr int kMinWidth = 48;

    static void attach(QHeaderView* header, const QString& key, const QVector<int>& widths,
                       int stretch = -1);

    ~ResizableColumns() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ResizableColumns(QHeaderView* header, const QString& key, const QVector<int>& widths, int stretch);

    void fill();
    void resized(int column, int now);
    void flush();
    void refit() const;
    [[nodiscard]] QVector<int> stored() const;

    QHeaderView* header_;
    QString key_;
    QVector<int> widths_;
    int stretch_;
    bool applying_ = false;
    bool dirty_ = false;
    QTimer save_;
};

}
