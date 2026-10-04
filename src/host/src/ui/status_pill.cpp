#include "lsw/host/ui/status_pill.hpp"

#include "lsw/host/ui/theme.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>

namespace lsw::host::ui {

namespace {

const char* role_for(StatusPill::State state) {
    switch (state) {
    case StatusPill::State::Running:
        return Theme::Role::kPillRun;
    case StatusPill::State::Done:
        return Theme::Role::kPillDone;
    case StatusPill::State::Failed:
        return Theme::Role::kPillFail;
    case StatusPill::State::Pending:
        break;
    }
    return Theme::Role::kPill;
}

QString word_for(StatusPill::State state, const QString& detail) {
    switch (state) {
    case StatusPill::State::Running:
        return detail.isEmpty() ? QStringLiteral("Running") : detail;
    case StatusPill::State::Done:
        return detail.isEmpty() ? QStringLiteral("Done") : detail;
    case StatusPill::State::Failed:
        return detail.isEmpty() ? QStringLiteral("Failed")
                                : QStringLiteral("Failed: %1").arg(detail);
    case StatusPill::State::Pending:
        break;
    }
    return QStringLiteral("Pending");
}

}  // namespace

QLabel* StatusPill::make(State state, const QString& text) {
    auto* pill = new QLabel(QStringLiteral("●  %1").arg(text));
    pill->setObjectName(QString::fromLatin1(role_for(state)));
    return pill;
}

void StatusPill::set_cell(QTableWidget* table, int row, int column, State state,
                          const QString& detail) {
    if (table == nullptr || row < 0 || row >= table->rowCount()) {
        return;
    }
    auto* holder = new QWidget;
    auto* layout = new QHBoxLayout(holder);
    layout->setContentsMargins(8, 0, 8, 0);
    layout->addWidget(make(state, word_for(state, detail)), 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch(1);
    table->setCellWidget(row, column, holder);
}

}  // namespace lsw::host::ui
