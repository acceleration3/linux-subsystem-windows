#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QButtonGroup;

namespace lsw::host::manager {

// The manager's left rail: what the machine is, what it is doing, where
// you are in the application, and the one control that turns it on and
// off.
//
// It replaces a row of four buttons across the top of the panel. That
// row said nothing about where you were, and two of its buttons were
// always disabled because Start and Stop take turns being meaningless.
// A rail marks the current page, carries the counts that would
// otherwise need a visit to notice, and leaves a single power button
// whose label says what pressing it does.
class NavRail : public QWidget {
    Q_OBJECT
public:
    // Order is the order they appear. ``Count`` is not a page.
    enum class Page { Overview, Console, Log, Render, Settings, Count };

    explicit NavRail(QWidget* parent = nullptr);

    void set_identity(const QString& name, const QString& detail);
    void set_current(Page page);
    [[nodiscard]] Page current() const noexcept { return current_; }

    // ONLINE means the agent answers, which is not the same as libvirt
    // reporting the domain as running: a guest can be booting for a
    // minute before it is interactive, and the rail should not claim
    // otherwise. The power button tracks the domain, because that is
    // the qemu process it manages.
    void set_agent_online(bool online);
    void set_running(bool running);

    // Counts and marks that make a page worth visiting. Zero or false
    // hides the marker rather than showing an empty one.
    void set_log_count(int records);
    void set_pending_changes(int count);
    void set_render_warning(bool warned);

    // The pages that need a guest to have been installed at all. With
    // none, they are shown but not selectable, which says more than
    // hiding them would: the application has those parts, this machine
    // has not got there yet.
    void set_guest_provisioned(bool provisioned);

Q_SIGNALS:
    void page_selected(Page page);
    void start_requested();
    void stop_requested();

private:
    void set_marker(Page page, const QString& text, bool amber);

    QLabel* name_label_ = nullptr;
    QLabel* detail_label_ = nullptr;
    QLabel* badge_ = nullptr;
    QPushButton* power_ = nullptr;
    QButtonGroup* group_ = nullptr;
    QPushButton* items_[static_cast<int>(Page::Count)]{};
    QLabel* markers_[static_cast<int>(Page::Count)]{};
    Page current_ = Page::Overview;
    bool running_ = false;
};

}  // namespace lsw::host::manager
