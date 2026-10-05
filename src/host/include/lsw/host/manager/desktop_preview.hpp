#pragma once

#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::manager {

// WGC monitor capture over RemoteWin's GPU transport. Hidden previews detach
// their capture connection; they never connect to QEMU's VNC console.
class DesktopPreview : public QWidget {
    Q_OBJECT
public:
    explicit DesktopPreview(link::AgentRpcClient& rpc, QWidget* parent = nullptr);
    ~DesktopPreview() override;

    void set_available(bool available);
    void set_paused(bool paused);
    void clear();

    [[nodiscard]] bool is_paused() const noexcept { return paused_; }

    [[nodiscard]] const QString& status() const noexcept { return status_; }

Q_SIGNALS:
    void status_changed(const QString& status);
    void paused_changed(bool paused);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    class Session;
    void start();
    void stop();
    void fail(const QString& reason);
    void note(const QString& status);

    link::AgentRpcClient& rpc_;
    QVBoxLayout* layout_;
    QLabel* message_;
    QPushButton* play_;
    QPushButton* retry_;
    QPointer<Session> session_;
    QString status_;
    bool available_ = false;
    bool failed_ = false;
    bool starting_ = false;
    bool paused_ = true;
};

}  // namespace lsw::host::manager
