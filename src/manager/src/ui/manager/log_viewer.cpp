#include "lsw/manager/ui/manager/log_viewer.hpp"

#include <QCheckBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QTextCursor>
#include <QVBoxLayout>

namespace lsw::manager::ui::manager {

LogViewer::LogViewer(apps::LogStore& store, QWidget* parent)
    : QDialog(parent), store_(store) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Agent Log"));
    resize(900, 600);

    auto* root = new QVBoxLayout(this);

    // Find bar — text + prev/next + match status + autoscroll toggle.
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->addWidget(new QLabel(tr("Find:")));
    find_edit_ = new QLineEdit;
    find_edit_->setPlaceholderText(tr("substring (case-sensitive)"));
    bar->addWidget(find_edit_, 1);
    find_prev_ = new QPushButton(tr("▲"));   // ▲
    find_next_ = new QPushButton(tr("▼"));   // ▼
    bar->addWidget(find_prev_);
    bar->addWidget(find_next_);
    status_ = new QLabel;
    bar->addWidget(status_);
    autoscroll_ = new QCheckBox(tr("Autoscroll"));
    autoscroll_->setChecked(true);
    bar->addWidget(autoscroll_);
    root->addLayout(bar);

    view_ = new QPlainTextEdit;
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    view_->setFont(mono);
    view_->setMaximumBlockCount(apps::LogStore::kCapacity + 1024);
    root->addWidget(view_, 1);

    rebuild_all();

    connect(find_edit_, &QLineEdit::textChanged, this, &LogViewer::on_find_text_changed);
    connect(find_next_, &QPushButton::clicked, this, &LogViewer::on_find_next);
    connect(find_prev_, &QPushButton::clicked, this, &LogViewer::on_find_prev);
    connect(autoscroll_, &QCheckBox::toggled, this, &LogViewer::on_autoscroll_toggled);
    connect(&store_, &apps::LogStore::appended, this, &LogViewer::on_appended);
    connect(&store_, &apps::LogStore::cleared, this, &LogViewer::on_cleared);

    // Enter in the find box jumps to next match. Ctrl+F focuses find.
    connect(find_edit_, &QLineEdit::returnPressed, this, &LogViewer::on_find_next);
    auto* focus_find = new QShortcut(QKeySequence::Find, this);
    connect(focus_find, &QShortcut::activated, this,
            [this] { find_edit_->setFocus(); find_edit_->selectAll(); });
}

QString LogViewer::format_record(const apps::LogRecord& r) {
    const QDateTime dt =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(r.ts_ms)).toLocalTime();
    return QStringLiteral("[%1 %2] %3")
        .arg(dt.toString(QStringLiteral("HH:mm:ss.zzz")),
             r.level.leftJustified(5, QLatin1Char(' ')),
             r.msg);
}

void LogViewer::rebuild_all() {
    QStringList lines;
    const auto& recs = store_.records();
    lines.reserve(recs.size());
    for (const auto& r : recs) {
        lines << format_record(r);
    }
    view_->setPlainText(lines.join(QLatin1Char('\n')));
    if (autoscroll_->isChecked()) {
        view_->moveCursor(QTextCursor::End);
    }
}

void LogViewer::on_appended(const QVector<apps::LogRecord>& batch) {
    if (batch.isEmpty()) {
        return;
    }
    const bool was_at_bottom = view_->verticalScrollBar()->value()
                                == view_->verticalScrollBar()->maximum();
    auto cursor = view_->textCursor();
    cursor.movePosition(QTextCursor::End);
    for (const auto& r : batch) {
        if (cursor.position() > 0) {
            cursor.insertText(QStringLiteral("\n"));
        }
        cursor.insertText(format_record(r));
    }
    if (autoscroll_->isChecked() || was_at_bottom) {
        view_->moveCursor(QTextCursor::End);
    }
}

void LogViewer::on_cleared() {
    view_->clear();
}

void LogViewer::on_autoscroll_toggled(bool on) {
    if (on) {
        view_->moveCursor(QTextCursor::End);
    }
}

void LogViewer::on_find_text_changed(const QString& s) {
    // Reset cursor to start of selection so the next ``Find`` searches
    // from the same spot — feels right when you're refining the term.
    if (s.isEmpty()) {
        status_->setText({});
        auto c = view_->textCursor();
        c.clearSelection();
        view_->setTextCursor(c);
        return;
    }
    on_find_next();
}

void LogViewer::on_find_next() {
    const auto needle = find_edit_->text();
    if (needle.isEmpty()) {
        return;
    }
    if (!view_->find(needle)) {
        // Wrap to top.
        auto c = view_->textCursor();
        c.movePosition(QTextCursor::Start);
        view_->setTextCursor(c);
        if (view_->find(needle)) {
            status_->setText(tr("(wrapped)"));
        } else {
            status_->setText(tr("no match"));
        }
    } else {
        status_->setText({});
    }
}

void LogViewer::on_find_prev() {
    const auto needle = find_edit_->text();
    if (needle.isEmpty()) {
        return;
    }
    if (!view_->find(needle, QTextDocument::FindBackward)) {
        auto c = view_->textCursor();
        c.movePosition(QTextCursor::End);
        view_->setTextCursor(c);
        if (view_->find(needle, QTextDocument::FindBackward)) {
            status_->setText(tr("(wrapped)"));
        } else {
            status_->setText(tr("no match"));
        }
    } else {
        status_->setText({});
    }
}

}  // namespace lsw::manager::ui::manager
