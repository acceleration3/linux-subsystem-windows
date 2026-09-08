#include "lsw/host/ui/theme.hpp"

#include "lsw/host/ui/lsw_style.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QString>
#include <QStyleFactory>

#include <QColor>

#include "theme_qss.hpp"

// Outside the namespace: Q_INIT_RESOURCE expands to a call on a symbol
// Qt declares at global scope.
void init_lsw_theme_resources() {
    Q_INIT_RESOURCE(resources);
}

namespace lsw::host::ui {

namespace {

// The design pairs Archivo with JetBrains Mono. Neither is guaranteed
// on a Linux host, and a missing family falls back silently to whatever
// Qt picks, which is how a considered type scale turns into DejaVu at
// the wrong weight. So the choice is made here, out loud, against what
// is actually installed.
[[nodiscard]] QString pick(const QStringList& candidates, const QString& fallback) {
    const auto families = QFontDatabase::families();
    for (const auto& name : candidates) {
        if (families.contains(name, Qt::CaseInsensitive)) {
            return name;
        }
    }
    return fallback;
}

}  // namespace

QString Theme::stylesheet() {
    return QString::fromUtf8(::lsw::embedded::kThemeQss);
}

void Theme::apply() {
    // The images live in a static library, whose resource initialiser
    // is only pulled in when something references it. Nothing would, so
    // this does. The symbol Qt generates is at global scope, which is
    // why the call is qualified.
    ::init_lsw_theme_resources();

    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (app == nullptr) {
        return;
    }

    const QString ui_family =
        pick({QStringLiteral("Archivo"), QStringLiteral("Inter"),
              QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")},
             app->font().family());

    QFont base{ui_family};
    base.setPixelSize(13);
    app->setFont(base);

    // Fusion under a proxy that paints the three indicators a
    // stylesheet cannot, and a palette to go with it.
    //
    // A stylesheet does not reach everything: QWizard paints its own
    // title band, item views draw their own alternating rows, and the
    // platform style may ignore a colour it thinks it knows better.
    // Those all read the palette, so the palette has to say the same
    // thing the stylesheet does or half the window is light.
    app->setStyle(new LswStyle);

    const QColor ink{QString::fromLatin1(Color::kInk)};
    const QColor surface{QString::fromLatin1(Color::kSurface)};
    const QColor raised{QString::fromLatin1(Color::kRaised)};
    const QColor text{QString::fromLatin1(Color::kFg)};
    const QColor dim{QString::fromLatin1(Color::kFgDim)};
    const QColor faint{QString::fromLatin1(Color::kFgFaint)};
    const QColor amber{QString::fromLatin1(Color::kAmber)};

    QPalette palette;
    palette.setColor(QPalette::Window, ink);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, QColor{QStringLiteral("#0f131a")});
    palette.setColor(QPalette::AlternateBase, surface);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::PlaceholderText, faint);
    palette.setColor(QPalette::Button, raised);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::Highlight, amber);
    palette.setColor(QPalette::HighlightedText, ink);
    palette.setColor(QPalette::ToolTipBase, raised);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Link, amber);
    palette.setColor(QPalette::Mid, surface);
    palette.setColor(QPalette::Dark, ink);
    palette.setColor(QPalette::Shadow, Qt::black);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, faint);
    palette.setColor(QPalette::Disabled, QPalette::Text, faint);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, faint);
    app->setPalette(palette);

    app->setStyleSheet(stylesheet());
}

}  // namespace lsw::host::ui
