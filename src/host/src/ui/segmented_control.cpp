#include "lsw/host/ui/segmented_control.hpp"

#include "lsw/host/ui/flow_layout.hpp"
#include "lsw/host/ui/theme.hpp"

#include <QAbstractButton>
#include <QFontMetrics>
#include <QPainter>

#include <algorithm>

namespace lsw::host::ui {

namespace {

constexpr int kPadX = 11;
constexpr int kPadY = 5;
constexpr int kNoteGap = 5;
constexpr int kLabelPx = 12;
constexpr int kNotePx = 10;

class SegmentButton final : public QAbstractButton {
public:
    SegmentButton(const QString& label, const QString& note, bool last, QWidget* parent)
        : QAbstractButton(parent), note_(note), last_(last) {
        setText(label);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAutoExclusive(false);
    }

    [[nodiscard]] QSize sizeHint() const override {
        const QFontMetrics label(label_font());
        int width = label.horizontalAdvance(text());
        if (!note_.isEmpty()) {
            width += kNoteGap + QFontMetrics(note_font()).horizontalAdvance(note_);
        }
        return {width + 2 * kPadX + (last_ ? 0 : 1), label.height() + 2 * kPadY + 2};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool on = isChecked();
        const bool live = isEnabled();
        if (on && live) {
            painter.fillRect(rect(), QColor(255, 159, 69, 0x1f));
        } else if (underMouse() && live) {
            painter.fillRect(rect(), Theme::color(Theme::Color::kRaised));
        }
        if (!last_) {
            painter.fillRect(QRect(width() - 1, 0, 1, height()), Theme::color(Theme::Color::kLine));
        }
        QColor text = Theme::color(Theme::Color::kFgDim);
        QColor note = Theme::color(Theme::Color::kFgFaint);
        if (!live) {
            text = note = Theme::color("#3a4150");
        } else if (on) {
            text = Theme::color(Theme::Color::kAmber);
            note = Theme::color("#d08a46");
        } else if (underMouse()) {
            text = Theme::color(Theme::Color::kFg);
        }
        const QFontMetrics label(label_font());
        const QRect body(0, 0, width() - (last_ ? 0 : 1), height());
        painter.setFont(label_font());
        painter.setPen(text);
        int x = kPadX;
        painter.drawText(QRect(x, body.top(), label.horizontalAdvance(this->text()), body.height()),
                         Qt::AlignVCenter | Qt::AlignLeft, this->text());
        if (!note_.isEmpty()) {
            x += label.horizontalAdvance(this->text()) + kNoteGap;
            painter.setFont(note_font());
            painter.setPen(note);
            painter.drawText(QRect(x, body.top(), width() - x, body.height()),
                             Qt::AlignVCenter | Qt::AlignLeft, note_);
        }
    }

private:
    [[nodiscard]] QFont label_font() const {
        QFont font = this->font();
        font.setPixelSize(kLabelPx);
        return font;
    }

    [[nodiscard]] static QFont note_font() { return Theme::mono_font(kNotePx); }

    QString note_;
    bool last_;
};

}

SegmentedControl::SegmentedControl(QWidget* parent) : QFrame(parent) {
    setObjectName(QString::fromLatin1(Theme::Role::kSegmented));
    setAttribute(Qt::WA_StyledBackground, true);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    row_ = new FlowLayout(this);
    row_->setContentsMargins(0, 0, 0, 0);
}

void SegmentedControl::set_options(const QVector<Option>& options) {
    if (options == options_) {
        return;
    }
    const QVariant before = current();
    options_ = options;
    for (QAbstractButton* button : std::as_const(buttons_)) {
        row_->removeWidget(button);
        button->hide();
        button->deleteLater();
    }
    buttons_.clear();
    for (qsizetype i = 0; i < options_.size(); ++i) {
        const Option& option = options_.at(i);
        auto* button = new SegmentButton(option.label, option.note, i + 1 == options_.size(), this);
        connect(button, &QAbstractButton::clicked, this, [this, i] {
            set_current(options_.at(i).value);
            Q_EMIT chosen(options_.at(i).value);
        });
        row_->addWidget(button);
        buttons_.push_back(button);
    }
    set_current(before);
}

void SegmentedControl::set_current(const QVariant& value) {
    for (qsizetype i = 0; i < options_.size(); ++i) {
        buttons_.at(i)->setChecked(options_.at(i).value.toString() == value.toString());
    }
}

QVariant SegmentedControl::current() const {
    for (qsizetype i = 0; i < options_.size(); ++i) {
        if (buttons_.at(i)->isChecked()) {
            return options_.at(i).value;
        }
    }
    return {};
}

}
