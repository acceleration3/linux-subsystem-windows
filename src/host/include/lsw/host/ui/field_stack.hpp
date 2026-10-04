#pragma once

#include <QString>

class QVBoxLayout;
class QWidget;

namespace lsw::host::ui {

// Fields as a stack: each one's name above it, and the control the full
// width of the column. A two-column form would put a short caption
// beside a long path and waste the width the path needs.
class FieldStack {
public:
    explicit FieldStack(QVBoxLayout* into) : into_(into) {}

    // The caption over ``control``, appended to the stack.
    void add(const QString& name, QWidget* control);

    // A wrapping mono note under the field before it.
    void add_note(const QString& text);

    // One field on its own, for a layout that places it itself: a grid
    // of fields two by two.
    [[nodiscard]] static QVBoxLayout* column(const QString& name, QWidget* control);

private:
    QVBoxLayout* into_;
};

}  // namespace lsw::host::ui
