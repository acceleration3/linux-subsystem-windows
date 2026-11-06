#include "lsw/host/ui/sort_item.hpp"
#include "lsw/host/ui/table_header.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>

using lsw::host::ui::SortItem;
using lsw::host::ui::TableHeader;
using lsw::host::ui::Theme;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

QSettings store() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lsw"), QStringLiteral("manager"));
}

struct Row {
    const char* name;
    std::optional<double> value;
};

const Row kRows[] = {{"delta", 3}, {"Alpha", 10}, {"charlie", std::nullopt}, {"bravo", 2}};

void fill(QTableWidget& t) {
    for (int row = 0; row < 4; ++row) {
        SortItem::put(&t, row, 0, QString::fromLatin1(kRows[row].name));
        SortItem::put(&t, row, 1, kRows[row].value ? QString::number(*kRows[row].value) : QStringLiteral("-"),
                      kRows[row].value);
        SortItem::put(&t, row, 2, QStringLiteral("x"));
    }
}

std::unique_ptr<QTableWidget> table(const QString& key, int width, const QVector<int>& unsortable = {}) {
    auto out = std::make_unique<QTableWidget>(4, 4);
    Theme::rows_table(out.get(), {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")},
                      key, {100, 100, 100, 100}, unsortable);
    fill(*out);
    for (int row = 0; row < 4; ++row) {
        out->setCellWidget(row, 3, new QLabel(QString::fromLatin1(kRows[row].name)));
    }
    out->resize(width, 260);
    TableHeader::refresh(out->horizontalHeader());
    out->show();
    QApplication::processEvents();
    return out;
}

void mouse(QWidget* widget, QEvent::Type type, QPoint at, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void drag(QHeaderView* head, int column, int by) {
    QWidget* view = head->viewport();
    const int y = head->height() / 2;
    const int edge = head->sectionViewportPosition(column) + head->sectionSize(column) - 1;
    mouse(view, QEvent::MouseMove, {edge, y}, Qt::NoButton, Qt::NoButton);
    mouse(view, QEvent::MouseButtonPress, {edge, y}, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 5; ++step) {
        mouse(view, QEvent::MouseMove, {edge + by * step / 5, y}, Qt::NoButton, Qt::LeftButton);
    }
    mouse(view, QEvent::MouseButtonRelease, {edge + by, y}, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
}

void click(QHeaderView* head, int column) {
    QWidget* view = head->viewport();
    const QPoint at(head->sectionViewportPosition(column) + head->sectionSize(column) / 2, head->height() / 2);
    mouse(view, QEvent::MouseButtonPress, at, Qt::LeftButton, Qt::LeftButton);
    mouse(view, QEvent::MouseButtonRelease, at, Qt::LeftButton, Qt::NoButton);
    QApplication::processEvents();
}

QStringList column_text(const QTableWidget& t, int column) {
    QStringList out;
    for (int row = 0; row < t.rowCount(); ++row) {
        out << t.item(row, column)->text();
    }
    return out;
}

QStringList names(const QTableWidget& t) {
    return column_text(t, 0);
}

}

int main(int argc, char** argv) {
    QApplication app{argc, argv};
    QTemporaryDir dir;
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());

    {
        auto t = table(QStringLiteral("plain"), 800);
        auto* head = t->horizontalHeader();
        check(head->sectionSize(0) == 100 && head->sectionSize(1) == 100 && head->sectionSize(2) == 100,
              "columns start at their default widths");
        check(head->sectionSize(3) == t->viewport()->width() - 300, "the last column fills the rest");
        check(head->sectionResizeMode(0) == QHeaderView::Interactive && head->stretchLastSection(),
              "every column is interactive and the last stretches");
        drag(head, 2, 30);
        check(head->sectionSize(2) == 130 && head->sectionSize(0) == 100 && head->sectionSize(1) == 100,
              "dragging a right edge right widens exactly that column");
        check(head->sectionViewportPosition(2) == 200 && head->sectionSize(3) == t->viewport()->width() - 330,
              "its left edge stays put and the last column gives way");
        drag(head, 1, -20);
        check(head->sectionSize(1) == 80 && head->sectionSize(0) == 100 && head->sectionSize(2) == 130,
              "dragging a right edge left narrows exactly that column");
        drag(head, 0, 10);
        check(head->sectionSize(0) == 110 && head->sectionSize(1) == 80, "the first column resizes like any other");
        head->resizeSection(0, 5);
        check(head->sectionSize(0) == TableHeader::kMinWidth, "a column cannot shrink below the minimum");
        t->resize(1000, 260);
        QApplication::processEvents();
        check(head->length() == t->viewport()->width(), "growing the table grows only the last column");
        t->resize(300, 260);
        QApplication::processEvents();
        check(head->length() > t->viewport()->width(), "a narrow table scrolls instead of squeezing");
    }
    {
        auto first = table(QStringLiteral("kept"), 800);
        drag(first->horizontalHeader(), 2, 120);
        first->horizontalHeader()->resizeSection(3, 500);
        first.reset();
        const QString saved = store().value(QStringLiteral("columns/kept")).toString();
        check(saved.startsWith(QStringLiteral("100,100,220,")), "dragged widths are written when the table goes away");
        auto second = table(QStringLiteral("kept"), 800);
        check(second->horizontalHeader()->sectionSize(2) == 220, "a rebuilt table takes the dragged widths");
        check(second->horizontalHeader()->sectionSize(3) == second->viewport()->width() - 420,
              "the last column's width is never remembered");
    }
    {
        store().setValue(QStringLiteral("columns/stored"), QStringLiteral("70,80,90,100"));
        auto t = table(QStringLiteral("stored"), 800);
        check(t->horizontalHeader()->sectionSize(0) == 70 && t->horizontalHeader()->sectionSize(2) == 90,
              "widths saved by an earlier run are used");
    }
    {
        store().setValue(QStringLiteral("columns/broken"), QStringLiteral("70,abc,90,100"));
        store().setValue(QStringLiteral("columns/short"), QStringLiteral("70,80"));
        store().setValue(QStringLiteral("columns/tiny"), QStringLiteral("1,1,1,1"));
        auto broken = table(QStringLiteral("broken"), 800);
        auto shorter = table(QStringLiteral("short"), 800);
        auto tiny = table(QStringLiteral("tiny"), 800);
        check(broken->horizontalHeader()->sectionSize(0) == 100 && shorter->horizontalHeader()->sectionSize(0) == 100
                  && tiny->horizontalHeader()->sectionSize(0) == 100,
              "unreadable stored widths fall back to the defaults");
    }
    {
        auto t = table(QStringLiteral("sorting"), 800);
        auto* head = t->horizontalHeader();
        const QStringList natural = names(*t);
        check(head->sortIndicatorSection() < 0, "a table starts in its natural order");
        click(head, 1);
        check(column_text(*t, 1) == QStringList({"2", "3", "10", "-"}), "the first click sorts a number column ascending, numerically");
        check(head->sortIndicatorSection() == 1 && head->sortIndicatorOrder() == Qt::AscendingOrder, "and shows the arrow");
        click(head, 1);
        check(column_text(*t, 1) == QStringList({"10", "3", "2", "-"}), "the second click sorts descending, with the dash still last");
        click(head, 1);
        check(names(*t) == natural && head->sortIndicatorSection() < 0, "the third click restores the natural order");
        click(head, 0);
        check(names(*t) == QStringList({"Alpha", "bravo", "charlie", "delta"}), "text sorts without regard to case");
        click(head, 1);
        check(column_text(*t, 1) == QStringList({"2", "3", "10", "-"}), "another column starts ascending");
        check(t->cellWidget(0, 3) != nullptr, "cell widgets stay on the table");
        bool follow = true;
        for (int row = 0; row < 4; ++row) {
            follow = follow && qobject_cast<QLabel*>(t->cellWidget(row, 3))->text() == t->item(row, 0)->text();
        }
        check(follow, "cell widgets follow their rows when sorted");
        auto* bravo = t->item(0, 0);
        check(bravo->text() == QStringLiteral("bravo"), "the smallest value is on top");

        for (int row = 0; row < 4; ++row) {
            SortItem::put(t.get(), row, 0, QString::fromLatin1(kRows[row].name));
            SortItem::put(t.get(), row, 1, QString::number(100 - row), 100.0 - row);
        }
        TableHeader::refresh(head);
        check(column_text(*t, 1) == QStringList({"97", "98", "99", "100"}), "a refill in natural order is sorted again by the chosen column");
        check(names(*t) == QStringList({"bravo", "charlie", "Alpha", "delta"}), "and each row keeps its own cells");
        click(head, 1);
        click(head, 1);
        check(names(*t) == natural, "back to natural after a refill restores the filled order");
        click(head, 2);
        click(head, 2);
        click(head, 2);
        click(head, 1);
    }
    {
        auto t = table(QStringLiteral("sorting"), 800);
        check(t->horizontalHeader()->sortIndicatorSection() == 1 && t->horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder,
              "the chosen sort comes back with the table");
        check(t->item(0, 1)->text() == QStringLiteral("2"), "and is applied to the rows");
    }
    {
        auto t = table(QStringLiteral("fixed"), 800, {0, 1, 2, 3});
        const QStringList natural = names(*t);
        click(t->horizontalHeader(), 1);
        check(names(*t) == natural && t->horizontalHeader()->sortIndicatorSection() < 0, "columns that are not sortable ignore clicks");
    }

    {
        auto t = table(QStringLiteral("ties"), 800);
        auto* head = t->horizontalHeader();
        const QStringList natural = names(*t);
        click(head, 2);
        check(names(*t) == natural, "equal values keep the natural order ascending");
        click(head, 2);
        check(head->sortIndicatorOrder() == Qt::DescendingOrder && names(*t) == natural,
              "and descending");
    }
    {
        auto t = table(QStringLiteral("clicks"), 800);
        click(t->horizontalHeader(), 1);
        t.reset();
        check(!store().contains(QStringLiteral("columns/clicks")), "a sort click does not write the column widths");
        check(store().value(QStringLiteral("sort/clicks")).toString() == QStringLiteral("1,asc"), "it writes the sort");
        auto u = table(QStringLiteral("clicks"), 800);
        drag(u->horizontalHeader(), 0, 25);
        u.reset();
        check(store().contains(QStringLiteral("columns/clicks")) && store().value(QStringLiteral("sort/clicks")).toString() == QStringLiteral("1,asc"),
              "a resize writes the widths and leaves the sort alone");
    }

    return g_failures == 0 ? 0 : 1;
}
