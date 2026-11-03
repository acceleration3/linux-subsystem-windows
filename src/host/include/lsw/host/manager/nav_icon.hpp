#pragma once

#include "lsw/host/ui/line_icon.hpp"

#include <QWidget>

namespace lsw::host::manager {
class NavIcon final : public QWidget {
public:
    explicit NavIcon(ui::LineIcon::Shape shape, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] QColor tone() const;

    ui::LineIcon::Shape shape_;
};

}
