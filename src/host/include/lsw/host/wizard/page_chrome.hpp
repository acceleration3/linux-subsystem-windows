#pragma once

#include <QString>

class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;
class QWizardPage;

namespace lsw::host::wizard {
struct PageChrome {
    QVBoxLayout* head = nullptr;
    QLabel* title = nullptr;
    QLabel* subtitle = nullptr;
    QVBoxLayout* body = nullptr;

    QListWidget* steps = nullptr;
    QLabel* rail_host = nullptr;
    QLabel* rail_free = nullptr;

    QLabel* counter = nullptr;
    QPushButton* cancel = nullptr;
    QPushButton* back = nullptr;
    QPushButton* next = nullptr;

    [[nodiscard]] static PageChrome install(QWizardPage* page, const QString& title,
                                            const QString& subtitle);
};

}
