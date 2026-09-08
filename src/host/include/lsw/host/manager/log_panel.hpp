#pragma once

#include "lsw/host/apps/log_store.hpp"

#include <QWidget>
#include <QString>
#include <QVector>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QLabel;

namespace lsw::host::manager {

// Read-only log: a scrolling text area pre-populated with everything
// in the LogStore, plus a find bar. Records appended to the store while
// the page is open stream in.
//
// A page in the manager's stack. Several may exist at once (a popped
// out copy alongside the embedded one) and they all read the same
// shared store.
class LogPanel : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(apps::LogStore& store, QWidget* parent = nullptr);

    // How many records are on screen, for the rail's marker.
    [[nodiscard]] int record_count() const noexcept { return shown_; }

Q_SIGNALS:
    void record_count_changed(int count);

private Q_SLOTS:
    void on_find_text_changed(const QString& s);
    void on_find_next();
    void on_find_prev();
    void on_appended(const QVector<apps::LogRecord>& batch);
    void on_cleared();
    void on_autoscroll_toggled(bool on);

private:
    void rebuild_all();
    [[nodiscard]] bool passes(const apps::LogRecord& record) const;
    static QString format_record(const apps::LogRecord& r);

    apps::LogStore& store_;
    QPlainTextEdit* view_ = nullptr;
    QLineEdit* find_edit_ = nullptr;
    QPushButton* find_next_ = nullptr;
    QPushButton* find_prev_ = nullptr;
    QCheckBox* autoscroll_ = nullptr;
    // Which severities are on screen. Toggles rather than a combo:
    // seeing only the errors is two clicks and no menu, and the state
    // of all three is readable at a glance.
    QPushButton* level_buttons_[3]{};
    bool level_shown_[3] = {true, true, true};
    int shown_ = 0;
    QLabel* status_ = nullptr;
};

}  // namespace lsw::host::manager
