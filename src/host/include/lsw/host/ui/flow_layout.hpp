#pragma once

#include <QLayout>
#include <QList>
#include <QRect>

namespace lsw::host::ui {
class FlowLayout : public QLayout {
public:
    enum class Mode { Wrap, Even };

    explicit FlowLayout(QWidget* parent = nullptr, Mode mode = Mode::Wrap, int horizontal = 0,
                        int vertical = 0);
    ~FlowLayout() override;

    void set_cell_width(int width);

    void addItem(QLayoutItem* item) override;
    [[nodiscard]] int count() const override;
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    [[nodiscard]] Qt::Orientations expandingDirections() const override;
    [[nodiscard]] bool hasHeightForWidth() const override;
    [[nodiscard]] int heightForWidth(int width) const override;
    void setGeometry(const QRect& rect) override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSize() const override;

private:
    [[nodiscard]] int arrange(const QRect& rect, bool apply) const;
    [[nodiscard]] int arrange_wrapped(const QRect& area, bool apply) const;
    [[nodiscard]] int arrange_even(const QRect& area, bool apply) const;

    QList<QLayoutItem*> items_;
    Mode mode_;
    int horizontal_;
    int vertical_;
    int cell_width_ = 0;
};

}
