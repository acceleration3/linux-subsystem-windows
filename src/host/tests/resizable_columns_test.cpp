#include "lsw/host/ui/resizable_columns.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QApplication>
#include <QHeaderView>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>

using lsw::host::ui::ResizableColumns;
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

std::unique_ptr<QTableWidget> table(const QString& key, int width) {
    auto out = std::make_unique<QTableWidget>(2, 4);
    Theme::rows_table(out.get(), {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")},
                      key, {100, 100, 100, 100}, 3);
    out->resize(width, 200);
    out->show();
    QApplication::processEvents();
    return out;
}

int widths_sum(const QTableWidget& t) {
    return t.horizontalHeader()->length();
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
        check(head->sectionSize(3) == t->viewport()->width() - 300, "the stretch column fills the rest");
        check(head->sectionResizeMode(0) == QHeaderView::Interactive && head->sectionResizeMode(3) == QHeaderView::Interactive,
              "every column is interactive");
        head->resizeSection(1, 160);
        check(head->sectionSize(1) == 160 && head->sectionSize(3) == t->viewport()->width() - 360,
              "a dragged column keeps its width and the stretch column gives way");
        head->resizeSection(0, 5);
        check(head->sectionSize(0) == ResizableColumns::kMinWidth, "a column cannot shrink below the minimum");
        t->resize(1000, 200);
        QApplication::processEvents();
        check(widths_sum(*t) == t->viewport()->width() && head->sectionSize(1) == 160, "growing the table grows only the stretch column");
        t->resize(300, 200);
        QApplication::processEvents();
        check(head->sectionSize(3) == 100, "a narrow table keeps the stretch column at its own width");
    }
    {
        auto first = table(QStringLiteral("kept"), 800);
        first->horizontalHeader()->resizeSection(2, 220);
        first.reset();
        check(store().value(QStringLiteral("columns/kept")).toString().startsWith(QStringLiteral("100,100,220,")),
              "dragged widths are written when the table goes away");
        auto second = table(QStringLiteral("kept"), 800);
        check(second->horizontalHeader()->sectionSize(2) == 220, "a rebuilt table takes the dragged widths");
    }
    {
        auto wide = table(QStringLiteral("filler"), 900);
        wide->horizontalHeader()->resizeSection(3, 60);
        check(wide->horizontalHeader()->sectionSize(3) == wide->viewport()->width() - 300,
              "dragging the stretch column leaves it filling the rest");
        wide.reset();
        auto narrow = table(QStringLiteral("filler"), 500);
        check(narrow->horizontalHeader()->length() <= narrow->viewport()->width() && narrow->horizontalHeader()->sectionSize(3) >= 100,
              "a stretch column drag is not remembered as a minimum");
        store().setValue(QStringLiteral("columns/filler2"), QStringLiteral("100,100,100,800"));
        auto stored = table(QStringLiteral("filler2"), 500);
        check(stored->horizontalHeader()->length() <= stored->viewport()->width(), "a stored stretch width is ignored");
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

    return g_failures == 0 ? 0 : 1;
}
