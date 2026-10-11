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
}  // namespace lsw::host::services

namespace lsw::host::manager {

// Manager-side terminal for the guest: a view onto one of the manager's
// guest shells (services::GuestShells). It shows that shell's transcript
// and sends what is typed below to it.
//
// The shells are not the page's. The agent is a session-0 service, so
// "the guest" is not one place: the picker in the input band chooses the
// Windows session, a user's (as that user, elevated) or session 0 (as
// SYSTEM), and every view that picks the same session shows the same
// cmd.exe. Leaving the page, or closing a pop-out, leaves the shell
// running where it was. The session list is the hub's; it is only polled
// while a console is on screen.
class ConsolePanel : public QWidget {
    Q_OBJECT
public:
    // ``status`` says whether the agent answers, and which Windows
    // sessions there are to pick from.
    ConsolePanel(services::GuestShells& shells, services::StatusHub& status,
                 QWidget* parent = nullptr);

    // Empties the shown shell's transcript, in every view of it. The
    // shell keeps running: this is the transcript, not the shell.
    void clear_transcript();

    // The page's one-line detail: where the shown shell runs and as whom,
    // or why there is none. ``summary_changed`` when that changes.
    [[nodiscard]] QString summary() const;

Q_SIGNALS:
    void summary_changed();
    // The agent would not open the shown shell, and why; ``opened`` when
    // it did.
    void open_failed(const QString& reason);
    void opened();

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void on_input_submitted();
    // The hub's session list changed: re-fill the picker, keeping the
    // choice when it still exists, and show what it then selects.
    void rebuild_picker();
    // The user picked a Windows session.
    void on_picker(int index);
    // Show ``session``'s shell, opening it when this is on screen.
    void show_shell(int session);
    // The agent started or stopped answering.
    void on_agent(bool online);
    void on_shell_changed(int session);
    // The status line and the input, from the shown shell's state.
    void refresh();
    void append_output(const QString& text);

    services::GuestShells& shells_;
    services::StatusHub& hub_;
    bool agent_online_ = false;
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* picker_ = nullptr;
    // The Windows session picked; unset means the agent's default (the
    // console user's). ``shown_`` is the one whose shell is on screen.
    std::optional<int> wanted_;
    std::optional<int> shown_;
};

}  // namespace lsw::host::manager
