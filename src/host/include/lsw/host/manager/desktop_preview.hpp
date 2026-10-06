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

// The overview's preview of the guest's desktop: its primary monitor
// captured inside Windows (WGC) over remotewin's monitor mirror, drawn
// read-only, as docs/design/lsw-ui.html draws it.
//
// It starts paused: shown with a guest to capture, it takes one frame and
// lets the capture go, and keeps that frame desaturated and dimmed under
// the play badge, with the tag in its corner saying "paused". A click
// plays it live; a second click pauses it again on the frame it was
// showing. Hidden, it captures nothing. With nothing to capture it is
// flat grey with no badge, and the tag says why.
class DesktopPreview : public QWidget {
    Q_OBJECT
public:
    explicit DesktopPreview(link::AgentRpcClient& rpc, QWidget* parent = nullptr);
    ~DesktopPreview() override;

    // Whether the guest's agent is there to start a capture.
    void set_available(bool available);
    void set_paused(bool paused);
    // Nothing to capture, and ``why`` in the tag: no guest, or it is off.
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
    // The view's picture as it is now, kept greyed for the paused look.
    void freeze();
    void toggle();
    void paint_surface(QPainter& p, const QRect& area) const;
    void lay_out();

    link::AgentRpcClient& rpc_;
    // Over the live view: the paused frame, the badge and the hover dim.
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

}  // namespace lsw::host::manager
