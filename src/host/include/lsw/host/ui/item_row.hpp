#pragma once

#include "lsw/host/ui/line_icon.hpp"

#include <QWidget>

class QHBoxLayout;
class QLabel;

namespace lsw::host::ui {
class IconTile;

class ItemRow : public QWidget {
    Q_OBJECT
public:
    explicit ItemRow(LineIcon::Shape icon, QWidget* parent = nullptr);

    void set_name(const QString& name);
    void set_sub(const QString& sub);
    void set_note(const QString& note, const char* tone = nullptr);
    void set_dim(bool dim);
    void set_last(bool last);

    [[nodiscard]] QHBoxLayout* trailing() const noexcept { return trailing_; }

private:
    IconTile* tile_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* sub_ = nullptr;
    QLabel* note_ = nullptr;
    QHBoxLayout* trailing_ = nullptr;
};

}
