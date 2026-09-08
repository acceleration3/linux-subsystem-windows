#include "lsw/host/ui/theme.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QString>

#include "theme_qss.hpp"

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

    app->setStyleSheet(stylesheet());
}

}  // namespace lsw::host::ui
