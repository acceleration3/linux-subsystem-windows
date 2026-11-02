#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

namespace lsw::host::manager {
class OpenDocumentDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { Cancel, Share, Copy };

    OpenDocumentDialog(const QStringList& files, const QStringList& folders,
                       const QString& application, bool guest_running, int unshareable,
                       QWidget* parent = nullptr);

    [[nodiscard]] Choice choice() const noexcept { return choice_; }

private:
    Choice choice_ = Choice::Cancel;
};

}
