#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstdint>
#include <optional>

class QWidget;

namespace lsw::host::manager {

// What each mirror received, and what each mirror actually looks like.
//
// Exists because when a mirror looks wrong the question is always the
// same — is the FRAME wrong, or is what the mirror did with it wrong? —
// and neither side answers it alone. ``dump_frame`` writes both out as
// PNGs: the frame as the guest sent it, and the widget as it renders,
// bars, holes and all.
//
// Lives in the GUI thread. Frames are recorded from there, and the MCP
// tool that reads them runs on an HTTP worker: the QImage snapshots are
// mutex-guarded (cheap — QImage is implicitly shared), while rendering a
// widget has to happen on the thread that owns it, so ``grab_widget``
// bounces through the event loop and blocks until it comes back.
class FrameTap : public QObject {
    Q_OBJECT
public:
    struct Entry {
        QImage image;         // exactly what the agent committed
        QString title;
        QSize widget;         // the mirror widget's size
        QRect frame_rect;     // where the frame is drawn inside it
        QRect content_rect;   // the sub-rect of the image being shown
        bool popup = false;
        std::uint64_t owner = 0;
    };

    // First touched from the GUI thread (the manager builds its MCP
    // server there at startup), so the singleton has GUI-thread affinity
    // and ``grab_widget`` has an event loop to post to.
    static FrameTap& instance();

    // GUI thread only.
    void attach(std::uint64_t hwnd, QWidget* mirror);
    void detach(std::uint64_t hwnd);
    void record(std::uint64_t hwnd, Entry entry);

    // Any thread.
    [[nodiscard]] QList<std::uint64_t> hwnds() const;
    [[nodiscard]] std::optional<Entry> get(std::uint64_t hwnd) const;

    // Render the live mirror widget — what is on screen, including the
    // letterbox bars and anything transparent. Null if that mirror is
    // gone. Blocks until the GUI thread has drawn it.
    [[nodiscard]] QImage grab_widget(std::uint64_t hwnd);

private:
    mutable QMutex mu_;
    QHash<std::uint64_t, Entry> entries_;
    QHash<std::uint64_t, QPointer<QWidget>> widgets_;  // GUI thread only
};

}  // namespace lsw::host::manager
