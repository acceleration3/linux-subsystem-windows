#pragma once

#include <QString>

class QFrame;
class QHBoxLayout;
class QVBoxLayout;

namespace lsw::host::ui {
class InfoTip;

struct Panel {
    QFrame* frame = nullptr;
    QHBoxLayout* header = nullptr;
    QVBoxLayout* body = nullptr;
    InfoTip* info = nullptr;

    [[nodiscard]] static Panel make(const QString& caption = {});

    [[nodiscard]] static Panel area(const QString& name, const QString& about = {},
                                    bool danger = false);

    void show_caption(const QString& data) const;

    void add_foot(const QString& meta_text) const;
    void explain(const QString& text) const;
};

}
