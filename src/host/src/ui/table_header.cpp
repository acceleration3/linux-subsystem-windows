#include "lsw/host/ui/table_header.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QHash>
#include <QHeaderView>
#include <QSettings>
#include <QStringList>
#include <QTableWidget>

namespace lsw::host::ui {

namespace {

constexpr int kSaveDelayMs = 400;
constexpr int kMaxWidth = 4000;
constexpr const char* kNaturalProperty = "lswNaturalOrder";
constexpr const char* kDescendingProperty = "lswDescending";

struct Remembered {
    QVector<int> widths;
    int column = -1;
    Qt::SortOrder order = Qt::AscendingOrder;
    bool has_widths = false;
    bool has_sort = false;
};

QHash<QString, Remembered>& remembered() {
    static QHash<QString, Remembered> store;
    return store;
}

QSettings open_store() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lsw"),
                     QStringLiteral("manager"));
}

Remembered load(const QString& key, int columns) {
    const auto memory = remembered().constFind(key);
    if (memory != remembered().constEnd()) {
        return *memory;
    }
    Remembered out;
    const QSettings store = open_store();
    if (store.status() != QSettings::NoError) {
        return out;
    }
    const QStringList parts =
        store.value(QStringLiteral("columns/") + key).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    QVector<int> widths;
    for (const QString& part : parts) {
        bool ok = false;
        const int width = part.toInt(&ok);
        if (!ok || width < TableHeader::kMinWidth || width > kMaxWidth) {
            widths.clear();
            break;
        }
        widths.push_back(width);
    }
    if (widths.size() == columns) {
        out.widths = widths;
        out.has_widths = true;
    }
    const QStringList sort = store.value(QStringLiteral("sort/") + key).toString().split(QLatin1Char(','));
    if (sort.size() == 2) {
        bool ok = false;
        const int column = sort.at(0).toInt(&ok);
        if (ok && column >= 0 && column < columns
            && (sort.at(1) == QLatin1String("asc") || sort.at(1) == QLatin1String("desc"))) {
            out.column = column;
            out.order = sort.at(1) == QLatin1String("desc") ? Qt::DescendingOrder : Qt::AscendingOrder;
            out.has_sort = true;
        }
    }
    remembered().insert(key, out);
    return out;
}

}

TableHeader* TableHeader::attach(QHeaderView* header, const QString& key, const QVector<int>& widths,
                                 const QVector<int>& unsortable) {
    if (header == nullptr || widths.isEmpty()) {
        return nullptr;
    }
    return new TableHeader(header, key, widths, unsortable);
}

void TableHeader::refresh(QHeaderView* header) {
    if (header == nullptr) {
        return;
    }
    if (auto* self = header->findChild<TableHeader*>(QString{}, Qt::FindDirectChildrenOnly)) {
        self->apply();
    }
}

bool TableHeader::natural(const QHeaderView* header) {
    return header->property(kNaturalProperty).toBool();
}

Qt::SortOrder TableHeader::order(const QHeaderView* header) {
    return header->property(kDescendingProperty).toBool() ? Qt::DescendingOrder : Qt::AscendingOrder;
}

TableHeader::TableHeader(QHeaderView* header, const QString& key, const QVector<int>& widths,
                         const QVector<int>& unsortable)
    : QObject(header), header_(header), key_(key), widths_(widths), unsortable_(unsortable) {
    header_->setSectionsMovable(false);
    header_->setMinimumSectionSize(kMinWidth);
    header_->setSectionResizeMode(QHeaderView::Interactive);
    header_->setStretchLastSection(true);
    header_->setSectionsClickable(true);
    header_->setSortIndicatorShown(true);

    const Remembered saved = load(key_, static_cast<int>(widths_.size()));
    if (saved.has_widths) {
        widths_ = saved.widths;
    }
    if (saved.has_sort && !unsortable_.contains(saved.column)) {
        column_ = saved.column;
        order_ = saved.order;
    }
    const int last = static_cast<int>(widths_.size()) - 1;
    for (int column = 0; column < last && column < header_->count(); ++column) {
        header_->resizeSection(column, qMax(kMinWidth, widths_.at(column)));
    }

    save_.setSingleShot(true);
    save_.setInterval(kSaveDelayMs);
    connect(&save_, &QTimer::timeout, this, &TableHeader::flush);
    connect(header_, &QHeaderView::sectionResized, this,
            [this](int column, int, int now) { resized(column, now); });
    connect(header_, &QHeaderView::sectionClicked, this, &TableHeader::clicked);
    connect(header_, &QHeaderView::geometriesChanged, this, &TableHeader::refit);
    header_->setSortIndicator(column_, order_);
    apply();
}

TableHeader::~TableHeader() {
    flush();
}

void TableHeader::resized(int column, int now) {
    if (column < 0 || column >= widths_.size() - 1) {
        refit();
        return;
    }
    widths_[column] = qBound(kMinWidth, now, kMaxWidth);
    Remembered& entry = remembered()[key_];
    entry.widths = widths_;
    entry.has_widths = true;
    dirty_widths_ = true;
    save_.start();
    refit();
}

void TableHeader::clicked(int column) {
    if (column < 0 || column >= widths_.size() || unsortable_.contains(column)) {
        header_->setSortIndicator(column_, order_);
        return;
    }
    if (column_ != column) {
        column_ = column;
        order_ = Qt::AscendingOrder;
    } else if (order_ == Qt::AscendingOrder) {
        order_ = Qt::DescendingOrder;
    } else {
        column_ = -1;
        order_ = Qt::AscendingOrder;
    }
    Remembered& entry = remembered()[key_];
    entry.column = column_;
    entry.order = order_;
    entry.has_sort = true;
    dirty_sort_ = true;
    save_.start();
    header_->setSortIndicator(column_, order_);
    apply();
}

void TableHeader::apply() {
    auto* view = qobject_cast<QAbstractItemView*>(header_->parentWidget());
    if (view == nullptr || view->model() == nullptr) {
        return;
    }
    header_->setProperty(kNaturalProperty, column_ < 0);
    header_->setProperty(kDescendingProperty, order_ == Qt::DescendingOrder);
    view->model()->sort(column_ < 0 ? 0 : column_, column_ < 0 ? Qt::AscendingOrder : order_);
}

void TableHeader::refit() const {
    if (refitting_) {
        return;
    }
    if (auto* table = qobject_cast<QTableWidget*>(header_->parentWidget())) {
        const_cast<TableHeader*>(this)->refitting_ = true;
        Theme::fit_rows(table);
        const_cast<TableHeader*>(this)->refitting_ = false;
    }
}

void TableHeader::flush() {
    if (!dirty_widths_ && !dirty_sort_) {
        return;
    }
    QSettings store = open_store();
    if (store.status() != QSettings::NoError) {
        return;
    }
    if (dirty_widths_) {
        QStringList parts;
        for (const int width : std::as_const(widths_)) {
            parts << QString::number(width);
        }
        store.setValue(QStringLiteral("columns/") + key_, parts.join(QLatin1Char(',')));
    }
    if (dirty_sort_) {
        const QString direction = column_ < 0                         ? QStringLiteral("none")
                                  : order_ == Qt::DescendingOrder ? QStringLiteral("desc")
                                                                  : QStringLiteral("asc");
        store.setValue(QStringLiteral("sort/") + key_, QStringLiteral("%1,%2").arg(qMax(column_, 0)).arg(direction));
    }
    dirty_widths_ = false;
    dirty_sort_ = false;
    store.sync();
}

}
