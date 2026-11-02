#include "lsw/host/manager/log_panel.hpp"

#include "lsw/host/config/settings.hpp"
#include "lsw/host/services/log_collector.hpp"
#include "lsw/host/ui/theme.hpp"
#include "lsw/host/ui/widgets.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace lsw::host::manager {

namespace {

enum Severity : int { kInfo = 0, kWarn = 1, kError = 2, kUnnamed = -1 };

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

QString capitalised(QString text) {
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

QTextCharFormat colour_format(const char* colour, bool medium = false) {
    QTextCharFormat format;
    format.setForeground(QColor(QString::fromLatin1(colour)));
    if (medium) {
        format.setFontWeight(QFont::Medium);
    }
    return format;
}

QString windows_label(const QString& relative) {
    static const QRegularExpression rolled{QStringLiteral("\\.\\d+\\.log$")};
    static const QRegularExpression session{QStringLiteral("^logs/agent-session-(\\d+)\\.log$")};
    static const QRegularExpression mirror{QStringLiteral("^logs/mirrors/(.+)\\.log$")};
    if (!relative.endsWith(QLatin1String(".log"), Qt::CaseInsensitive) || relative.contains(rolled)
        || relative.startsWith(QLatin1String("av/"))) {
        return {};
    }
    static const QHash<QString, QString> named{
        {QStringLiteral("logs/agent.log"), QStringLiteral("Agent")},
        {QStringLiteral("logs/update.log"), QStringLiteral("Update")},
        {QStringLiteral("logs/shares.log"), QStringLiteral("Shared folders")},
        {QStringLiteral("logs/winfsp-install.log"), QStringLiteral("WinFsp install")},
        {QStringLiteral("logs/promote.log"), QStringLiteral("End of setup")},
        {QStringLiteral("logs/disk-grow.log"), QStringLiteral("Disk resize")},
        {QStringLiteral("panther/setupact.log"), QStringLiteral("Setup")},
        {QStringLiteral("panther/setuperr.log"), QStringLiteral("Setup errors")},
        {QStringLiteral("setupapi.dev.log"), QStringLiteral("Driver installs")},
        {QStringLiteral("gpu-drivers.log"), QStringLiteral("GPU drivers")},
    };
    if (const auto it = named.find(relative); it != named.end()) {
        return it.value();
    }
    if (const auto match = session.match(relative); match.hasMatch()) {
        return QStringLiteral("Agent session %1").arg(match.captured(1));
    }
    if (const auto match = mirror.match(relative); match.hasMatch()) {
        return match.captured(1) == QLatin1String("desktop") ? QStringLiteral("Desktop mirror")
                                                              : QStringLiteral("Mirror %1").arg(match.captured(1));
    }
    return QFileInfo{relative}.completeBaseName();
}

}

LogPanel::LogPanel(apps::LogStore& store, config::Settings& settings,
                   services::LogCollector& collector, QWidget* parent)
    : QWidget(parent), store_(store), settings_(settings), collector_(collector) {
    setWindowTitle(tr("Log"));
    resize(900, 600);

    using ui::Theme;
    using ui::Widgets;

    time_format_ = colour_format(Theme::Color::kFgFaint);
    level_formats_[kInfo] = colour_format(Theme::Color::kInfo, true);
    level_formats_[kWarn] = colour_format(Theme::Color::kAmber, true);
    level_formats_[kError] = colour_format(Theme::Color::kBad, true);
    level_formats_[3] = colour_format(Theme::Color::kInfo, true);
    source_format_ = colour_format(Theme::Color::kFgFaint);
    text_format_ = colour_format(Theme::Color::kFgDim);
    faint_format_ = colour_format(Theme::Color::kFgFaint);

    auto* root = new QVBoxLayout(this);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    picker_ = new QComboBox;
    picker_->setMinimumWidth(190);
    picker_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    bar->addWidget(picker_);
    find_edit_ = new QLineEdit;
    find_edit_->setPlaceholderText(tr("Find (case-sensitive)"));
    bar->addWidget(find_edit_, 1);
    find_prev_ = new QPushButton(QStringLiteral("▲"));
    find_next_ = new QPushButton(QStringLiteral("▼"));
    bar->addWidget(find_prev_);
    bar->addWidget(find_next_);
    status_ = new QLabel;
    bar->addWidget(status_);
    autoscroll_ = new QCheckBox(tr("Autoscroll"));
    autoscroll_->setChecked(true);
    bar->addWidget(autoscroll_);

    const struct {
        const char* label;
        const char* tone;
    } kLevels[] = {
        {"Info", Theme::Tone::kInfo},
        {"Warn", Theme::Tone::kAmber},
        {"Error", Theme::Tone::kBad},
    };
    for (int i = 0; i < 3; ++i) {
        auto* button = new QPushButton(tr(kLevels[i].label));
        button->setCheckable(true);
        button->setChecked(true);
        button->setProperty(Theme::Tone::kProperty, kLevels[i].tone);
        connect(button, &QPushButton::toggled, this, [this, i](bool on) {
            level_shown_[i] = on;
            filters_changed();
        });
        level_buttons_[i] = button;
        bar->addWidget(button);
    }
    root->addLayout(bar);

    view_ = new QPlainTextEdit;
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    view_->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    view_->setFont(Theme::mono_font(12));
    view_->setMaximumBlockCount(apps::LogStore::kCapacity + 1024);
    root->addWidget(view_, 1);

    auto* foot = new QHBoxLayout;
    foot->setContentsMargins(0, 0, 0, 0);
    auto* open_folder = Widgets::button(tr("Open log folder"), Theme::Role::kQuiet);
    auto* collect = Widgets::button(tr("Collect logs"), Theme::Role::kQuiet);
    note_ = Widgets::meta(QString{});
    foot->addWidget(open_folder);
    foot->addWidget(collect);
    foot->addStretch(1);
    foot->addWidget(note_);
    root->addLayout(foot);

    refill_picker();
    recount();
    update_note();

    connect(picker_, &QComboBox::currentIndexChanged, this, &LogPanel::on_source_changed);
    connect(find_edit_, &QLineEdit::textChanged, this, &LogPanel::on_find_text_changed);
    connect(find_next_, &QPushButton::clicked, this, &LogPanel::on_find_next);
    connect(find_prev_, &QPushButton::clicked, this, &LogPanel::on_find_prev);
    connect(autoscroll_, &QCheckBox::toggled, this, &LogPanel::on_autoscroll_toggled);
    connect(&store_, &apps::LogStore::appended, this, &LogPanel::on_appended);
    connect(&store_, &apps::LogStore::cleared, this, &LogPanel::on_cleared);
    connect(&tail_, &LogFileTail::appended, this, &LogPanel::on_file_appended);
    connect(&tail_, &LogFileTail::replaced, this, [this] {
        if (file_mode() && isVisible()) {
            rebuild();
        }
    });
    connect(&collector_, &services::LogCollector::finished, this, [this] { refill_picker(); });
    connect(open_folder, &QPushButton::clicked, this, [this] {
        QDir{}.mkpath(settings_.logs_dir());
        QDesktopServices::openUrl(QUrl::fromLocalFile(settings_.logs_dir()));
    });
    connect(collect, &QPushButton::clicked, this, &LogPanel::collect_requested);
    connect(find_edit_, &QLineEdit::returnPressed, this, &LogPanel::on_find_next);
    auto* focus_find = new QShortcut(QKeySequence::Find, this);
    connect(focus_find, &QShortcut::activated, this,
            [this] { find_edit_->setFocus(); find_edit_->selectAll(); });
}

QString LogPanel::store_key(const QString& source) {
    return QStringLiteral("store:") + source;
}

QString LogPanel::file_key(const QString& path) {
    return QStringLiteral("file:") + path;
}

QString LogPanel::source() const {
    return picker_->currentData().toString();
}

bool LogPanel::file_mode() const {
    return source().startsWith(QLatin1String("file:"));
}

QString LogPanel::file_path() const {
    return file_mode() ? source().mid(5) : QString{};
}

void LogPanel::select_source(const QString& key) {
    const int index = picker_->findData(key);
    if (index >= 0) {
        picker_->setCurrentIndex(index);
    }
}

void LogPanel::refill_picker() {
    const QString current = picker_->count() > 0 ? source() : QStringLiteral("all");
    const QSignalBlocker quiet(picker_);
    picker_->clear();
    picker_->addItem(tr("All"), QStringLiteral("all"));
    const QStringList fixed{QStringLiteral("manager"), QStringLiteral("qt"), QStringLiteral("agent")};
    picker_->addItem(tr("Manager"), store_key(fixed[0]));
    picker_->addItem(tr("Qt"), store_key(fixed[1]));
    picker_->addItem(tr("Agent"), store_key(fixed[2]));
    for (const QString& name : store_.sources()) {
        if (!fixed.contains(name.toLower())) {
            picker_->addItem(capitalised(name), store_key(name));
        }
    }
    listed_sources_ = store_.sources().size();

    picker_->insertSeparator(picker_->count());
    const QString vm_dir = settings_.vm_logs_dir();
    picker_->addItem(tr("GPU driver"), file_key(QDir{vm_dir}.filePath(QStringLiteral("avgpu.log"))));
    picker_->addItem(tr("QEMU"), file_key(QDir{vm_dir}.filePath(QStringLiteral("qemu.log"))));
    picker_->addItem(tr("virtiofsd"), file_key(QDir{vm_dir}.filePath(QStringLiteral("virtiofsd.log"))));

    const QDir guest{QDir{settings_.logs_dir()}.filePath(QStringLiteral("guest"))};
    QList<std::pair<QString, QString>> windows;
    QDirIterator it{guest.path(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories};
    while (it.hasNext()) {
        const QString relative = guest.relativeFilePath(it.next());
        if (const QString label = windows_label(relative); !label.isEmpty()) {
            windows.append({label, relative});
        }
    }
    std::sort(windows.begin(), windows.end());
    if (!windows.isEmpty()) {
        picker_->insertSeparator(picker_->count());
    }
    for (const auto& [label, relative] : windows) {
        picker_->addItem(tr("Windows: %1").arg(label), file_key(guest.filePath(relative)));
    }

    const int at = picker_->findData(current);
    picker_->setCurrentIndex(at >= 0 ? at : 0);
}

bool LogPanel::wanted_source(const apps::LogRecord& record) const {
    const QString key = source();
    if (key == QLatin1String("all")) {
        return true;
    }
    if (key.startsWith(QLatin1String("store:"))) {
        return apps::LogStore::source_matches(record.source, key.mid(6));
    }
    return true;
}

bool LogPanel::passes(const apps::LogRecord& record) const {
    const Severity severity = severity_of(record);
    return severity == kUnnamed || level_shown_[severity];
}

bool LogPanel::parse_file_line(const QString& line, apps::LogRecord& out) {
    static const QRegularExpression pattern{
        QStringLiteral("^(\\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d\\.\\d{3}) (\\w+)\\s+(.*)$")};
    const auto match = pattern.match(line);
    if (!match.hasMatch()) {
        out.ts_ms = 0;
        out.level.clear();
        out.source.clear();
        out.msg = line;
        return false;
    }
    const QDateTime when =
        QDateTime::fromString(match.captured(1), QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    out.ts_ms = when.isValid() ? static_cast<quint64>(when.toMSecsSinceEpoch()) : 0;
    out.level = match.captured(2).toUpper();
    out.source.clear();
    out.msg = match.captured(3);
    return true;
}

void LogPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refill_picker();
    if (file_mode()) {
        tail_.follow(file_path());
        tail_.set_active(true);
        if (dirty_) {
            rebuild();
        }
    } else {
        catch_up();
    }
    update_note();
}

void LogPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    tail_.set_active(false);
}

void LogPanel::on_source_changed() {
    tail_.follow(file_path());
    tail_.set_active(file_mode() && isVisible());
    filters_changed();
}

void LogPanel::filters_changed() {
    if (isVisible()) {
        rebuild();
        return;
    }
    dirty_ = true;
    recount();
    update_note();
}

void LogPanel::recount() {
    hidden_counted_ = 0;
    if (file_mode()) {
        shown_ = 0;
    } else {
        int count = 0;
        for (const auto& record : store_.records()) {
            if (wanted_source(record) && passes(record)) {
                ++count;
            }
        }
        shown_ = count;
    }
    report_count();
}

void LogPanel::report_count() {
    update_note();
    Q_EMIT record_count_changed(shown_);
}

void LogPanel::update_note() {
    if (file_mode()) {
        note_->setText(tail_.exists() ? file_path() : tr("No file yet"));
    } else {
        note_->setText(tr("%1 records").arg(shown_));
    }
}

void LogPanel::rebuild() {
    if (store_.sources().size() != listed_sources_) {
        refill_picker();
    }
    view_->clear();
    QVector<apps::LogRecord> parsed;
    QVector<const apps::LogRecord*> picked;
    if (file_mode()) {
        parsed.reserve(tail_.lines().size());
        for (const QString& line : tail_.lines()) {
            apps::LogRecord record;
            (void)parse_file_line(line, record);
            parsed.push_back(std::move(record));
        }
        for (const auto& record : parsed) {
            if (passes(record)) {
                picked.push_back(&record);
            }
        }
    } else {
        for (const auto& record : store_.records()) {
            if (wanted_source(record) && passes(record)) {
                picked.push_back(&record);
            }
        }
        std::stable_sort(picked.begin(), picked.end(),
                         [](const auto* a, const auto* b) { return a->ts_ms < b->ts_ms; });
    }
    last_ts_ = picked.isEmpty() ? 0 : picked.last()->ts_ms;
    render(picked);
    rendered_total_ = store_.total();
    shown_ = static_cast<int>(picked.size());
    hidden_counted_ = 0;
    dirty_ = false;
    if (autoscroll_->isChecked()) {
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
    }
    report_count();
}

void LogPanel::catch_up() {
    if (!isVisible() || file_mode()) {
        return;
    }
    if (store_.sources().size() != listed_sources_) {
        refill_picker();
    }
    const quint64 missing = store_.total() - rendered_total_;
    const quint64 held = static_cast<quint64>(store_.records().size());
    if (dirty_ || missing > held || missing > static_cast<quint64>(kRebuildOver)) {
        rebuild();
        return;
    }
    if (missing == 0) {
        return;
    }
    QVector<const apps::LogRecord*> picked;
    const auto& records = store_.records();
    for (qsizetype i = records.size() - static_cast<qsizetype>(missing); i < records.size(); ++i) {
        if (wanted_source(records[i]) && passes(records[i])) {
            picked.push_back(&records[i]);
        }
    }
    rendered_total_ = store_.total();
    if (picked.isEmpty()) {
        return;
    }
    std::stable_sort(picked.begin(), picked.end(),
                     [](const auto* a, const auto* b) { return a->ts_ms < b->ts_ms; });
    if (picked.first()->ts_ms + kReorderMs < last_ts_) {
        rebuild();
        return;
    }
    const bool at_bottom = view_->verticalScrollBar()->value() == view_->verticalScrollBar()->maximum();
    last_ts_ = qMax(last_ts_, picked.last()->ts_ms);
    render(picked);
    shown_ += static_cast<int>(picked.size()) - hidden_counted_;
    hidden_counted_ = 0;
    finish_render(at_bottom);
    report_count();
}

void LogPanel::render(const QVector<const apps::LogRecord*>& records) {
    if (records.isEmpty()) {
        return;
    }
    QTextCursor cursor{view_->document()};
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    bool first = view_->document()->isEmpty();
    for (const apps::LogRecord* record : records) {
        if (!first) {
            cursor.insertBlock();
        }
        first = false;
        if (record->ts_ms == 0 && record->level.isEmpty()) {
            cursor.insertText(record->msg, faint_format_);
            continue;
        }
        const QDateTime when =
            QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(record->ts_ms)).toLocalTime();
        const Severity severity = severity_of(*record);
        cursor.insertText(when.toString(QStringLiteral("HH:mm:ss.zzz")), time_format_);
        cursor.insertText(QStringLiteral("  "), text_format_);
        cursor.insertText(record->level.trimmed().toUpper().leftJustified(5), level_formats_[severity == kUnnamed ? 3 : severity]);
        cursor.insertText(QStringLiteral("  "), text_format_);
        if (!record->source.isEmpty()) {
            cursor.insertText(record->source, source_format_);
            cursor.insertText(QStringLiteral("  "), text_format_);
        }
        cursor.insertText(record->msg, text_format_);
    }
    cursor.endEditBlock();
}

