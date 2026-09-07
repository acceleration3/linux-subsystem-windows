#include "lsw/host/provision/debloat/debloat_level.hpp"

namespace lsw::host::provision::debloat {

namespace {

// Consumer applications. Bundled with every Windows install, none of
// them wanted on a machine whose entire job is running one application
// on somebody else's desktop.
//
// What is deliberately NOT here matters as much as what is. The Store
// stays, because an application removed with it cannot be put back. So
// do the runtime packages every other appx links against — VCLibs,
// the .NET native framework, UI.Xaml — which are dependencies rather
// than applications, and taking them out breaks whatever is left.
// Notepad, Calculator, Photos, Terminal and the Snipping Tool stay too:
// they are small, they are what a user reaches for inside the guest,
// and removing them is how a debloat earns a bad name.
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
    "Microsoft.People",
    "Microsoft.PowerAutomateDesktop",
    "Microsoft.Todos",
    "Microsoft.WindowsAlarms",
    "Microsoft.WindowsFeedbackHub",
    "Microsoft.WindowsMaps",
    "Microsoft.WindowsSoundRecorder",
    "Microsoft.Xbox",
    "Microsoft.YourPhone",
    "Microsoft.ZuneMusic",
    "Microsoft.ZuneVideo",
    "MicrosoftCorporationII.MicrosoftFamily",
    "MicrosoftCorporationII.QuickAssist",
    "MicrosoftTeams",
    "MSTeams",
    "Clipchamp.Clipchamp",
};

// OneDrive is a setup stub in System32 rather than a provisioned app,
// so it comes out as a file. Removing the stub is enough: nothing
// re-runs it, and no first-logon task recreates it.
const QStringList kSanePaths = {
    "/Windows/System32/OneDriveSetup.exe",
    "/Windows/SysWOW64/OneDriveSetup.exe",
};

// Everything above, plus the parts that make the machine serviceable.
//
// Edge goes as three directories rather than a package: it installs
// outside the component store and DISM never owned it. WebView2 goes
// with it, which is the tradeoff worth knowing about - an application
// that embeds a browser control will not find one.
//
// winre.wim is the recovery environment. Removing it reclaims several
// hundred megabytes and removes the ability to repair the install,
// which for a disposable guest is the right way round.
const QStringList kMaximumPaths = {
    "/Program Files (x86)/Microsoft/Edge",
    "/Program Files (x86)/Microsoft/EdgeUpdate",
    "/Program Files (x86)/Microsoft/EdgeCore",
    "/Windows/System32/Microsoft-Edge-Webview",
    "/Windows/System32/Recovery/winre.wim",
    "/Windows/Web/Wallpaper",
    "/Windows/Web/4K",
};

// Component packages, by name prefix. Optional features nothing in a
// guest reaches for, and the servicing/security packages that make the
// difference between "smaller" and "not serviceable".
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

}  // namespace

DebloatPlan::DebloatPlan(DebloatLevel level) : level_(level) {
    if (level_ == DebloatLevel::None) {
        return;
    }
    appx_prefixes_ = kConsumerApps;
    paths_ = kSanePaths;
    if (level_ == DebloatLevel::Maximum) {
        paths_ += kMaximumPaths;
        component_prefixes_ = kMaximumComponents;
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
            "environment and %1 optional component packages. Smallest and fastest to "
            "install.")
            .arg(component_prefixes_.size());
    }
    return {};
}

QString DebloatPlan::warning() const {
    if (level_ != DebloatLevel::Maximum) {
        return {};
    }
    return QStringLiteral(
        "This cannot be undone from inside the guest. Windows Update and Defender are "
        "gone, optional features and languages cannot be added later, and any "
        "application that embeds a browser control will not find WebView2. Choose it "
        "for a disposable guest, not one you intend to keep.");
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

}  // namespace lsw::host::provision::debloat
