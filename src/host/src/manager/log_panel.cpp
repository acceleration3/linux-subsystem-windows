#include "lsw/host/manager/log_panel.hpp"

#include "lsw/host/ui/theme.hpp"

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

namespace lsw::host::manager {

namespace {

// The severities the buttons filter by, in button order.
enum Severity : int { kInfo = 0, kWarn = 1, kError = 2, kUnnamed = -1 };

// Which button a record answers to. Debug counts as info; anything that
// is not one of the named levels answers to none of them.
Severity severity_of(const apps::LogRecord& record) {
    const QString level = record.level.trimmed().toUpper();
    if (level.startsWith(QLatin1Char('E'))) {
        return kError;
    }
    if (level.startsWith(QLatin1Char('W'))) {
        return kWarn;
    }
    if (level.startsWith(QLatin1Char('I')) || level.startsWith(QLatin1Char('D'))) {
        return kInfo;
    }
    return kUnnamed;
}

}  // namespace

LogPanel::LogPanel(apps::LogStore& store, QWidget* parent)
    : QWidget(parent), store_(store) {
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

    // Severity, as three latching buttons. The colours are the ones the
    // rest of the application uses for the same three meanings.
    const struct {
        const char* label;
        const char* tone;
    } kLevels[] = {
        {"Info", ui::Theme::Tone::kInfo},
        {"Warn", ui::Theme::Tone::kAmber},
        {"Error", ui::Theme::Tone::kBad},
    };
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(tr(kLevels[i].label));
        button->setCheckable(true);
        button->setChecked(true);
        button->setProperty(ui::Theme::Tone::kProperty, kLevels[i].tone);
        connect(button, &QPushButton::toggled, this, [this, i](bool on) {
            level_shown_[i] = on;
            rebuild_all();
        });
        level_buttons_[i] = button;
        bar->addWidget(button);
    }
    root->addLayout(bar);

    view_ = new QPlainTextEdit;
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setFont(ui::Theme::mono_font(12));
    view_->setMaximumBlockCount(apps::LogStore::kCapacity + 1024);
    root->addWidget(view_, 1);

    rebuild_all();

    connect(find_edit_, &QLineEdit::textChanged, this, &LogPanel::on_find_text_changed);
    connect(find_next_, &QPushButton::clicked, this, &LogPanel::on_find_next);
    connect(find_prev_, &QPushButton::clicked, this, &LogPanel::on_find_prev);
    connect(autoscroll_, &QCheckBox::toggled, this, &LogPanel::on_autoscroll_toggled);
    connect(&store_, &apps::LogStore::appended, this, &LogPanel::on_appended);
    connect(&store_, &apps::LogStore::cleared, this, &LogPanel::on_cleared);

    // Enter in the find box jumps to next match. Ctrl+F focuses find.
    connect(find_edit_, &QLineEdit::returnPressed, this, &LogPanel::on_find_next);
    auto* focus_find = new QShortcut(QKeySequence::Find, this);
    connect(focus_find, &QShortcut::activated, this,
            [this] { find_edit_->setFocus(); find_edit_->selectAll(); });
}

QString LogPanel::format_record(const apps::LogRecord& r) {
    // Three columns by width rather than by table: timestamp, level,
    // message, with the level in the colour that means that everywhere
    // else in the application. Plain text keeps find working across it.
    const QDateTime dt =
        QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(r.ts_ms)).toLocalTime();
    const QString level = r.level.trimmed().toUpper();
    const Severity severity = severity_of(r);
    const char* colour = severity == kError  ? ui::Theme::Color::kBad
                         : severity == kWarn ? ui::Theme::Color::kAmber
                                             : ui::Theme::Color::kInfo;
    return QStringLiteral("<span style=\"color:%1\">%2</span>  "
                          "<span style=\"color:%3;font-weight:500\">%4</span>  "
                          "<span style=\"color:%5\">%6</span>")
        .arg(QString::fromLatin1(ui::Theme::Color::kFgFaint),
             dt.toString(QStringLiteral("HH:mm:ss.zzz")), QString::fromLatin1(colour),
             level.leftJustified(5, QChar(u'\u00a0')),
             QString::fromLatin1(ui::Theme::Color::kFgDim), r.msg.toHtmlEscaped());
}

bool LogPanel::passes(const apps::LogRecord& record) const {
    // Anything that is not one of the three named levels is shown
    // whatever the buttons say: a record nobody can filter is worse
    // than one nobody asked for.
    const Severity severity = severity_of(record);
    return severity == kUnnamed || level_shown_[severity];
}

void LogPanel::rebuild_all() {
    QStringList lines;
    const auto& recs = store_.records();
    lines.reserve(recs.size());
    for (const auto& r : recs) {
        if (!passes(r)) {
            continue;
        }
        lines << format_record(r);
    }
    view_->clear();
    view_->appendHtml(lines.join(QStringLiteral("<br>")));
    if (autoscroll_->isChecked()) {
        view_->moveCursor(QTextCursor::End);
    }
    shown_ = static_cast<int>(lines.size());
    Q_EMIT record_count_changed(shown_);
}

void LogPanel::on_appended(const QVector<apps::LogRecord>& batch) {
    if (batch.isEmpty()) {
        return;
    }
    const bool was_at_bottom = view_->verticalScrollBar()->value()
                                == view_->verticalScrollBar()->maximum();
    int added = 0;
    for (const auto& r : batch) {
        if (!passes(r)) {
            continue;
        }
        view_->appendHtml(format_record(r));
        ++added;
    }
    if (autoscroll_->isChecked() || was_at_bottom) {
        view_->moveCursor(QTextCursor::End);
    }
    // The store keeps a bounded window and so does the view; the count
    // is of what is still there, not of everything ever appended.
    shown_ = qMin(shown_ + added, static_cast<int>(store_.records().size()));
    Q_EMIT record_count_changed(shown_);
}

void LogPanel::on_cleared() {
    view_->clear();
    shown_ = 0;
    Q_EMIT record_count_changed(0);
}

void LogPanel::on_autoscroll_toggled(bool on) {
    if (on) {
        view_->moveCursor(QTextCursor::End);
    }
}

void LogPanel::on_find_text_changed(const QString& s) {
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

void LogPanel::on_find_next() {
    find(false);
}

void LogPanel::on_find_prev() {
    find(true);
}

void LogPanel::find(bool backward) {
    const auto needle = find_edit_->text();
    if (needle.isEmpty()) {
        return;
    }
    const QTextDocument::FindFlags flags =
        backward ? QTextDocument::FindBackward : QTextDocument::FindFlags{};
    if (view_->find(needle, flags)) {
        status_->setText({});
        return;
    }
    // Wrap to the far end and try once more.
    auto c = view_->textCursor();
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    view_->setTextCursor(c);
    status_->setText(view_->find(needle, flags) ? tr("(wrapped)") : tr("no match"));
}

}  // namespace lsw::host::manager
