#include "lsw/host/provision/install/product_keys.hpp"

#include <array>
#include <utility>

namespace lsw::host::provision::install {

namespace {

// KMS client keys: learn.microsoft.com/windows-server/get-started/kms-client-activation-keys.
// The Core (Home) rows are the generic install keys, since Home has no
// KMS key.
constexpr std::array<std::pair<const char*, const char*>, 19> kKeys{{
    {"Core", "YTMG3-N6DKC-DKB77-7M9GH-8HVX7"},
    {"CoreN", "4CPRK-NM3K3-X6XXQ-RXX86-WXCHW"},
    {"CoreSingleLanguage", "BT79Q-G7N6G-PGBYW-4YWX6-6F4BT"},
    {"CoreCountrySpecific", "N2434-X9D7W-8PF6X-8DV9T-8TYMD"},
    {"Professional", "W269N-WFGWX-YVC9B-4J6C9-T83GX"},
    {"ProfessionalN", "MH37W-N47XK-V7XM9-C7227-GCQG9"},
    {"ProfessionalWorkstation", "NRG8B-VKK3Q-CXVCJ-9G2XF-6Q84J"},
    {"ProfessionalWorkstationN", "9FNHH-K3HBT-3W4TD-6383H-6XYWF"},
    {"ProfessionalEducation", "6TP4R-GNPTD-KYYHQ-7B7DP-J447Y"},
    {"ProfessionalEducationN", "YVWGF-BXNMC-HTQYQ-CPQ99-66QFC"},
    {"Education", "NW6C2-QMPVW-D7KKK-3GKT6-VCFB2"},
    {"EducationN", "2WH4N-8QGBV-H22JP-CT43Q-MDWWJ"},
    {"Enterprise", "NPPR9-FWDCX-D2C8J-H872K-2YT43"},
    {"EnterpriseN", "DPH2V-TTNVB-4X9Q3-TJR4H-KHJW4"},
    {"EnterpriseG", "YYVX9-NTFWV-6MDM3-9PT4T-4M68B"},
    {"EnterpriseGN", "44RPN-FTY23-9VTTB-MP9BX-T84FV"},
    {"EnterpriseS", "M7XTQ-FN8P6-TTKYV-9D4CC-J462D"},
    {"EnterpriseSN", "92NFX-8DJQP-P6BBQ-THF9C-7CG2H"},
    {"IoTEnterpriseS", "KBN8V-HFGQ4-MGXVD-347P6-PDQGT"},
}};

}  // namespace

QString ProductKeys::for_edition(const QString& edition_id) {
    for (const auto& [edition, key] : kKeys) {
        if (edition_id.compare(QLatin1String(edition), Qt::CaseInsensitive) == 0) {
            return QString::fromLatin1(key);
        }
    }
    return {};
}

QString ProductKeys::fallback() {
    return for_edition(QStringLiteral("Professional"));
}

}  // namespace lsw::host::provision::install
