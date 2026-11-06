#include "lsw/host/ui/resizable_columns.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QEvent>
#include <QHash>
#include <QHeaderView>
#include <QSettings>
#include <QStringList>
#include <QTableWidget>

namespace lsw::host::ui {

namespace {

constexpr int kSaveDelayMs = 400;
constexpr int kMaxWidth = 4000;

QHash<QString, QVector<int>>& remembered() {
    static QHash<QString, QVector<int>> widths;
    return widths;
}

QString settings_key(const QString& key) {
    return QStringLiteral("columns/") + key;
}

QSettings open_store() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("lsw"),
                     QStringLiteral("manager"));
}

}

void ResizableColumns::attach(QHeaderView* header, const QString& key, const QVector<int>& widths,
                              int stretch) {
    if (header == nullptr || widths.isEmpty()) {
        return;
    }
    new ResizableColumns(header, key, widths, stretch);
}

ResizableColumns::ResizableColumns(QHeaderView* header, const QString& key, const QVector<int>& widths,
                                   int stretch)
    : QObject(header), header_(header), key_(key), widths_(widths),
      stretch_(stretch < 0 || stretch >= widths.size() ? static_cast<int>(widths.size()) - 1 : stretch) {
    header_->setStretchLastSection(false);
    header_->setSectionsMovable(false);
    header_->setMinimumSectionSize(kMinWidth);
    header_->setSectionResizeMode(QHeaderView::Interactive);
    header_->setCascadingSectionResizes(false);

    const QVector<int> saved = stored();
    if (saved.size() == widths_.size()) {
        const int fixed = widths_.at(stretch_);
        widths_ = saved;
        widths_[stretch_] = fixed;
    }
    applying_ = true;
    for (int column = 0; column < widths_.size() && column < header_->count(); ++column) {
        header_->resizeSection(column, qMax(kMinWidth, widths_.at(column)));
    }
    applying_ = false;

    save_.setSingleShot(true);
    save_.setInterval(kSaveDelayMs);
    connect(&save_, &QTimer::timeout, this, &ResizableColumns::flush);
    connect(header_, &QHeaderView::sectionResized, this,
            [this](int column, int, int now) { resized(column, now); });
    header_->installEventFilter(this);
    fill();
}

ResizableColumns::~ResizableColumns() {
    flush();
}

bool ResizableColumns::eventFilter(QObject* watched, QEvent* event) {
    if (watched == header_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        fill();
    }
    return QObject::eventFilter(watched, event);
}

void ResizableColumns::fill() {
    if (applying_ || stretch_ >= header_->count()) {
        return;
    }
    const int room = header_->width();
    const int others = header_->length() - header_->sectionSize(stretch_);
    const int wanted = qMax(qMax(kMinWidth, widths_.at(stretch_)), room - others);
    if (wanted != header_->sectionSize(stretch_)) {
        applying_ = true;
        header_->resizeSection(stretch_, wanted);
        applying_ = false;
    }
    refit();
}

void ResizableColumns::resized(int column, int now) {
    if (applying_ || column < 0 || column >= widths_.size()) {
        return;
    }
    if (column == stretch_) {
        fill();
        return;
    }
    widths_[column] = qBound(kMinWidth, now, kMaxWidth);
    remembered().insert(key_, widths_);
    dirty_ = true;
    save_.start();
    fill();
}

void ResizableColumns::refit() const {
    if (auto* table = qobject_cast<QTableWidget*>(header_->parentWidget())) {
        Theme::fit_rows(table);
    }
}

QVector<int> ResizableColumns::stored() const {
    const auto memory = remembered().constFind(key_);
    if (memory != remembered().constEnd()) {
        return *memory;
    }
    const QSettings store = open_store();
    if (store.status() != QSettings::NoError) {
        return {};
    }
    const QStringList parts = store.value(settings_key(key_)).toString().split(QLatin1Char(','), Qt::SkipEmptyParts);
    QVector<int> out;
    for (const QString& part : parts) {
        bool ok = false;
        const int width = part.toInt(&ok);
        if (!ok || width < kMinWidth || width > kMaxWidth) {
            return {};
        }
        out.push_back(width);
    }
    if (!out.isEmpty()) {
        remembered().insert(key_, out);
    }
    return out;
}

void ResizableColumns::flush() {
    if (!dirty_) {
        return;
    }
    dirty_ = false;
    QStringList parts;
    for (const int width : std::as_const(widths_)) {
        parts << QString::number(width);
    }
    QSettings store = open_store();
    if (store.status() != QSettings::NoError) {
        return;
    }
    store.setValue(settings_key(key_), parts.join(QLatin1Char(',')));
    store.sync();
}

}
