#pragma once

#include "lsw/host/ui/line_icon.hpp"

#include <QWidget>

namespace lsw::host::ui {
class IconTile final : public QWidget {
public:
    explicit IconTile(LineIcon::Shape shape, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    LineIcon::Shape shape_;
};

}
