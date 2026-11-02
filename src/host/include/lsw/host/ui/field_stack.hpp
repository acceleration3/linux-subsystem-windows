#pragma once

#include <QString>

class QVBoxLayout;
class QWidget;

namespace lsw::host::ui {
class FieldStack {
public:
    explicit FieldStack(QVBoxLayout* into) : into_(into) {}

    void add(const QString& name, QWidget* control);

    void add_note(const QString& text);

    [[nodiscard]] static QVBoxLayout* column(const QString& name, QWidget* control);

private:
    QVBoxLayout* into_;
};

}
