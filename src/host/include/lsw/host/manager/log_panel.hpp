#pragma once

#include "lsw/host/apps/log_store.hpp"
#include "lsw/host/manager/log_file_tail.hpp"

#include <QString>
#include <QTextCharFormat>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace lsw::host::config { class Settings; }
namespace lsw::host::services { class LogCollector; }

namespace lsw::host::manager {

class LogPanel : public QWidget {
    Q_OBJECT
public:
    static constexpr int kRebuildOver = 2000;
    static constexpr quint64 kReorderMs = 2000;

    LogPanel(apps::LogStore& store, config::Settings& settings, services::LogCollector& collector,
             QWidget* parent = nullptr);

    [[nodiscard]] int shown() const noexcept { return shown_; }
    [[nodiscard]] QString source() const;
    [[nodiscard]] QPlainTextEdit* view() const noexcept { return view_; }
    [[nodiscard]] QComboBox* picker() const noexcept { return picker_; }
    void select_source(const QString& key);

    [[nodiscard]] static QString store_key(const QString& source);
    [[nodiscard]] static QString file_key(const QString& path);

Q_SIGNALS:
    void record_count_changed(int count);
    void collect_requested();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private Q_SLOTS:
    void on_find_text_changed(const QString& s);
    void on_find_next();
    void on_find_prev();
    void on_appended(const QVector<apps::LogRecord>& batch);
    void on_cleared();
    void on_autoscroll_toggled(bool on);
    void on_source_changed();
    void on_file_appended(const QStringList& lines);

private:
    [[nodiscard]] bool file_mode() const;
    [[nodiscard]] QString file_path() const;
    [[nodiscard]] bool wanted_source(const apps::LogRecord& record) const;
    [[nodiscard]] bool passes(const apps::LogRecord& record) const;
    [[nodiscard]] static bool parse_file_line(const QString& line, apps::LogRecord& out);

    void refill_picker();
    void filters_changed();
    void rebuild();
    void catch_up();
    void render(const QVector<const apps::LogRecord*>& records);
    void render_file_lines(const QStringList& lines);
    void finish_render(bool was_at_bottom);
    void recount();
    void report_count();
    void find(bool backward);
    void update_note();

    apps::LogStore& store_;
    config::Settings& settings_;
    services::LogCollector& collector_;
    LogFileTail tail_;
    QPlainTextEdit* view_ = nullptr;
    QComboBox* picker_ = nullptr;
    QLineEdit* find_edit_ = nullptr;
    QPushButton* find_next_ = nullptr;
    QPushButton* find_prev_ = nullptr;
    QCheckBox* autoscroll_ = nullptr;
    QPushButton* level_buttons_[3]{};
    bool level_shown_[3] = {true, true, true};
    QLabel* status_ = nullptr;
    QLabel* note_ = nullptr;
    QTextCharFormat time_format_;
    QTextCharFormat level_formats_[4];
    QTextCharFormat source_format_;
    QTextCharFormat text_format_;
    QTextCharFormat faint_format_;
    int shown_ = 0;
    int hidden_counted_ = 0;
    qsizetype listed_sources_ = -1;
    quint64 rendered_total_ = 0;
    quint64 last_ts_ = 0;
    bool dirty_ = true;
};

}
