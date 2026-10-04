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
    out = sub(out, "product_key_block",
              in.product_key.isEmpty()
                  ? QString{}
                  : QStringLiteral("\t\t\t\t<ProductKey>\n"
                                   "\t\t\t\t\t<Key>%1</Key>\n"
                                   "\t\t\t\t\t<WillShowUI>OnError</WillShowUI>\n"
                                   "\t\t\t\t</ProductKey>")
                        .arg(in.product_key));
    out = sub(out, "image_index", QString::number(in.image_index));
    // The whole element or none of it: Setup has no way to be told
    // "the default path" once a <Path> is present.
    out = sub(out, "install_from_path",
              in.install_from_path.isEmpty()
                  ? QString{}
                  : QStringLiteral("\t\t\t\t\t\t<Path>%1</Path>").arg(in.install_from_path));
    out = sub(out, "win11_bypass_block",
              in.win11_bypass ? QString{embedded::kWin11BypassTemplate} : QString{});
    return out;
}

}  // namespace lsw::host::provision::install
