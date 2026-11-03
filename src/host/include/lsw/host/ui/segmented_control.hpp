#pragma once

#include <QFrame>
#include <QString>
#include <QVariant>
#include <QVector>

class QAbstractButton;
namespace lsw::host::ui {
class FlowLayout;

class SegmentedControl : public QFrame {
    Q_OBJECT
public:
    struct Option {
        QString label;
        QString note;
        QVariant value;

        [[nodiscard]] bool operator==(const Option&) const = default;
    };

    explicit SegmentedControl(QWidget* parent = nullptr);

    void set_options(const QVector<Option>& options);
    void set_current(const QVariant& value);

    [[nodiscard]] QVariant current() const;

Q_SIGNALS:
    void chosen(const QVariant& value);

private:
    FlowLayout* row_ = nullptr;
    QVector<Option> options_;
    QVector<QAbstractButton*> buttons_;
};

}
