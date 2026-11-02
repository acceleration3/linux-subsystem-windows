#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <thread>

struct _rfbClient;

class QEnterEvent;
class QMouseEvent;
class QPainter;

namespace lsw::host::ui {
class VncView : public QWidget {
    Q_OBJECT
public:
    explicit VncView(QWidget* parent = nullptr);
    ~VncView() override;

    void connect_to(const QString& host, int port);

    void disconnect();

    void set_preview(bool on);

    void clear();

    void play();
    void pause();

    [[nodiscard]] bool is_paused() const;
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
    void open_session(bool pause_on_frame);
    void worker_loop();
    void resize_buffer(int w, int h);
    void frame_arrived(int session);
    void freeze_frame();
    void state_changed(bool was_paused);

    static void on_framebuffer_update_static(_rfbClient* c, int x, int y, int w, int h);
    static void on_finished_update_static(_rfbClient* c);
    static signed char on_malloc_framebuffer_static(_rfbClient* c);

    _rfbClient* client_ = nullptr;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    QMutex image_mutex_;
    QImage image_;
    int fb_width_ = 0;
    int fb_height_ = 0;
    int content_w_ = 0;
    int content_h_ = 0;

    bool preview_ = false;
    bool hovered_ = false;
    bool streaming_ = false;
    std::atomic<bool> pause_on_frame_{false};
    int session_ = 0;
    QString last_host_;
    int last_port_ = -1;

    QTimer settle_timer_;
    QElapsedTimer first_frame_clock_;

    QImage frozen_;

    QTimer retry_timer_;
};

}
