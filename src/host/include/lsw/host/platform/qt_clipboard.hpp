#pragma once

#include "lsw/host/platform/host_clipboard.hpp"

class QClipboard;

namespace lsw::host::platform {
class QtClipboard final : public HostClipboard {
    Q_OBJECT
public:
    explicit QtClipboard(QObject* parent = nullptr);

    void set(const ClipboardPayload& payload) override;
    void clear_if_owned() override;

    [[nodiscard]] bool working() const override { return true; }

    [[nodiscard]] bool limited() const override { return limited_; }

private:
    void on_data_changed();

    QClipboard* clipboard_;
    bool limited_;
    bool owned_ = false;
    bool suppress_ = false;
};

}
