#pragma once

#include <QVector>
#include <QWidget>
#include <optional>

namespace lsw::host::ui {
class UsageGraph final : public QWidget {
public:
    struct Point {
        qint64 at_ms = 0;
        quint64 lower = 0;
        quint64 upper = 0;
    };

    explicit UsageGraph(QWidget* parent = nullptr);

    void set_series(const QVector<Point>& points, std::optional<quint64> ceiling, qint64 now_ms,
                    qint64 span_ms);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<Point> points_;
    std::optional<quint64> ceiling_;
    qint64 now_ms_ = 0;
    qint64 span_ms_ = 1;
};

}
