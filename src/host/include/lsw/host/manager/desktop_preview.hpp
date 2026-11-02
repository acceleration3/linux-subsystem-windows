#pragma once

#include <QImage>
#include <QPointer>
#include <QString>
#include <QWidget>

class QLabel;

namespace lsw::host::link {
class AgentRpcClient;
}

namespace lsw::host::manager {
class DesktopPreview : public QWidget {
    Q_OBJECT
public:
    explicit DesktopPreview(link::AgentRpcClient& rpc, QWidget* parent = nullptr);
    ~DesktopPreview() override;

    void set_available(bool available);
    void set_paused(bool paused);
    void clear(const QString& why);

    [[nodiscard]] bool is_paused() const noexcept { return paused_; }

    [[nodiscard]] const QString& status() const noexcept { return status_; }

Q_SIGNALS:
    void paused_changed(bool paused);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    class Session;
    class Surface;

    void start();
    void stop();
    void fail(const QString& reason);
    void note(const QString& status);
    void freeze();
    void toggle();
    void paint_surface(QPainter& p, const QRect& area) const;
    void lay_out();

    link::AgentRpcClient& rpc_;
    Surface* surface_;
    QLabel* tag_;
    QPointer<Session> session_;
    QImage frozen_;
    QString status_;
    bool available_ = false;
    bool failed_ = false;
    bool starting_ = false;
    bool paused_ = true;
    bool hovered_ = false;
};

}
