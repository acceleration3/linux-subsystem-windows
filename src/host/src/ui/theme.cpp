#include "lsw/host/ui/theme.hpp"

#include "lsw/host/ui/lsw_style.hpp"
#include "lsw/host/ui/sort_item.hpp"
#include "lsw/host/ui/table_header.hpp"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QString>
#include <QStyleFactory>

#include <QColor>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QWidget>

#include "theme_qss.hpp"

void init_lsw_theme_resources() {
    Q_INIT_RESOURCE(resources);
}

namespace lsw::host::ui {

namespace {
[[nodiscard]] QString pick(const QStringList& candidates, const QString& fallback) {
    const auto families = QFontDatabase::families();
    for (const auto& name : candidates) {
        if (families.contains(name, Qt::CaseInsensitive)) {
            return name;
        }
    }
    return fallback;
}

class CellWidgetDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr int kInset = 12;

    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option,
                              const QModelIndex& ) const override {
        editor->setGeometry(option.rect.adjusted(kInset, 0, -kInset, -1));
    }
};

}

void Theme::surface(QWidget* widget, const char* role) {
    widget->setObjectName(QString::fromLatin1(role));
    widget->setAttribute(Qt::WA_StyledBackground, true);
}

void Theme::rows_table(QTableWidget* table, const QStringList& headers, const QString& key,
                       const QVector<int>& widths, const QVector<int>& unsortable) {
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
    QFont font = head->font();
    font.setFamilies(mono_font(10).families());
    font.setPixelSize(10);
    font.setWeight(QFont::Medium);
    font.setLetterSpacing(QFont::PercentageSpacing, 113);
    head->setFont(font);
    TableHeader::attach(head, key, widths, unsortable);
}

QTableWidgetItem* Theme::kind_cell(const QString& text, int natural) {
    auto* item = new SortItem(text, std::nullopt, natural);
    item->setFont(mono_font(12));
    item->setForeground(color(Color::kFgFaint));
    return item;
}

void Theme::fit_rows(QTableWidget* table) {
    int height = table->horizontalHeader()->height();
    for (int row = 0; row < table->rowCount(); ++row) {
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
    const int room = table->viewport()->width();
    if (room > 0 && table->horizontalHeader()->length() > room) {
        height += table->horizontalScrollBar()->sizeHint().height();
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

}
