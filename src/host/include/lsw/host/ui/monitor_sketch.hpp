#pragma once

#include <QString>
#include <QWidget>

namespace lsw::host::ui {
class MonitorSketch final : public QWidget {
public:
    explicit MonitorSketch(QWidget* parent = nullptr);

    void show_mode(int width, int height, const QString& title, const QString& detail);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] int screen_height() const;

    int aspect_width_ = 16;
    int aspect_height_ = 9;
    QString title_;
    QString detail_;
};

}
