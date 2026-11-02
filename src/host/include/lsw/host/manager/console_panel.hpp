#pragma once

#include <QWidget>
#include <QString>

#include <optional>

class QComboBox;
class QPlainTextEdit;
class QLineEdit;
class QLabel;

namespace lsw::host::services {
class GuestShells;
class StatusHub;
}

namespace lsw::host::manager {
class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    ConsolePanel(services::GuestShells& shells, services::StatusHub& status,
                 QWidget* parent = nullptr);

    void clear_transcript();

    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();
    void open_failed(const QString& reason);
    void opened();

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void on_input_submitted();
    void rebuild_picker();
    void on_picker(int index);
    void show_shell(int session);
    void on_agent(bool online);
    void on_shell_changed(int session);
    void refresh();
    void append_output(const QString& text);

    services::GuestShells& shells_;
    services::StatusHub& hub_;
    bool agent_online_ = false;
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* picker_ = nullptr;
    std::optional<int> wanted_;
    std::optional<int> shown_;
};

}
