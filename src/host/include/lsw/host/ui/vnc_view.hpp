#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <thread>

// Forward-declare libvncclient's opaque client struct so we don't drag
// <rfb/rfbclient.h> into every consumer's translation unit.
struct _rfbClient;

class QEnterEvent;
class QMouseEvent;
class QPainter;

namespace lsw::host::ui {

// Embedded VNC viewer widget. Connects to a host:port via libvncclient,
// runs the RFB message loop on a worker thread, and paints the latest
// framebuffer into the widget on each Qt repaint cycle.
//
// Two ways to use it. Plain, the default, is the wizard's install view:
// a live surface with no controls that keeps reconnecting while it is
// meant to stream. Preview mode (``set_preview``) is the overview's: it
// starts paused, connecting only long enough to take one frame of the
// guest's screen and then dropping the connection, because a live stream costs host
// CPU and guest display work for a thumbnail nobody may be watching. A
// click plays it live, another pauses it on the last frame.
//
// Threading:
//   * The worker thread owns ``client_`` and is the only thread that
//     calls libvncclient functions.
//   * ``image_`` is the framebuffer; libvncclient writes directly into
//     ``image_.bits()`` (we set ``client_->frameBuffer`` to it). Worker
//     and GUI thread coordinate via ``image_mutex_``.
//   * On a framebuffer update the worker queues an ``update()`` on the
//     GUI thread via QMetaObject::invokeMethod; the end of each
//     update message of a paused start is reported the same way.
class VncView : public QWidget {
    Q_OBJECT
public:
    explicit VncView(QWidget* parent = nullptr);
    ~VncView() override;

    // Connect to ``host:port``. Idempotent re-call: tears down any
    // existing session first. Safe to call before ``show()``. The
    // host/port are remembered so ``play()`` can reconnect after a
    // pause. In preview mode this is a paused start: one frame, then
    // the connection goes.
    void connect_to(const QString& host, int port);

    // Tear down the session and stop the worker. Called automatically
    // on destruction.
    void disconnect();

    // Opt into the preview behaviour described above: the paused start,
    // click to play or pause, and the paused look (the frame desaturated
    // and dimmed under a pause badge). Off by default, so the wizard's
    // install view stays a plain live surface.
    void set_preview(bool on);

    // Drop the session, the target and any frame: nothing to show, and
    // nothing to play. The preview for no guest, or a guest that is off.
    void clear();

    // Reconnect to the last ``connect_to`` target live / freeze on the
    // last frame and drop the connection. Clicking the preview toggles
    // between these.
    void play();
    void pause();

    // Preview mode with a target and not streaming live, which includes
    // the wait for the first frame of a paused start.
    [[nodiscard]] bool is_paused() const;
    // The last ``connect_to`` port, or -1 after ``clear()``.
    [[nodiscard]] int port() const { return last_port_; }

Q_SIGNALS:
    void paused_changed(bool paused);

protected:
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    // (Re)connect to the remembered target. ``pause_on_frame`` makes it
    // a paused start.
    void open_session(bool pause_on_frame);
    void worker_loop();
    void resize_buffer(int w, int h);
    // A paused start's session finished an update with something drawn:
    // time the settling that ends in the pause (see kSettleMs).
    void frame_arrived(int session);
    // Keep the frame just drawn, greyed, for painting while paused.
    void freeze_frame();
    // The badge (paused) or the hover control (playing) in the middle.
    void paint_badge(QPainter& p, bool paused) const;
    // Emits ``paused_changed`` when ``is_paused()`` moved off
    // ``was_paused``, and refreshes the cursor and the surface.
    void state_changed(bool was_paused);

    // libvncclient callback shims — forward to the instance via
    // ``rfbClientGetClientData``. The malloc callback returns
    // libvncclient's ``rfbBool`` (signed char) — declared as ``signed
    // char`` here to avoid pulling rfb headers into this hpp.
    static void on_framebuffer_update_static(_rfbClient* c, int x, int y, int w, int h);
    static void on_finished_update_static(_rfbClient* c);
    static signed char on_malloc_framebuffer_static(_rfbClient* c);

    _rfbClient* client_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    QMutex image_mutex_;
    QImage image_;  // protected by image_mutex_
    int fb_width_ = 0;
    int fb_height_ = 0;
    // Active painted region: max ``x+w``/``y+h`` seen in update
    // callbacks. We over-allocate the framebuffer (see
    // on_malloc_framebuffer_static) so paintEvent must crop to this
    // before scaling, otherwise a tiny live region inside a large
    // black canvas gets shrunk to nothing on the wizard panel.
    int content_w_ = 0;
    int content_h_ = 0;

    // ``streaming_`` is a connection being wanted: live, or the wait
    // for a paused start's frame. ``pause_on_frame_`` is that wait; the
    // worker reads it to know its finished updates are to be reported.
    // ``session_`` numbers the connections so a report queued by one
    // that has since been replaced is dropped. The last connect target
    // is kept so a click can reconnect.
    bool preview_ = false;
    bool hovered_ = false;
    bool streaming_ = false;
    std::atomic<bool> pause_on_frame_{false};
    int session_ = 0;
    QString last_host_;
    int last_port_ = -1;

    // A paused start ends once updates go quiet after the first frame
    // (``settle_timer_``), or a while after it (``first_frame_clock_``).
    QTimer settle_timer_;
    QElapsedTimer first_frame_clock_;

    // The paused look of the last frame, made once when pausing rather
    // than on every paint. GUI thread only.
    QImage frozen_;

    // A live view must survive the VM not being up yet, QEMU dropping
    // the connection (share-policy / contention), or the session
    // ending. While ``streaming_`` is wanted we re-attempt the session
    // on this timer instead of giving up. A pause or ``clear()``
    // cancels it. Lives on / used from the GUI thread only; the RFB
    // worker thread marshals a (re)schedule via invokeMethod.
    QTimer retry_timer_;
};

}  // namespace lsw::host::ui
