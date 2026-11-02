#pragma once

#include <QString>
#include <QVariant>

class QFrame;
class QLabel;
class QPushButton;
class QWidget;

namespace lsw::host::ui {
class Widgets {
public:
    Widgets() = delete;

    [[nodiscard]] static QLabel* caption(const QString& text);

    [[nodiscard]] static QLabel* meta(const QString& text, bool wrap = false);

    [[nodiscard]] static QLabel* prose(const QString& text);

    [[nodiscard]] static QLabel* step_line(const QString& mark, const QString& text,
                                           const char* tone);

    [[nodiscard]] static QFrame* separator();

    [[nodiscard]] static QPushButton* button(const QString& text, const char* role = nullptr);

    static void set_role(QWidget* widget, const char* role);
    static void set_flag(QWidget* widget, const char* property, const QVariant& value);

    static void bring_to_front(QWidget* window);
};

}
