#pragma once

#include <QWidget>

#include <array>

class QLabel;
class QPushButton;
class QButtonGroup;

namespace lsw::host::manager {
class NavRail : public QWidget {
    Q_OBJECT
public:
    enum class Page {
        Overview,
        Display,
        Graphics,
        Files,
        Usb,
        Console,
        Log,
        Maintenance,
        Settings,
        Count
    };

    explicit NavRail(QWidget* parent = nullptr);

    void set_identity(const QString& name, const QString& detail);
    void set_current(Page page);
    [[nodiscard]] Page current() const noexcept { return current_; }

    void set_agent_online(bool online);
    void set_running(bool running);

    void set_log_count(int records);
    void set_usb_count(int devices);
    void set_pending(Page page, int changes);
    void set_graphics_warning(const char* tone);

    void set_guest_provisioned(bool provisioned);

Q_SIGNALS:
    void page_selected(Page page);
    void start_requested();
    void stop_requested();

private:
    struct Marker {
        QString text;
        const char* tone = nullptr;
        int pending = 0;
    };

    void set_marker(Page page, const QString& text, const char* tone);
    void show_marker(Page page);

    QLabel* name_label_ = nullptr;
    QLabel* detail_label_ = nullptr;
    QLabel* badge_ = nullptr;
    QPushButton* power_ = nullptr;
    QButtonGroup* group_ = nullptr;
    QPushButton* items_[static_cast<int>(Page::Count)]{};
    QLabel* markers_[static_cast<int>(Page::Count)]{};
    std::array<Marker, static_cast<int>(Page::Count)> state_{};
    Page current_ = Page::Overview;
    bool running_ = false;
};

}
