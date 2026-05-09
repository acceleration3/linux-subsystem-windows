#pragma once

#include "lsw/manager/apps/log_store.hpp"

#include <QDialog>
#include <QString>
#include <QVector>

class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QLabel;

namespace lsw::manager::ui::manager {

// Read-only log viewer: a plain-text scrolling area pre-populated with
// everything in the LogStore plus a find bar. Live records appended
// to the store while the viewer is open are auto-streamed in.
//
// Modeless — opening multiple is fine; each runs against the same
// shared store. Closing the dialog deletes it (Qt::WA_DeleteOnClose).
class LogViewer : public QDialog {
    Q_OBJECT
public:
    explicit LogViewer(apps::LogStore& store, QWidget* parent = nullptr);

private Q_SLOTS:
    void on_find_text_changed(const QString& s);
    void on_find_next();
    void on_find_prev();
    void on_appended(const QVector<apps::LogRecord>& batch);
    void on_cleared();
    void on_autoscroll_toggled(bool on);

private:
    void rebuild_all();
    static QString format_record(const apps::LogRecord& r);

    apps::LogStore& store_;
    QPlainTextEdit* view_ = nullptr;
    QLineEdit* find_edit_ = nullptr;
    QPushButton* find_next_ = nullptr;
    QPushButton* find_prev_ = nullptr;
    QCheckBox* autoscroll_ = nullptr;
    QLabel* status_ = nullptr;
};

}  // namespace lsw::manager::ui::manager
