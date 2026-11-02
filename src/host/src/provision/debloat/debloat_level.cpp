#include "lsw/host/provision/debloat/debloat_level.hpp"

namespace lsw::host::provision::debloat {

namespace {
const QStringList kConsumerApps = {
    "Microsoft.BingNews",
    "Microsoft.BingWeather",
    "Microsoft.BingSearch",
    "Microsoft.GamingApp",
    "Microsoft.GetHelp",
    "Microsoft.Getstarted",
    "Microsoft.Microsoft3DViewer",
    "Microsoft.MicrosoftOfficeHub",
    "Microsoft.MicrosoftSolitaireCollection",
    "Microsoft.MicrosoftStickyNotes",
    "Microsoft.MixedReality.Portal",
    "Microsoft.OutlookForWindows",
    "Microsoft.Paint",
    "Microsoft.People",
    "Microsoft.PowerAutomateDesktop",
    "Microsoft.Todos",
    "Microsoft.WindowsAlarms",
    "Microsoft.WindowsCamera",
    "Microsoft.WindowsFeedbackHub",
    "Microsoft.WindowsMaps",
    "Microsoft.WindowsSoundRecorder",
    "Microsoft.WindowsTerminal",
    "Microsoft.Xbox",
    "Microsoft.YourPhone",
    "Microsoft.ZuneMusic",
    "Microsoft.ZuneVideo",
    "MicrosoftCorporationII.MicrosoftFamily",
    "MicrosoftWindows.Client.WebExperience",
    "MicrosoftWindows.CrossDevice",
    "MicrosoftCorporationII.QuickAssist",
    "MicrosoftTeams",
    "MSTeams",
    "Clipchamp.Clipchamp",
};

const QStringList kSanePaths = {
    "/Windows/System32/OneDriveSetup.exe",
    "/Windows/SysWOW64/OneDriveSetup.exe",
};

const QStringList kMaximumPaths = {
    "/Program Files (x86)/Microsoft/Edge",
    "/Program Files (x86)/Microsoft/EdgeUpdate",
    "/Program Files (x86)/Microsoft/EdgeCore",
    "/Windows/System32/Microsoft-Edge-Webview",
    "/Windows/System32/Recovery/winre.wim",
    "/Windows/Web/Wallpaper",
    "/Windows/Web/4K",
};

const QStringList kMaximumComponents = {
    "Microsoft-Windows-InternetExplorer-Optional-Package",
    "Microsoft-Windows-MediaPlayer-Package",
    "Microsoft-Windows-TabletPCMath-Package",
    "Microsoft-Windows-StepsRecorder-Package",
    "Microsoft-Windows-WordPad-Package",
    "Microsoft-Windows-Wallpaper-Content-Extended",
    "Microsoft-Windows-Kernel-LA57",
    "Microsoft-Windows-Hello-Face-Package",
    "Microsoft-Windows-LanguageFeatures-Handwriting",
    "Microsoft-Windows-LanguageFeatures-OCR",
    "Microsoft-Windows-LanguageFeatures-Speech",
    "Microsoft-Windows-LanguageFeatures-TextToSpeech",
    "Windows-Defender-Client-Package",
};

const QStringList kWinSxSKeep = {
    "Catalogs",
    "FileMaps",
    "Fusion",
    "InstallTemp",
    "Manifests",
    "servicingstack",
    "servicing-stack",
    "vc80",
    "vc90",
    "vcruntime",
    "crt",
    "common-controls",
    "commoncontrols",
    "gdiplus",
    "shell32",
    "user32",
    "comctl32",
    "uiautomation",
    "oleaut32",
    "netfx",
    "dotnet",
};

const QStringList kDriverRemove = {
    "prn",
    "ntprint",
    "wsdprint",
    "sti",
    "wsdscan",
    "bth",
    "netwtw",
    "netwew",
    "athw",
    "rtwlan",
    "sensor",
    "tpm",
};

}

DebloatPlan::DebloatPlan(DebloatLevel level) : level_(level) {
    if (level_ == DebloatLevel::None) {
        return;
    }
    appx_prefixes_ = kConsumerApps;
    paths_ = kSanePaths;
    if (level_ == DebloatLevel::Maximum) {
        paths_ += kMaximumPaths;
        component_prefixes_ = kMaximumComponents;
        winsxs_keep_ = kWinSxSKeep;
        driver_remove_ = kDriverRemove;
    }
}

QString DebloatPlan::summary() const {
    switch (level_) {
    case DebloatLevel::None:
        return QStringLiteral("Windows is installed exactly as Microsoft ships it.");
    case DebloatLevel::Sane:
        return QStringLiteral(
            "Removes %1 bundled applications and turns off telemetry, Copilot, Recall, "
            "widgets and web search. Windows Update, Defender and the recovery "
            "environment are left working, so the guest still takes updates and can "
            "have features added back.")
            .arg(appx_prefixes_.size());
    case DebloatLevel::Maximum:
        return QStringLiteral(
            "Everything above, plus Edge and WebView2, Defender, the recovery "
            "environment, most of the component store and the drivers for hardware "
            "this VM does not have. Around 3 GiB smaller, and the fastest to "
            "install.");
    }
    return {};
}

QString DebloatPlan::warning() const {
    if (level_ != DebloatLevel::Maximum) {
        return {};
    }
    return QStringLiteral("Cannot be undone: Windows Update, repair, most drivers and WebView2 go "
                          "with it. For a disposable guest.");
}

QString DebloatPlan::warning_detail() const {
    if (level_ != DebloatLevel::Maximum) {
        return {};
    }
    return QStringLiteral(
        "This cannot be undone from inside the guest. Gutting the component store takes "
        "Windows Update with it, security updates included, along with sfc, DISM "
        "repair, update rollback, and any way to add a feature or a language later. "
        "It also drops the drivers for hardware this VM does not have, so printing, "
        "Bluetooth, Wi-Fi, scanners, sensors and audio are gone and a device passed "
        "through afterwards will find no driver for them. And an application that "
        "embeds a browser control will not find WebView2.\n\n"
        "Choose it for a disposable guest that runs one program, not one you intend to "
        "keep.");
}

QString DebloatPlan::to_string(DebloatLevel level) {
    switch (level) {
    case DebloatLevel::Sane:
        return QStringLiteral("sane");
    case DebloatLevel::Maximum:
        return QStringLiteral("maximum");
    case DebloatLevel::None:
        break;
    }
    return QStringLiteral("none");
}

DebloatLevel DebloatPlan::from_string(const QString& text) {
    if (text.compare(QLatin1String("sane"), Qt::CaseInsensitive) == 0) {
        return DebloatLevel::Sane;
    }
    if (text.compare(QLatin1String("maximum"), Qt::CaseInsensitive) == 0) {
        return DebloatLevel::Maximum;
    }
    return DebloatLevel::None;
}

}
