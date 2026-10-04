#pragma once

#include <QString>
#include <QVariant>

class QFrame;
class QLabel;
class QPushButton;
class QWidget;

namespace lsw::host::ui {

// The handful of small widgets every page builds the same way, so a
// caption on one page cannot drift from a caption on another.
//
// Each one is a stock Qt widget with a Theme::Role object name. The
// look stays in the stylesheet; this only spells the construction once.
class Widgets {
public:
    Widgets() = delete;

    // An uppercase mono caption: what a group-box title would be.
    [[nodiscard]] static QLabel* caption(const QString& text);

    // Small mono text, for what the machine said. ``wrap`` for anything
    // that can run longer than its column.
    [[nodiscard]] static QLabel* meta(const QString& text, bool wrap = false);

    // A sentence in the interface face, dimmed and wrapping: prose under
    // a setting's name, a footnote, an explanation.
    [[nodiscard]] static QLabel* prose(const QString& text);

    // A one-pixel rule between two sections.
    [[nodiscard]] static QFrame* separator();

    // A button wearing ``role``, one of Theme::Role; the plain button
    // when ``role`` is null.
    [[nodiscard]] static QPushButton* button(const QString& text, const char* role = nullptr);

    // Give a widget that is already on screen a different role, or a
    // different value for a property the stylesheet selects on. Qt does
    // not re-evaluate a stylesheet when either changes, so both repolish.
    static void set_role(QWidget* widget, const char* role);
    static void set_flag(QWidget* widget, const char* property, const QVariant& value);

    // Show a window and put it in front of the others, with focus.
    static void bring_to_front(QWidget* window);
};

}  // namespace lsw::host::ui
