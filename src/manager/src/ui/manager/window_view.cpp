#include "lsw/manager/ui/manager/window_view.hpp"

#include "lsw/manager/vm/agent_rpc_client.hpp"

#include <QByteArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <QWindow>
#include <QtConcurrent>

#include <cmath>

#include <nlohmann/json.hpp>

namespace lsw::manager::ui::manager {

namespace {

// Shared modifier/button bitmask (mirror of input_inject.hpp).
constexpr unsigned kModCtrl = 1;
constexpr unsigned kModShift = 2;
constexpr unsigned kModAlt = 4;
constexpr unsigned kModLBtn = 8;
constexpr unsigned kModRBtn = 16;
constexpr unsigned kModMBtn = 32;

unsigned mods_of(Qt::KeyboardModifiers km,
                 Qt::MouseButtons mb = Qt::NoButton) {
    unsigned m = 0;
    if (km & Qt::ControlModifier) m |= kModCtrl;
    if (km & Qt::ShiftModifier) m |= kModShift;
    if (km & Qt::AltModifier) m |= kModAlt;
    if (mb & Qt::LeftButton) m |= kModLBtn;
    if (mb & Qt::RightButton) m |= kModRBtn;
    if (mb & Qt::MiddleButton) m |= kModMBtn;
    return m;
}

// 0 = left, 1 = right, 2 = middle (matches input_inject.hpp).
int button_index(Qt::MouseButton b) {
    if (b == Qt::RightButton) return 1;
    if (b == Qt::MiddleButton) return 2;
    return 0;
}

// Qt::Key → Win32 virtual-key code. Win32 VK_* aren't available on
// the Linux host so the constants are inlined. Unmapped keys return
// 0 — the WM_CHAR path (event text) still delivers typed characters.
unsigned vk_for(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<unsigned>('A' + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<unsigned>('0' + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return 0x70u + static_cast<unsigned>(key - Qt::Key_F1);  // VK_F1..
    }
    switch (key) {
    case Qt::Key_Backspace: return 0x08;  // VK_BACK
    case Qt::Key_Tab:       return 0x09;  // VK_TAB
    case Qt::Key_Backtab:   return 0x09;
    case Qt::Key_Return:
    case Qt::Key_Enter:     return 0x0D;  // VK_RETURN
    case Qt::Key_Escape:    return 0x1B;  // VK_ESCAPE
    case Qt::Key_Space:     return 0x20;  // VK_SPACE
    case Qt::Key_PageUp:    return 0x21;  // VK_PRIOR
    case Qt::Key_PageDown:  return 0x22;  // VK_NEXT
    case Qt::Key_End:       return 0x23;  // VK_END
    case Qt::Key_Home:      return 0x24;  // VK_HOME
    case Qt::Key_Left:      return 0x25;  // VK_LEFT
    case Qt::Key_Up:        return 0x26;  // VK_UP
    case Qt::Key_Right:     return 0x27;  // VK_RIGHT
    case Qt::Key_Down:      return 0x28;  // VK_DOWN
    case Qt::Key_Insert:    return 0x2D;  // VK_INSERT
    case Qt::Key_Delete:    return 0x2E;  // VK_DELETE
    case Qt::Key_Shift:     return 0x10;  // VK_SHIFT
    case Qt::Key_Control:   return 0x11;  // VK_CONTROL
    case Qt::Key_Alt:       return 0x12;  // VK_MENU
    case Qt::Key_CapsLock:  return 0x14;  // VK_CAPITAL
    case Qt::Key_Meta:      return 0x5B;  // VK_LWIN
    case Qt::Key_Semicolon: return 0xBA;  // VK_OEM_1
    case Qt::Key_Equal:     return 0xBB;  // VK_OEM_PLUS
    case Qt::Key_Comma:     return 0xBC;  // VK_OEM_COMMA
    case Qt::Key_Minus:     return 0xBD;  // VK_OEM_MINUS
    case Qt::Key_Period:    return 0xBE;  // VK_OEM_PERIOD
    case Qt::Key_Slash:     return 0xBF;  // VK_OEM_2
    case Qt::Key_QuoteLeft: return 0xC0;  // VK_OEM_3
    case Qt::Key_BracketLeft:  return 0xDB;  // VK_OEM_4
    case Qt::Key_Backslash:    return 0xDC;  // VK_OEM_5
    case Qt::Key_BracketRight: return 0xDD;  // VK_OEM_6
    case Qt::Key_Apostrophe:   return 0xDE;  // VK_OEM_7
    default:                return 0;
    }
}

// True when the key event represents typed text we should deliver as
// WM_CHAR (and ONLY as WM_CHAR). Posting WM_KEYDOWN as well would make
// the guest's own message pump synthesise a second WM_CHAR via
// TranslateMessage → every character typed twice. Control chars
// (Enter/Tab/Backspace, text < 0x20) and any Ctrl/Alt/Meta combo are
// NOT text — they go through the VK key_down/up path so shortcuts and
// navigation still work.
bool is_text_input(const QKeyEvent* e) {
    const QString t = e->text();
    if (t.isEmpty() || t[0].unicode() < 0x20) {
        return false;
    }
    return (e->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) == 0;
}

// Fire-and-forget one inject_input frame. ``p`` carries the
// event-specific fields; hwnd is stamped in here.
void post_input(vm::AgentRpcClient& rpc, std::uint64_t hwnd,
                nlohmann::json p) {
    p["hwnd"] = hwnd;
    rpc.post_oneway({{"cmd", "inject_input"}, {"params", std::move(p)}});
}

// Win32 ``WM_NCHITTEST`` return codes the agent reports back. Mirrored
// here so the host doesn't need to drag in winuser.h.
constexpr int kHtClient      = 1;
constexpr int kHtCaption     = 2;
constexpr int kHtSysMenu     = 3;
constexpr int kHtMinButton   = 8;
constexpr int kHtMaxButton   = 9;
constexpr int kHtLeft        = 10;
constexpr int kHtRight       = 11;
constexpr int kHtTop         = 12;
constexpr int kHtTopLeft     = 13;
constexpr int kHtTopRight    = 14;
constexpr int kHtBottom      = 15;
constexpr int kHtBottomLeft  = 16;
constexpr int kHtBottomRight = 17;

// (``edges_for_ht`` removed in Phase 7 with the rest of the
// WM_NCHITTEST path: the host derives resize edges directly from
// widget-edge px zones via ``edges_for_widget_pos``, never from a
// HT code.)

// Synchronous NC hit-test against the guest window. ``ht`` defaults
// to ``kHtClient`` on RPC / parse failure (the safe fallback that lets
// the existing forward-to-guest path run); the rest of the fields are
// diagnostic — only consumed by the debug overlay.
// Synchronous ``WM_NCHITTEST`` against the guest window for the
// given EFB-relative pixel. Used by ``mousePressEvent`` AFTER the
// cached UIA button-rect check misses, to discriminate caption-drag
// (HTCAPTION → host startSystemMove) from a real content click
// (HTCLIENT / anything else → forward to guest). Tabs / address
// bars / toolbars on the caption row correctly return HTCLIENT
// here; only the dead caption strip returns HTCAPTION.
//
// Round-trip cost: one RPC per left-press whose target isn't a
// caption button. The agent's handler is a plain ``SendMessageW`` —
// typically <1 ms on the loopback channel.
int nc_hit_test(vm::AgentRpcClient& rpc, std::uint64_t hwnd, QPoint g) {
    const QByteArray reply = rpc.call(nlohmann::json{
        {"cmd", "nchittest"},
        {"params", {{"hwnd", hwnd}, {"x", g.x()}, {"y", g.y()}}}});
    if (reply.isEmpty()) {
        return kHtClient;
    }
    try {
        const auto j = nlohmann::json::parse(reply.constData(),
                                              reply.constData() + reply.size());
        if (j.is_object()) {
            return j.value("ht", kHtClient);
        }
    } catch (const nlohmann::json::exception&) {
    }
    return kHtClient;
}

// Which window edge(s) a widget-local position sits in. The resize
// zone is the outermost ``border`` px of the widget itself, NOT the
// outside of the captured image — because once the host widget grows
// beyond the guest's max size the captured image letterboxes inside,
// and we don't want the entire letterbox area to act as a resize
// grip. The user grabs the host window's edge to resize.
Qt::Edges edges_for_widget_pos(QPointF pos, int w, int h, int border) {
    if (border <= 0) {
        return {};
    }
    Qt::Edges e = {};
    if (pos.x() < border) {
        e |= Qt::LeftEdge;
    } else if (pos.x() >= w - border) {
        e |= Qt::RightEdge;
    }
    if (pos.y() < border) {
        e |= Qt::TopEdge;
    } else if (pos.y() >= h - border) {
        e |= Qt::BottomEdge;
    }
    return e;
}

// Resize cursor for an Edges set. ``ArrowCursor`` when no edge (caller
// will ``unsetCursor`` rather than painting a stale arrow).
Qt::CursorShape cursor_for_edges(Qt::Edges e) {
    const bool L = (e & Qt::LeftEdge) != 0;
    const bool R = (e & Qt::RightEdge) != 0;
    const bool T = (e & Qt::TopEdge) != 0;
    const bool B = (e & Qt::BottomEdge) != 0;
    if ((T && L) || (B && R)) return Qt::SizeFDiagCursor;
    if ((T && R) || (B && L)) return Qt::SizeBDiagCursor;
    if (L || R) return Qt::SizeHorCursor;
    if (T || B) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

// Toggled by ``LSW_DEBUG_NCHIT=1`` in the environment. Read once on
// first call so flipping it requires a manager restart but every
// per-press paint cost is one branch and a static load.
bool debug_nchit_enabled() {
    static const bool on = [] {
        const QByteArray v = qgetenv("LSW_DEBUG_NCHIT");
        return v == "1" || v.toLower() == "true";
    }();
    return on;
}

// Human-readable name for an ``HT*`` value (overlay label). Returns
// "?" for unknown codes so unfamiliar custom-chrome returns are
// still visible in the corner instead of being silently mapped to
// "CLIENT".
const char* ht_name(int ht) {
    switch (ht) {
    case -2: return "TRANSPARENT";  // HTTRANSPARENT
    case -1: return "ERROR";        // HTERROR
    case 0:  return "NOWHERE";      // HTNOWHERE
    case kHtClient:      return "CLIENT";
    case kHtCaption:     return "CAPTION";
    case kHtSysMenu:     return "SYSMENU";
    case 4:  return "GROWBOX";      // HTGROWBOX
    case 5:  return "MENU";         // HTMENU
    case 6:  return "HSCROLL";      // HTHSCROLL
    case 7:  return "VSCROLL";      // HTVSCROLL
    case kHtMinButton:   return "MINBUTTON";
    case kHtMaxButton:   return "MAXBUTTON";
    case kHtLeft:        return "LEFT";
    case kHtRight:       return "RIGHT";
    case kHtTop:         return "TOP";
    case kHtTopLeft:     return "TOPLEFT";
    case kHtTopRight:    return "TOPRIGHT";
    case kHtBottom:      return "BOTTOM";
    case kHtBottomLeft:  return "BOTTOMLEFT";
    case kHtBottomRight: return "BOTTOMRIGHT";
    case 18: return "BORDER";       // HTBORDER
    case 20: return "CLOSE";        // HTCLOSE
    case 21: return "HELP";         // HTHELP
    default: return "?";
    }
}

}  // namespace

WindowView::WindowView(vm::AgentRpcClient& rpc, std::uint64_t hwnd,
                       QString window_title, WindowView* relative_to,
                       QPoint rel_guest_px)
    : QWidget(nullptr), rpc_(rpc), hwnd_(hwnd), owner_view_(relative_to) {
    setWindowTitle(window_title);
    setAttribute(Qt::WA_DeleteOnClose);

    if (relative_to != nullptr) {
        // Owned popup: menus, combo drop-downs, Find/Settings flyouts,
        // message boxes. Make it a FRAMELESS TOOLTIP-type window whose
        // (transient) parent is the owner mirror. On Wayland Qt maps
        // that to an xdg_popup, which the compositor positions
        // RELATIVE to the parent surface (a plain top-level can't be
        // positioned by the client at all). Show-without-activating so
        // opening a menu doesn't yank host focus off the main mirror.
        // Translucent (no black fill / no opaque-paint) so the area
        // outside the captured menu is transparent, not an ugly black
        // rectangle.
        // We TRIM the captured WinUI shadow margin off the frame (see
        // opaque_bounds / on_frame) so the menu fills the popup; the
        // single outer shadow is then the compositor's — so we do
        // NOT set NoDropShadow here.
        setParent(relative_to, Qt::ToolTip | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TranslucentBackground);
        setMinimumSize(1, 1);  // menus are small; don't force 320×240
        pending_rel_guest_px_ = rel_guest_px;
        has_pending_rel_ = true;
    } else {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setStyleSheet("background-color: black;");
        // Min size is set later from the guest's WM_GETMINMAXINFO; do
        // NOT force 320×240 — a guest window smaller than that would
        // get inflated by the initial Qt layout firing resize_window.
        setMinimumSize(1, 1);
    }

    // Receive keyboard focus and continuous mouse-move (not just
    // while a button is held) so the guest sees hover/move too.
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    resize_debounce_.setSingleShot(true);
    resize_debounce_.setInterval(150);
    connect(&resize_debounce_, &QTimer::timeout, this, &WindowView::flush_resize);

    move_coalesce_.setSingleShot(true);
    move_coalesce_.setInterval(8);  // ~120 Hz cap on forwarded moves
    connect(&move_coalesce_, &QTimer::timeout, this, &WindowView::flush_move);

    connect(&rpc_, &vm::AgentRpcClient::frame_received, this, &WindowView::on_frame);
    connect(&rpc_, &vm::AgentRpcClient::window_gone, this, &WindowView::on_window_gone);
    rpc_.subscribe_window(hwnd_);

    // Caption-drag detector. The agent fires ``caption_drag_started``
    // the instant DefWindowProc enters its SC_MOVE modal loop on the
    // guest (and cancels the loop with WM_CANCELMODE in the same
    // breath, so the guest never visibly moves). On a match we mark
    // the current press stolen and start a host-side system move —
    // Windows decided "this is a drag"; the host carries it out.
    drag_event_token_ = rpc_.on_event(
        QStringLiteral("caption_drag_started"),
        [this](const nlohmann::json& j) {
            const auto h = j.value("hwnd", std::uint64_t{0});
            if (h != hwnd_) return;
            QMetaObject::invokeMethod(this, [this] {
                if (QWindow* wh = windowHandle()) {
                    wh->startSystemMove();
                }
                // Suppress the matching release so the guest doesn't
                // see an orphan WM_LBUTTONUP after the agent's
                // WM_CANCELMODE already tore its modal state down.
                stolen_press_buttons_ |= 1u;  // bit 0 = left button
            }, Qt::QueuedConnection);
        });

    if (relative_to != nullptr) {
        // Popup: no separate props phase; size/place is image-driven.
        props_ready_ = true;
        return;
    }
    // Main mirror: fetch the guest window's actual size + buttons +
    // min/max BEFORE showing, so we open at the right dimensions with
    // the right chrome and DON'T let Qt's default geometry trigger a
    // resize_window RPC that shrinks the guest. show() happens in the
    // apply callback once we have the answer.
    const std::uint64_t qhwnd = hwnd_;
    auto* w = new QFutureWatcher<QByteArray>(this);
    connect(w, &QFutureWatcher<QByteArray>::finished, this, [this, w] {
        const QByteArray reply = w->result();
        w->deleteLater();
        try {
            const auto j = nlohmann::json::parse(reply.constData(),
                                                  reply.constData()
                                                      + reply.size());
            if (j.is_object() && j.value("valid", false)) {
                // Frameless: the user wants the captured guest chrome
                // to be the ONLY chrome — no double title bar. We do
                // our own caption-drag (HTCAPTION → startSystemMove)
                // and edge-resize (halo → startSystemResize) below.
                // ``Qt::Window`` keeps it a top-level so it gets a
                // taskbar entry / Alt-Tab slot.
                setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
                const bool resizable = j.value("resizable", true);
                // 7 px ≈ Win11's invisible resize border. Wide enough
                // to grab reliably, narrow enough not to eat content.
                constexpr int kResizeBorderPx = 7;
                resize_border_ = resizable ? kResizeBorderPx : 0;
                const int gw = j.value("w", 800);
                const int gh = j.value("h", 600);
                const int minw = j.value("min_w", 0);
                const int minh = j.value("min_h", 0);
                if (!resizable) {
                    setFixedSize(gw, gh);
                } else {
                    if (minw > 0 && minh > 0) {
                        setMinimumSize(minw, minh);
                    }
                    // Deliberately NO setMaximumSize: the user can
                    // grow the host mirror past the guest's max track
                    // size — flush_resize clamps the value sent to the
                    // guest at the guest's screen resolution, and the
                    // captured image letterboxes inside the larger
                    // widget via compute_frame_rect's aspect-fit.
                    sizing_to_frame_ = true;
                    resize(gw + 2 * resize_border_,
                           gh + 2 * resize_border_);
                }
                auto rect_of = [&j](const char* key) {
                    QRect r;
                    if (auto it = j.find(key);
                        it != j.end() && it->is_array() && it->size() == 4) {
                        const int rx = it->at(0).get<int>();
                        const int ry = it->at(1).get<int>();
                        const int rw = it->at(2).get<int>();
                        const int rh = it->at(3).get<int>();
                        if (rw > 0 && rh > 0) {
                            r = QRect(rx, ry, rw, rh);
                        }
                    }
                    return r;
                };
                btn_min_rect_    = rect_of("btn_min");
                btn_max_rect_    = rect_of("btn_max");
                btn_close_rect_  = rect_of("btn_close");
                btn_help_rect_   = rect_of("btn_help");
                // title-bar rect is no longer requested from
                // window_props; caption-drag is decided per-press by
                // the ``nchittest`` RPC below.
                screen_w_ = j.value("screen_w", 0);
                screen_h_ = j.value("screen_h", 0);
                // EFB width that the cached btn rects correspond to.
                // on_frame shifts the rects by any delta against this
                // each time a new frame arrives at a different size.
                last_image_w_for_btns_ = gw;
            }
        } catch (const nlohmann::json::exception&) {
        }
        props_ready_ = true;
        show();
    });
    w->setFuture(QtConcurrent::run([this, qhwnd] {
        return rpc_.call(nlohmann::json{
            {"cmd", "window_props"}, {"params", {{"hwnd", qhwnd}}}});
    }));
}

void WindowView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (sizing_to_frame_) {
        // We sized the widget to match a guest-reported frame — don't
        // bounce that back as a resize_window command.
        sizing_to_frame_ = false;
        return;
    }
    if (!props_ready_) {
        // Initial Qt show layout for a main mirror, before window_props
        // has applied. Any resize_window here would shrink the guest
        // to a random Qt default rectangle — the user's "host window
        // is opening with a random size and resizing the guest" bug.
        return;
    }
    if (is_popup()) {
        // Never drive resize_window for a menu/flyout. Its size is the
        // guest's; echoing the widget size back shrank it on every
        // layout pass (feedback) — that was the "menus a bit smaller".
        return;
    }
    // Coalesce the storm of resizeEvents during an interactive drag;
    // only the size the user settles on is pushed to the guest.
    resize_debounce_.start();
}

QRect WindowView::compute_frame_rect() const {
    if (image_.isNull() || width() <= 0 || height() <= 0) {
        return rect();
    }
    const QSize src =
        content_rect_.isEmpty() ? image_.size() : content_rect_.size();
    const QSize tgt = src.scaled(size(), Qt::KeepAspectRatio);
    return QRect((width() - tgt.width()) / 2, (height() - tgt.height()) / 2,
                 tgt.width(), tgt.height());
}

QRect WindowView::opaque_bounds(const QImage& img) {
    if (img.isNull()) {
        return {};
    }
    const QImage a = img.format() == QImage::Format_ARGB32
                         ? img
                         : img.convertToFormat(QImage::Format_ARGB32);
    int minx = a.width();
    int miny = a.height();
    int maxx = -1;
    int maxy = -1;
    // Alpha >= 160 = the solid menu body; the WinUI shadow ramps far
    // below that, so it gets trimmed.
    constexpr int kSolid = 160;
    for (int y = 0; y < a.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(a.scanLine(y));
        for (int x = 0; x < a.width(); ++x) {
            if (qAlpha(row[x]) >= kSolid) {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
        }
    }
    if (maxx < minx || maxy < miny) {
        return {};  // nothing solid — caller uses the full frame
    }
    const QRect r(minx, miny, maxx - minx + 1, maxy - miny + 1);
    // Degenerate guard: if "solid" is almost the whole frame there was
    // no real shadow margin to trim — keep the full frame.
    if (r.width() >= a.width() - 2 && r.height() >= a.height() - 2) {
        return {};
    }
    return r;
}

QPoint WindowView::map_to_guest(QPointF wp) const {
    const QRect fr = compute_frame_rect();
    if (image_.isNull() || fr.width() <= 0 || fr.height() <= 0) {
        return {0, 0};
    }
    // The drawn source is content_rect_ (popup, shadow trimmed) or the
    // whole frame. Guest client coords are in the FULL frame's space,
    // so add the trimmed origin back.
    const QRect src = content_rect_.isEmpty() ? image_.rect() : content_rect_;
    const double fx = (wp.x() - fr.x()) / double(fr.width());
    const double fy = (wp.y() - fr.y()) / double(fr.height());
    const int gx = std::clamp(
        src.x() + static_cast<int>(std::lround(fx * src.width())), 0,
        image_.width() - 1);
    const int gy = std::clamp(
        src.y() + static_cast<int>(std::lround(fy * src.height())), 0,
        image_.height() - 1);
    return {gx, gy};
}

void WindowView::flush_resize() {
    // Ask the guest to make its window the host widget size so the
    // mirror is ~1:1, BUT clamp to the guest's screen — the user may
    // have dragged the host past the guest's desktop. compute_frame_rect
    // letterboxes the captured frame inside the larger widget when
    // that happens.
    int w = width();
    int h = height();
    if (screen_w_ > 0) w = std::min(w, screen_w_);
    if (screen_h_ > 0) h = std::min(h, screen_h_);
    const std::uint64_t hwnd = hwnd_;
    (void)QtConcurrent::run([this, hwnd, w, h] {
        nlohmann::json req = {
            {"cmd", "resize_window"},
            {"params", {{"hwnd", hwnd}, {"width", w}, {"height", h}}},
        };
        (void)rpc_.call(req);
    });
}

void WindowView::flush_move() {
    if (!move_pending_) {
        return;
    }
    move_pending_ = false;
    post_input(rpc_, hwnd_,
               {{"type", "move"}, {"x", move_x_}, {"y", move_y_},
                {"mods", move_mods_}});
}

void WindowView::mouseMoveEvent(QMouseEvent* e) {
    // Resize-halo hover: paint the right Qt::Size*Cursor so the user
    // gets the standard visual cue at the window edge. Outside the
    // halo we drop back to whatever the parent default is (arrow).
    // Popups have resize_border_ == 0, so this is a no-op there.
    const Qt::Edges hover_edges = edges_for_widget_pos(
        e->position(), width(), height(), resize_border_);
    if (hover_edges != 0) {
        setCursor(cursor_for_edges(hover_edges));
    } else if (testAttribute(Qt::WA_SetCursor)) {
        unsetCursor();
    }
    const QPoint g = map_to_guest(e->position());
    move_x_ = g.x();
    move_y_ = g.y();
    move_mods_ = mods_of(e->modifiers(), e->buttons());
    move_pending_ = true;
    if (!move_coalesce_.isActive()) {
        move_coalesce_.start();
    }
    e->accept();
}

void WindowView::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    flush_move();  // keep ordering: any pending move lands before the click
    const QPoint g = map_to_guest(e->position());
    // Captured frame = full DWM extended frame (title bar included).
    // A left-press that lands on the guest's caption / system buttons
    // / resize edges should drive the *host* mirror, not the guest —
    // otherwise dragging the captured title bar moves the guest window
    // and minimize/maximize toggle the guest off-screen. Popups don't
    // have NC chrome to worry about; skip the round trip for them.
    if (!is_popup() && e->button() == Qt::LeftButton) {
        QWindow* wh = windowHandle();
        // 0. Resize halo — highest priority so a press in the outer
        //    border zone always initiates a system resize, even if
        //    the same widget position would otherwise clamp into a
        //    button rect's screen area after map_to_guest.
        if (const Qt::Edges edges = edges_for_widget_pos(
                e->position(), width(), height(), resize_border_);
            edges != 0) {
            bool started = false;
            if (wh != nullptr) {
                started = wh->startSystemResize(edges);
            }
            if (started) {
                last_hit_widget_ = e->position();
                last_hit_guest_ = g;
                last_ht_ = 17;  // HTBOTTOMRIGHT, just for the overlay label
                if (debug_nchit_enabled()) {
                    update();
                }
                stolen_press_buttons_ |= (1u << button_index(e->button()));
                e->accept();
                return;
            }
        }
        // 1. Check the OS-frame button rects locally. UIA-sourced
        //    (Phase 7): the agent's ``window_props`` reply now
        //    includes the title bar's bounding rect and each Button
        //    child rect. Authoritative for the system caption
        //    buttons regardless of how the app's own wndproc treats
        //    WM_NCHITTEST.
        bool stolen = false;
        int synthetic_ht = -1;
        if (btn_min_rect_.contains(g)) {
            showMinimized();
            stolen = true;
            synthetic_ht = kHtMinButton;
        } else if (btn_max_rect_.contains(g)) {
            if (isMaximized()) {
                showNormal();
            } else {
                showMaximized();
            }
            stolen = true;
            synthetic_ht = kHtMaxButton;
        } else if (btn_close_rect_.contains(g)) {
            // Defer to the existing close path (closeEvent posts
            // WM_CLOSE so the app can prompt for unsaved changes).
            close();
            stolen = true;
            synthetic_ht = 20;  // HTCLOSE
        }
        // No host-side hit-test: non-button presses just go to the
        // guest. If Windows decides the press is a caption-drag
        // (DefWindowProc enters its SC_MOVE modal loop), the agent's
        // ``EVENT_SYSTEM_MOVESIZESTART`` hook catches that instantly,
        // cancels the guest move, and pushes ``caption_drag_started``
        // — handled in the constructor lambda, which calls
        // ``startSystemMove`` on this mirror's window.
        if (stolen) {
            last_ht_ = synthetic_ht;
            last_hit_widget_ = e->position();
            last_hit_guest_ = g;
            if (debug_nchit_enabled()) {
                update();
            }
            stolen_press_buttons_ |= (1u << button_index(e->button()));
            e->accept();
            return;
        }
    }
    post_input(rpc_, hwnd_,
               {{"type", "down"}, {"x", g.x()}, {"y", g.y()},
                {"button", button_index(e->button())},
                {"mods", mods_of(e->modifiers(), e->buttons())}});
    e->accept();
}

void WindowView::mouseReleaseEvent(QMouseEvent* e) {
    const unsigned bit = 1u << button_index(e->button());
    if ((stolen_press_buttons_ & bit) != 0) {
        // The matching press was rerouted to a host-side window op
        // (system move/resize/minimize/maximize). The guest never saw
        // the down, so forwarding the up would be a phantom release —
        // swallow it.
        stolen_press_buttons_ &= ~bit;
        e->accept();
        return;
    }
    flush_move();
    const QPoint g = map_to_guest(e->position());
    post_input(rpc_, hwnd_,
               {{"type", "up"}, {"x", g.x()}, {"y", g.y()},
                {"button", button_index(e->button())},
                {"mods", mods_of(e->modifiers(), e->buttons())}});
    e->accept();
}

void WindowView::mouseDoubleClickEvent(QMouseEvent* e) {
    flush_move();
    // Qt delivers press, release, then THIS in place of the 2nd
    // press; WM_*BUTTONDBLCLK is exactly that 2nd-down. A trailing
    // release follows from Qt as a normal mouseReleaseEvent.
    const QPoint g = map_to_guest(e->position());
    // Double-click on the captured caption is the standard "toggle
    // maximize" gesture; reroute it the same way the single-press
    // path reroutes caption interactions. The preceding press+release
    // were already absorbed by the steal logic, so we don't need to
    // suppress anything extra here — but the trailing single release
    // for this second-down also lands as a stolen release.
    if (!is_popup() && e->button() == Qt::LeftButton) {
        // Local-rect check first (same reasoning as mousePressEvent).
        // The max button rect already toggles via the single-press
        // path, so a double-click on it just runs the toggle twice
        // (net no-op). We still need to swallow the dbl-click so the
        // guest doesn't see a phantom WM_LBUTTONDBLCLK.
        if (btn_min_rect_.contains(g) || btn_max_rect_.contains(g)
            || btn_close_rect_.contains(g)) {
            stolen_press_buttons_ |= (1u << button_index(e->button()));
            e->accept();
            return;
        }
        // Caption dbl-click → toggle maximize. Local rect check via
        // the cached UIA title-bar rect; no RPC.
        // Caption dbl-click also flows through the guest now (no
        // host-side hit-test). Windows enters its modal move loop on
        // the first single-press; ``caption_drag_started`` handler
        // already started a host system-move, so any second-down
        // (the dbl-click event) will arrive at the guest as a
        // WM_LBUTTONDBLCLK but won't restart a move because the
        // first one was cancelled. Toggle-maximize via dbl-click can
        // be added later by listening for a "drag never moved" event
        // — punted for now in favour of the simpler path.
    }
    post_input(rpc_, hwnd_,
               {{"type", "dblclick"}, {"x", g.x()}, {"y", g.y()},
                {"button", button_index(e->button())},
                {"mods", mods_of(e->modifiers(), e->buttons())}});
    e->accept();
}

void WindowView::wheelEvent(QWheelEvent* e) {
    flush_move();
    const QPoint g = map_to_guest(e->position());
    const int gx = g.x();
    const int gy = g.y();
    const QPoint ad = e->angleDelta();
    const unsigned m = mods_of(e->modifiers(), e->buttons());
    if (ad.y() != 0) {
        post_input(rpc_, hwnd_,
                   {{"type", "wheel"}, {"x", gx}, {"y", gy},
                    {"wheel", ad.y()}, {"hwheel", false}, {"mods", m}});
    }
    if (ad.x() != 0) {
        post_input(rpc_, hwnd_,
                   {{"type", "wheel"}, {"x", gx}, {"y", gy},
                    {"wheel", ad.x()}, {"hwheel", true}, {"mods", m}});
    }
    e->accept();
}

void WindowView::keyPressEvent(QKeyEvent* e) {
    const unsigned m = mods_of(e->modifiers());
    if (is_text_input(e)) {
        // Text: WM_CHAR ONLY (no WM_KEYDOWN — see is_text_input; that
        // is what caused every character to be typed twice).
        for (const QChar ch : e->text()) {
            const unsigned cp = ch.unicode();
            if (cp != 0) {
                post_input(rpc_, hwnd_,
                           {{"type", "char"}, {"cp", cp}, {"mods", m}});
            }
        }
    } else {
        // Navigation / shortcut / control key: VK down. The guest's
        // own TranslateMessage turns this into the right control
        // WM_CHAR (newline, backspace, …) so we must NOT also send one.
        post_input(rpc_, hwnd_,
                   {{"type", "key_down"}, {"vk", vk_for(e->key())},
                    {"mods", m}});
    }
    e->accept();
}

void WindowView::keyReleaseEvent(QKeyEvent* e) {
    // Text keys were char-only (no key_down), so no key_up either —
    // sending one would be a phantom release the guest never saw a
    // press for.
    if (!is_text_input(e)) {
        post_input(rpc_, hwnd_,
                   {{"type", "key_up"}, {"vk", vk_for(e->key())},
                    {"mods", mods_of(e->modifiers())}});
    }
    e->accept();
}

WindowView::~WindowView() {
    if (drag_event_token_ != 0) {
        rpc_.off_event(drag_event_token_);
        drag_event_token_ = 0;
    }
    rpc_.unsubscribe_window(hwnd_);
}

void WindowView::guest_closed() {
    // The guest window is verifiably gone — allow the real teardown
    // through closeEvent and close for good.
    guest_gone_ = true;
    close();
}

void WindowView::closeEvent(QCloseEvent* e) {
    if (guest_gone_) {
        // Real teardown (guest window vanished / app exited).
        rpc_.unsubscribe_window(hwnd_);
        QWidget::closeEvent(e);
        return;
    }
    // User clicked the mirror's X. Don't destroy the mirror yet —
    // just ASK the guest window to close (WM_CLOSE). The mirror stays
    // up; if the app shows a save-changes prompt that prompt becomes
    // its own HWND/mirror. The mirror only really goes away once the
    // guest window actually disappears (window_gone / watcher stale →
    // guest_closed()). If the app refuses to close, so does the
    // mirror — which is the intent.
    e->ignore();
    const std::uint64_t hwnd = hwnd_;
    (void)QtConcurrent::run([this, hwnd] {
        nlohmann::json req = {{"cmd", "close_window"}, {"params", {{"hwnd", hwnd}}}};
        (void)rpc_.call(req);
    });
}

void WindowView::on_frame(std::uint64_t hwnd, QImage image) {
    if (hwnd != hwnd_) {
        return;
    }
    image_ = std::move(image);

    // Keep cached caption-button rects aligned with the visible buttons
    // across guest resizes. The buttons are top-right anchored, so the
    // x of each rect shifts by the same delta as the EFB width — y,
    // width and height are unchanged. Skipped for popups (no buttons)
    // and until window_props has populated last_image_w_for_btns_.
    if (!is_popup() && !image_.isNull() && last_image_w_for_btns_ > 0
        && image_.width() != last_image_w_for_btns_) {
        const int dx = image_.width() - last_image_w_for_btns_;
        auto shift = [dx](QRect& r) {
            if (!r.isEmpty()) {
                r.translate(dx, 0);
            }
        };
        shift(btn_min_rect_);
        shift(btn_max_rect_);
        shift(btn_close_rect_);
        shift(btn_help_rect_);
        last_image_w_for_btns_ = image_.width();
    }

    if (is_popup() && !image_.isNull()) {
        // Trim the WinUI shadow's transparent margin so the menu fills
        // the popup (the single outer shadow is the compositor's).
        // ``opaque_bounds`` is EMPTY for classic Win32 (#32768) menus
        // — they have no in-window shadow margin — in which case we
        // use the full frame. Always keep the widget sized to the
        // source and anchored; don't gate on the trim changing (that
        // skipped sizing entirely for classic menus → mis-placed).
        content_rect_ = opaque_bounds(image_);
        const QSize want =
            content_rect_.isEmpty() ? image_.size() : content_rect_.size();
        if (want != size()) {
            sizing_to_frame_ = true;
            resize(want);
        }
        if (anchor_set_) {
            move(popup_anchor_ + content_rect_.topLeft());
        }
        first_frame_ = false;
    } else if (first_frame_ && !image_.isNull()) {
        // Main mirror: open at the guest client size (plain logical
        // px). Flag the programmatic resize so resizeEvent doesn't
        // echo it back as a resize_window RPC.
        first_frame_ = false;
        sizing_to_frame_ = true;
        resize(image_.size());
    }
    update();
}

void WindowView::on_window_gone(std::uint64_t hwnd) {
    if (hwnd != hwnd_) {
        return;
    }
    guest_closed();  // guest dropped the window — real teardown
}

void WindowView::paintEvent(QPaintEvent* /*e*/) {
    QPainter p{this};
    if (owner_view_.isNull()) {
        // Main mirror: opaque, black during any transient aspect
        // mismatch while the guest catches up to a resize.
        p.fillRect(rect(), Qt::black);
    } else {
        // Popup: translucent — clear to transparent so anything
        // outside the captured menu doesn't show as a black box.
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    if (image_.isNull()) {
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRect srcr = content_rect_.isEmpty() ? image_.rect() : content_rect_;
    p.drawImage(compute_frame_rect(), image_, srcr);

    if (debug_nchit_enabled() && !is_popup()) {
        // Paint the OS-frame caption button rects (from WM_GETTITLEBARINFOEX)
        // OVER the captured frame so a mismatch between them and the
        // VISIBLE buttons is immediately obvious. Each rect is mapped
        // from guest-px to widget-px via the same frame transform the
        // press handler uses.
        const QRect fr = compute_frame_rect();
        auto guest_rect_to_widget = [&](const QRect& gr) {
            if (gr.isEmpty() || image_.width() <= 0 || image_.height() <= 0) {
                return QRect();
            }
            const double sx = double(fr.width()) / image_.width();
            const double sy = double(fr.height()) / image_.height();
            return QRect(
                fr.x() + static_cast<int>(std::lround(gr.x() * sx)),
                fr.y() + static_cast<int>(std::lround(gr.y() * sy)),
                static_cast<int>(std::lround(gr.width() * sx)),
                static_cast<int>(std::lround(gr.height() * sy)));
        };
        p.setRenderHint(QPainter::Antialiasing, true);
        // (Title-bar outline removed — there's no cached title-bar
        // rect anymore; caption-drag is resolved per-press via
        // ``nchittest`` and shows up in ``last_ht_`` if you want it
        // surfaced in the overlay text.)
        struct BtnDraw { const QRect& src; QColor color; };
        const BtnDraw btns[] = {
            {btn_min_rect_,   QColor(0,   200, 255, 220)},  // cyan
            {btn_max_rect_,   QColor(0,   255, 100, 220)},  // green
            {btn_close_rect_, QColor(255, 80,  80,  220)},  // red
            {btn_help_rect_,  QColor(255, 200, 0,   220)},  // amber
        };
        for (const auto& b : btns) {
            const QRect r = guest_rect_to_widget(b.src);
            if (r.isEmpty()) continue;
            p.setPen(QPen(b.color, 2));
            p.setBrush(QColor(b.color.red(), b.color.green(), b.color.blue(),
                              48));
            p.drawRect(r);
        }

        if (last_ht_ < 0) {
            return;
        }
        // The dot sits at the press position in WIDGET LOGICAL coords
        // (Qt's e->position()). The label dumps every number that
        // feeds map_to_guest so a drift between visual button and HT
        // result is attributable.
        p.setPen(QPen(Qt::yellow, 2));
        p.setBrush(QColor(255, 0, 0, 220));
        p.drawEllipse(last_hit_widget_, 6.0, 6.0);

        const double dpr = devicePixelRatioF();
        const QString l1 = QStringLiteral("NCHIT %1 (%2)  guest=%3,%4")
                               .arg(ht_name(last_ht_))
                               .arg(last_ht_)
                               .arg(last_hit_guest_.x())
                               .arg(last_hit_guest_.y());
        const QString l2 = QStringLiteral("click=%1,%2  widget=%3x%4  dpr=%5")
                               .arg(QString::number(last_hit_widget_.x(), 'f', 1))
                               .arg(QString::number(last_hit_widget_.y(), 'f', 1))
                               .arg(width())
                               .arg(height())
                               .arg(QString::number(dpr, 'f', 2));
        const QString l3 = QStringLiteral("image=%1x%2  frame=%3,%4 %5x%6")
                               .arg(image_.width())
                               .arg(image_.height())
                               .arg(fr.x()).arg(fr.y())
                               .arg(fr.width()).arg(fr.height());
        const QString l4 = QStringLiteral("efb=%1,%2 %3x%4  win=%5,%6 %7x%8")
                               .arg(last_efb_.x()).arg(last_efb_.y())
                               .arg(last_efb_.width()).arg(last_efb_.height())
                               .arg(last_win_.x()).arg(last_win_.y())
                               .arg(last_win_.width()).arg(last_win_.height());
        const QString l5 = QStringLiteral("screen=%1,%2  dpi=%3")
                               .arg(last_screen_.x()).arg(last_screen_.y())
                               .arg(last_dpi_);
        const QStringList lines = {l1, l2, l3, l4, l5};
        QFont f = p.font();
        f.setPointSize(9);
        f.setBold(true);
        p.setFont(f);
        const QFontMetrics fm(f);
        const int pad = 4;
        const int line_h = fm.lineSpacing();
        int w_max = 0;
        for (const QString& l : lines) {
            w_max = std::max(w_max, fm.horizontalAdvance(l));
        }
        const QRect bg(2, 2, w_max + pad * 2,
                       line_h * lines.size() + pad * 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawRect(bg);
        p.setPen(Qt::yellow);
        for (int i = 0; i < lines.size(); ++i) {
            p.drawText(QRect(bg.x() + pad, bg.y() + pad + line_h * i,
                              w_max, line_h),
                       Qt::AlignLeft | Qt::AlignVCenter, lines[i]);
        }
    }
}

void WindowView::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::ActivationChange && !is_popup() && !guest_gone_) {
        // Mirror host activation onto guest foreground state. Popups
        // don't activate so this is a main-mirror concern. Skipped
        // during teardown — closing the mirror posts WM_CLOSE which
        // will let Windows pick the next foreground naturally.
        const std::uint64_t hwnd = hwnd_;
        const bool active = isActiveWindow();
        (void)QtConcurrent::run([this, hwnd, active] {
            nlohmann::json req = {
                {"cmd", active ? "focus_window" : "unfocus_window"},
                {"params", {{"hwnd", hwnd}}}};
            (void)rpc_.call(req);
        });
    }
    if (e->type() == QEvent::WindowStateChange && !is_popup()
        && (windowState() & Qt::WindowMinimized)) {
        // Minimizing the main window dismisses its menus/popups: ask
        // each guest popup to close, then tear down its mirror so we
        // don't leave orphaned floating menus behind the icon.
        const auto popups = findChildren<WindowView*>(
            QString{}, Qt::FindDirectChildrenOnly);
        for (WindowView* pv : popups) {
            if (pv == nullptr) {
                continue;
            }
            const std::uint64_t ph = pv->hwnd_;
            (void)QtConcurrent::run([this, ph] {
                nlohmann::json req = {{"cmd", "close_window"},
                                      {"params", {{"hwnd", ph}}}};
                (void)rpc_.call(req);
            });
            pv->guest_closed();
        }
    }
}

void WindowView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    if (!has_pending_rel_ || owner_view_.isNull()) {
        return;
    }
    has_pending_rel_ = false;

    // Force both native windows to exist so the transient-parent link
    // is set BEFORE the surface is mapped — otherwise Qt's Wayland
    // backend makes a plain toplevel (which the compositor places
    // arbitrarily → "wrong place") instead of an xdg_popup.
    owner_view_->createWinId();
    createWinId();
    if (QWindow* ow = owner_view_->windowHandle()) {
        if (QWindow* me = windowHandle()) {
            me->setTransientParent(ow);
        }
    }

    // The guest-relative offset is measured from the owner's guest
    // CLIENT origin, which on the host is the owner's content (0,0) —
    // i.e. mapToGlobal({0,0}), NOT pos() (that's the framed-window
    // top-left, off by the title-bar/border, which shifted every
    // popup down-right). Scale the offset by the owner's current
    // display scale so it stays correct even if the owner isn't 1:1.
    double sx = 1.0;
    double sy = 1.0;
    if (!owner_view_->image_.isNull()) {
        const QRect ofr = owner_view_->compute_frame_rect();
        if (owner_view_->image_.width() > 0 && ofr.width() > 0) {
            sx = double(ofr.width()) / owner_view_->image_.width();
        }
        if (owner_view_->image_.height() > 0 && ofr.height() > 0) {
            sy = double(ofr.height()) / owner_view_->image_.height();
        }
    }
    const QPoint origin = owner_view_->mapToGlobal(QPoint(0, 0));
    // Anchor = where the FULL captured frame's (0,0) would sit. The
    // visible menu is inset by content_rect_.topLeft() within that
    // frame, so on_frame offsets the final position by it once the
    // trim is known; place at the anchor for now.
    popup_anchor_ =
        origin
        + QPoint(static_cast<int>(std::lround(pending_rel_guest_px_.x() * sx)),
                 static_cast<int>(std::lround(pending_rel_guest_px_.y() * sy)));
    anchor_set_ = true;
    move(popup_anchor_ + content_rect_.topLeft());
}

}  // namespace lsw::manager::ui::manager
