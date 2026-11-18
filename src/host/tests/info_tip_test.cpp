#include "lsw/host/ui/info_tip.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QApplication>
#include <QDir>
#include <QEnterEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QWidget>
#include <cstdio>

namespace {

constexpr int kTextPixels = 13;
constexpr int kNarrowBubble = 360;
constexpr int kWideBubble = 480;

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        ++failures;
    }
}

void settle() {
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

QString words_spanning(const QFont& font, int width) {
    const QFontMetrics metrics(font);
    QString text;
    while (metrics.horizontalAdvance(text) < width) text += QStringLiteral("memory window ");
    return text.trimmed();
}

QWidget* bubble() {
    for (QWidget* top : QApplication::topLevelWidgets()) {
        if (top->objectName() == QLatin1String("InfoBubble") && top->isVisible()) {
            return top;
        }
    }
    return nullptr;
}

QWidget* hover(lsw::host::ui::InfoTip& tip) {
    QEnterEvent enter(QPointF(4, 4), QPointF(4, 4), QPointF(4, 4));
    QApplication::sendEvent(&tip, &enter);
    settle();
    return bubble();
}

void leave(lsw::host::ui::InfoTip& tip) {
    QEvent left(QEvent::Leave);
    QApplication::sendEvent(&tip, &left);
    settle();
}

}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    lsw::host::ui::Theme::apply();
    const QString shots = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{};

    int windows_before = 0;
    for (QWidget* top : QApplication::topLevelWidgets()) windows_before += top->isVisible() ? 1 : 0;
    auto* loose = new lsw::host::ui::InfoTip(QStringLiteral("Made before it has a parent."));
    auto* emptied = new lsw::host::ui::InfoTip;
    emptied->set_text(QStringLiteral("Given text before it has a parent."));
    settle();
    int windows_after = 0;
    for (QWidget* top : QApplication::topLevelWidgets()) windows_after += top->isVisible() ? 1 : 0;
    check(!loose->isVisible() && !emptied->isVisible() && windows_after == windows_before,
          "an icon made without a parent never shows as a window of its own");

    QWidget window;
    auto* row = new QHBoxLayout(&window);
    row->addWidget(loose);
    row->addWidget(emptied);
    QFont text_font = window.font();
    text_font.setPixelSize(kTextPixels);
    auto* long_tip = new lsw::host::ui::InfoTip(words_spanning(text_font, 3 * kNarrowBubble));
    auto* short_tip = new lsw::host::ui::InfoTip(QStringLiteral("Set on the Display page."));
    auto* empty_tip = new lsw::host::ui::InfoTip;
    QString paragraphs;
    for (int i = 0; i < 4; ++i) {
        paragraphs += QStringLiteral("<b>Part %1.</b> ").arg(i + 1) + words_spanning(text_font, 4 * kWideBubble) +
                      QStringLiteral("<br><br>");
    }
    auto* paragraphs_tip = new lsw::host::ui::InfoTip(paragraphs);
    row->addWidget(paragraphs_tip);
    row->addWidget(long_tip);
    row->addWidget(short_tip);
    row->addWidget(empty_tip);
    window.resize(600, 80);
    window.show();
    settle();

    check(!empty_tip->isVisibleTo(&window), "a tip without text is not shown");
    check(loose->isVisibleTo(&window) && emptied->isVisibleTo(&window),
          "an icon made before its parent shows once it is in a window");

    const int line = QFontMetrics(text_font).lineSpacing();
    QWidget* shown = hover(*long_tip);
    check(shown != nullptr, "hovering the icon opens the bubble");
    int long_width = 0;
    if (shown != nullptr) {
        long_width = shown->width();
        const int lines = (shown->height() - 16 + line / 2) / line;
        check(shown->width() <= kNarrowBubble + 40, "a long explanation wraps instead of running wide");
        check(lines >= 2 && lines <= 5, "a long explanation takes a few lines");
        if (!shots.isEmpty()) {
            shown->grab().save(QDir(shots).filePath(QStringLiteral("info-tip-long.png")));
        }
    }
    leave(*long_tip);
    check(bubble() == nullptr, "leaving the icon closes the bubble");

    shown = hover(*short_tip);
    check(shown != nullptr && shown->height() < 2 * line + 16 && shown->width() < long_width,
          "a short explanation stays on one line, in a narrower bubble");
    if (shown != nullptr && !shots.isEmpty()) {
        shown->grab().save(QDir(shots).filePath(QStringLiteral("info-tip-short.png")));
    }
    leave(*short_tip);

    shown = hover(*paragraphs_tip);
    check(shown != nullptr && shown->width() > long_width + 60 && shown->width() <= kWideBubble + 40,
          "an explanation of several paragraphs gets a wider bubble, not a tall column");
    if (shown != nullptr && !shots.isEmpty()) {
        shown->grab().save(QDir(shots).filePath(QStringLiteral("info-tip-paragraphs.png")));
    }
    leave(*paragraphs_tip);

    if (failures == 0) {
        std::printf("ok\n");
    }
    return failures == 0 ? 0 : 1;
}