void LogPanel::finish_render(bool was_at_bottom) {
    if (autoscroll_->isChecked() || was_at_bottom) {
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
    }
}

void LogPanel::on_appended(const QVector<apps::LogRecord>& batch) {
    if (batch.isEmpty() || file_mode()) {
        return;
    }
    if (isVisible()) {
        catch_up();
        return;
    }
    int added = 0;
    for (const auto& record : batch) {
        if (wanted_source(record) && passes(record)) {
            ++added;
        }
    }
    shown_ = qMin(shown_ + added, static_cast<int>(store_.records().size()));
    hidden_counted_ += added;
    report_count();
}

void LogPanel::on_file_appended(const QStringList& lines) {
    if (!isVisible() || !file_mode()) {
        return;
    }
    QVector<apps::LogRecord> parsed;
    parsed.reserve(lines.size());
    for (const QString& line : lines) {
        apps::LogRecord record;
        (void)parse_file_line(line, record);
        parsed.push_back(std::move(record));
    }
    QVector<const apps::LogRecord*> picked;
    for (const auto& record : parsed) {
        if (passes(record)) {
            picked.push_back(&record);
        }
    }
    const bool at_bottom = view_->verticalScrollBar()->value() == view_->verticalScrollBar()->maximum();
    render(picked);
    shown_ += static_cast<int>(picked.size());
    finish_render(at_bottom);
    report_count();
}

void LogPanel::on_cleared() {
    listed_sources_ = -1;
    dirty_ = true;
    if (isVisible() && !file_mode()) {
        rebuild();
        return;
    }
    if (!file_mode()) {
        shown_ = 0;
        hidden_counted_ = 0;
        report_count();
    }
}

void LogPanel::on_autoscroll_toggled(bool on) {
    if (on) {
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
    }
}

void LogPanel::on_find_text_changed(const QString& s) {
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
    auto c = view_->textCursor();
    c.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
    view_->setTextCursor(c);
    status_->setText(view_->find(needle, flags) ? tr("(wrapped)") : tr("no match"));
}

}
