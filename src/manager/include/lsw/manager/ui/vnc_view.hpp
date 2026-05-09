#pragma once

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

class QPushButton;
class QEnterEvent;

namespace lsw::manager::ui {

// Embedded VNC viewer widget. Connects to a host:port via libvncclient,
// runs the RFB message loop on a worker thread, and paints the latest
// framebuffer into the widget on each Qt repaint cycle.
//
// Threading:
//   * The worker thread owns ``client_`` and is the only thread that
//     calls libvncclient functions.
//   * ``image_`` is the framebuffer; libvncclient writes directly into
//     ``image_.bits()`` (we set ``client_->frameBuffer`` to it). Worker
//     and GUI thread coordinate via ``image_mutex_``.
//   * On a framebuffer update the worker queues an ``update()`` on the
//     GUI thread via QMetaObject::invokeMethod.
class VncView : public QWidget {
    Q_OBJECT
public:
    explicit VncView(QWidget* parent = nullptr);
    ~VncView() override;

    // Connect to ``host:port``. Idempotent re-call: tears down any
    // existing session first. Safe to call before ``show()``. The
    // host/port are remembered so ``start()`` can reconnect after a
    // ``stop()``.
    void connect_to(const QString& host, int port);

    // Tear down the session and stop the worker. Called automatically
    // on destruction.
    void disconnect();

    // Preview controls: when enabled, hovering the widget fades the
    // image toward grey and shows a centered Start/Stop button. Off by
    // default — the wizard install view stays a plain surface with no
    // button and no hover effect.
    void set_controls_visible(bool on);

    // Reconnect to the last ``connect_to`` target / tear the session
    // down. ``stop()`` freezes on the last frame. The Start/Stop
    // button toggles between these.
    void start();
    void stop();
    [[nodiscard]] bool is_streaming() const { return streaming_; }

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    void worker_loop();
    void resize_buffer(int w, int h);
    void reposition_button();
    void refresh_button();

    // libvncclient callback shims — forward to the instance via
    // ``rfbClientGetClientData``. The malloc callback returns
    // libvncclient's ``rfbBool`` (signed char) — declared as ``signed
    // char`` here to avoid pulling rfb headers into this hpp.
    static void on_framebuffer_update_static(_rfbClient* c, int x, int y, int w, int h);
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

    // Preview-controls state. ``controls_visible_`` gates the whole
    // feature; ``hovered_`` drives the grey fade + button visibility;
    // ``streaming_`` tracks connected-vs-stopped for the toggle. The
    // last connect target is kept so the button can reconnect.
    bool controls_visible_ = false;
    bool hovered_ = false;
    bool streaming_ = false;
    QString last_host_;
    int last_port_ = -1;
    QPushButton* toggle_btn_ = nullptr;

    // A live preview must survive the VM not being up yet, QEMU
    // dropping the connection (share-policy / contention), or the
    // session ending. While ``streaming_`` is desired we re-attempt
    // ``connect_to`` on this timer instead of giving up. ``stop()``
    // cancels it. Lives on / used from the GUI thread only; the RFB
    // worker thread marshals a (re)schedule via invokeMethod.
    QTimer retry_timer_;
};

}  // namespace lsw::manager::ui
