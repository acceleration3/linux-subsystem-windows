#pragma once

#include <QString>
#include <cstdint>

class QLabel;
class QTableWidget;

namespace lsw::host::ui {
class StatusPill {
public:
    enum class State : std::uint8_t { Pending, Running, Done, Failed, Warning };

    StatusPill() = delete;

    [[nodiscard]] static QLabel* make(State state, const QString& text);

    static void set(QLabel* pill, State state, const QString& text);

    static void set_cell(QTableWidget* table, int row, int column, State state,
                         const QString& detail = {});
};

}
