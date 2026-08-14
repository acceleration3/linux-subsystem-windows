#include "lsw/host/manager/vulkan_mirror.hpp"

#include "lsw/base/log.hpp"
#include "lsw/host/link/agent_rpc_client.hpp"
#include "lsw/host/manager/input_modifiers.hpp"
#include "lsw/host/manager/virtual_key_map.hpp"

#include "lsw/linkproto/commands.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtConcurrent>

#include <nlohmann/json.hpp>

namespace lsw::host::manager {

VulkanMirror::VulkanMirror(link::AgentRpcClient& rpc, const QString& title, QSize size,
                           QWidget* parent)
    : QWidget(parent), rpc_(rpc) {
    setWindowTitle(title);
    // A native window, because a native window id is the whole point:
    // the replay creates its Vulkan surface on it. Without this, Qt may
    // render the widget into its parent's surface and there is nothing
    // to hand over.
    setAttribute(Qt::WA_NativeWindow);
    // Qt must not paint here at all — the GPU is presenting into this
    // window behind Qt's back, and a background fill would flicker over
    // the application every time Qt decided to repaint.
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_PaintOnScreen);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // Not resizable, for the reason the replay's own window never was:
    // the application sizes its swapchain from ITS window, inside the
    // VM, which a drag here does not change — so it would rebuild at
    // the same extent forever.
    setFixedSize(size);
    (void)winId();  // realise it now: the id is wanted before we are shown
}

void VulkanMirror::bind(std::uint64_t hwnd) {
    if (hwnd == 0 || hwnd_ == hwnd) {
        return;
    }
    hwnd_ = hwnd;
    input_.emplace(rpc_, hwnd);
    LSW_INFO("vulkan mirror: bound to guest window {:#x}", hwnd);
}

void VulkanMirror::guest_closed() {
    guest_gone_ = true;
    input_.reset();
    close();
}

QPoint VulkanMirror::to_guest(QPointF widget_position) const {
    return {static_cast<int>(widget_position.x()), static_cast<int>(widget_position.y())};
}

void VulkanMirror::paintEvent(QPaintEvent* event) {
    // Nothing. See the class comment: the pixels are the replay's.
    event->accept();
}

void VulkanMirror::closeEvent(QCloseEvent* e) {
    if (guest_gone_ || !is_bound()) {
        Q_EMIT closed();
        e->accept();
        return;
    }
    // Ask the guest window to close and keep the mirror up until it
    // actually does — an application with an "are you sure?" prompt
    // gets to show it.
    e->ignore();
    const std::uint64_t hwnd = hwnd_;
    (void)QtConcurrent::run([this, hwnd] {
        (void)rpc_.call(nlohmann::json{{"cmd", linkproto::cmd::kCloseWindow},
                                       {"params", {{"hwnd", hwnd}}}});
    });
}

void VulkanMirror::mouseMoveEvent(QMouseEvent* e) {
    if (input_) {
        input_->move(to_guest(e->position()),
                     InputModifiers::of(e->modifiers(), e->buttons()));
    }
    e->accept();
}

void VulkanMirror::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    if (input_) {
        input_->button_down(to_guest(e->position()), InputModifiers::button_index(e->button()),
                            InputModifiers::of(e->modifiers(), e->buttons()));
    }
    e->accept();
}

void VulkanMirror::mouseReleaseEvent(QMouseEvent* e) {
    if (input_) {
        input_->button_up(to_guest(e->position()), InputModifiers::button_index(e->button()),
                          InputModifiers::of(e->modifiers(), e->buttons()));
    }
    e->accept();
}

void VulkanMirror::mouseDoubleClickEvent(QMouseEvent* e) {
    if (input_) {
        input_->double_click(to_guest(e->position()),
                             InputModifiers::button_index(e->button()),
                             InputModifiers::of(e->modifiers(), e->buttons()));
    }
    e->accept();
}

void VulkanMirror::wheelEvent(QWheelEvent* e) {
    if (input_) {
        const QPoint delta = e->angleDelta();
        const bool horizontal = std::abs(delta.x()) > std::abs(delta.y());
        input_->wheel(to_guest(e->position()), horizontal ? delta.x() : delta.y(), horizontal,
                      InputModifiers::of(e->modifiers(), e->buttons()));
    }
    e->accept();
}

void VulkanMirror::keyPressEvent(QKeyEvent* e) {
    if (input_) {
        const unsigned mods = InputModifiers::of(e->modifiers());
        if (VirtualKeyMap::is_text_input(e)) {
            // Text is WM_CHAR only: a key-down as well would have the
            // guest's TranslateMessage synthesise a second one.
            for (const QChar ch : e->text()) {
                if (ch.unicode() != 0) {
                    input_->text(ch.unicode(), mods);
                }
            }
        } else {
            input_->key_down(VirtualKeyMap::virtual_key_for(e->key()), mods);
        }
    }
    e->accept();
}

void VulkanMirror::keyReleaseEvent(QKeyEvent* e) {
    if (input_ && !VirtualKeyMap::is_text_input(e)) {
        input_->key_up(VirtualKeyMap::virtual_key_for(e->key()),
                       InputModifiers::of(e->modifiers()));
    }
    e->accept();
}

}  // namespace lsw::host::manager
