#include "lsw/host/ui/theme.hpp"

#include "lsw/host/ui/lsw_style.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QString>
#include <QStyleFactory>

#include <QColor>
#include <QHeaderView>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QWidget>

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

// The row lists' cells inset their text by the stylesheet's item padding
// (12 px either side). A control or a pill set into a cell is placed by
// the delegate too, and the stock one hands it the *text* rectangle: the
// item's padding taken off every side, vertical included, so a 24 px
// button in a 38 px row with 9 px of padding above and below was cut to
// 20. This one gives a cell widget the whole height of its row (less the
// hairline under it) and the same 12 px either side the text has, so
// controls line up with the column's text and are never clipped.
class CellWidgetDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr int kInset = 12;

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& /*index*/) const override {
        editor->setGeometry(option.rect.adjusted(kInset, 0, -kInset, -1));
    }
};

}  // namespace

void Theme::surface(QWidget* widget, const char* role) {
    widget->setObjectName(QString::fromLatin1(role));
    widget->setAttribute(Qt::WA_StyledBackground, true);
}

// One table, everywhere. The design draws a row list rather than a
// grid: headings in letter-spaced mono over a hairline, rows separated
// by a fainter one, and no box around any of it, because the panel
// holding the table is already the box.
void Theme::rows_table(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->setObjectName(QStringLiteral("Rows"));
    table->setAttribute(Qt::WA_StyledBackground, true);
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(38);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setWordWrap(false);
    table->setTextElideMode(Qt::ElideRight);
    table->setItemDelegate(new CellWidgetDelegate(table));

    auto* head = table->horizontalHeader();
    head->setDefaultAlignment(Qt::AlignLeft | Qt::AlignBottom);
    head->setHighlightSections(false);
    head->setFixedHeight(28);
    // Letter spacing is the one thing the heading needs and a
    // stylesheet cannot say.
    QFont font = head->font();
    font.setFamilies(mono_font(10).families());
    font.setPixelSize(10);
    font.setWeight(QFont::Medium);
    font.setLetterSpacing(QFont::PercentageSpacing, 113);
    head->setFont(font);
}

QTableWidgetItem* Theme::kind_cell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFont(mono_font(12));
    item->setForeground(color(Color::kFgFaint));
    return item;
}

void Theme::fit_rows(QTableWidget* table) {
    int height = table->horizontalHeader()->height();
    for (int row = 0; row < table->rowCount(); ++row) {
        // Tall enough for every control in the row at its own height,
        // with a little air: a row sized for text alone clips a combo
        // or a button set into it.
        int needed = table->rowHeight(row);
        for (int column = 0; column < table->columnCount(); ++column) {
            if (QWidget* cell = table->cellWidget(row, column)) {
                cell->ensurePolished();
                needed = qMax(needed, cell->sizeHint().height() + 8);
            }
        }
        if (needed != table->rowHeight(row)) {
            table->setRowHeight(row, needed);
        }
        height += needed;
    }
    table->setFixedHeight(height + 2);
}

int Theme::cell_inset() {
    return CellWidgetDelegate::kInset;
}

QFont Theme::mono_font(int pixel_size) {
    QFont font;
    font.setFamilies({QStringLiteral("JetBrains Mono"), QStringLiteral("DejaVu Sans Mono")});
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(pixel_size);
    return font;
}

QColor Theme::color(const char* hex) {
    return QColor{QString::fromLatin1(hex)};
}

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

    const QColor ink = color(Color::kInk);
    const QColor surface = color(Color::kSurface);
    const QColor raised = color(Color::kRaised);
    const QColor text = color(Color::kFg);
    const QColor faint = color(Color::kFgFaint);
    const QColor amber = color(Color::kAmber);

    QPalette palette;
    palette.setColor(QPalette::Window, ink);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, color(Color::kBase));
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
