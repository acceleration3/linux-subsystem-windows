#pragma once

#include <QString>
#include <cstdint>

class QLabel;
class QTableWidget;

namespace lsw::host::ui {

// A status word with its dot, in a pill: Pending, Running, Done,
// Failed. Each state keeps the colour that means that everywhere else,
// so a column of them reads from shape and colour before the word is.
class StatusPill {
public:
    enum class State : std::uint8_t { Pending, Running, Done, Failed };

    StatusPill() = delete;

    // "●  ``text``" in the colours for ``state``.
    [[nodiscard]] static QLabel* make(State state, const QString& text);

    // A step's state in a table cell: the state's own word, or
    // ``detail`` in its place ("Failed: ``detail``" for a failure),
    // padded in from the cell's edges.
    static void set_cell(QTableWidget* table, int row, int column, State state,
                         const QString& detail = {});
};

}  // namespace lsw::host::ui
