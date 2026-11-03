#pragma once

#include <QString>

class QFrame;
class QHBoxLayout;
class QVBoxLayout;

namespace lsw::host::ui {
struct Panel {
    QFrame* frame = nullptr;
    QHBoxLayout* header = nullptr;
    QVBoxLayout* body = nullptr;

    [[nodiscard]] static Panel make(const QString& caption = {});

    [[nodiscard]] static Panel area(const QString& name, const QString& caption = {},
                                    bool danger = false);

    void add_foot(const QString& meta_text) const;
};

}
