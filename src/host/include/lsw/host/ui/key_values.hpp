#pragma once

#include <QString>

class QGridLayout;
class QHBoxLayout;
class QVBoxLayout;
class QWidget;

namespace lsw::host::ui {
class KeyValues {
public:
    explicit KeyValues(QVBoxLayout* body);

    QHBoxLayout* add(const QString& key, const QString& value, const char* tone = nullptr,
                     const QString& note = {});

    QHBoxLayout* add_widget(const QString& key, QWidget* value);

private:
    QGridLayout* grid_;
    int row_ = 0;
};

}
