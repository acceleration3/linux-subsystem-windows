#pragma once

#include "lsw/host/services/app_errors.hpp"

#include <QDialog>

namespace lsw::host::manager {
class AppErrorDialog : public QDialog {
    Q_OBJECT
public:
    explicit AppErrorDialog(const services::AppErrorReport& report, QWidget* parent = nullptr);

    [[nodiscard]] static QString heading(const services::AppErrorReport& report);
};

}
