#include "lsw/host/provision/install/autounattend.hpp"

#include "autounattend_template.hpp"
#include "win11_bypass_template.hpp"

namespace lsw::host::provision::install {

namespace {

QString sub(QString s, const QString& key, const QString& value) {
    return s.replace(QStringLiteral("{{%1}}").arg(key), value);
}

}  // namespace

QString Autounattend::render(const AutounattendInputs& in) {
    QString out{embedded::kAutounattendTemplate};
    out = sub(out, "locale", in.locale);
    out = sub(out, "timezone", in.timezone);
    out = sub(out, "computer_name", in.computer_name);
    out = sub(out, "user_name", in.user_name);
    out = sub(out, "user_password", in.user_password);
    out = sub(out, "product_key", in.product_key);
    out = sub(out, "image_index", QString::number(in.image_index));
    out = sub(out, "win11_bypass_block",
              in.win11_bypass ? QString{embedded::kWin11BypassTemplate} : QString{});
    return out;
}

}  // namespace lsw::host::provision::install
