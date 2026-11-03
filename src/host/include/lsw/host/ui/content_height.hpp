#pragma once

#include <QObject>

#include <functional>

class QEvent;
class QWidget;

namespace lsw::host::ui {
class ContentHeight final : public QObject {
public:
    using ContentSource = std::function<QWidget*()>;

    static void keep(QWidget* window);

    static void keep(QWidget* window, ContentSource content);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    ContentHeight(QWidget* window, ContentSource content);

    void schedule();

    void apply();

    QWidget* window_;
    ContentSource content_;
    QWidget* watched_content_ = nullptr;
    bool pending_ = false;
};

}
