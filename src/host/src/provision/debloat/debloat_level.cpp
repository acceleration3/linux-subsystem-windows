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
// Notepad, Calculator, Photos and the Snipping Tool stay, and so do
// SecHealthUI and the App Installer. The list below was checked against
// a real Windows 11 25H2 image: every name in it is either present
// there or present on another edition, and nothing in it is a prefix of
// a package the rest depend on.
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

// What survives in WinSxS.
//
// The component store is where the size of a Windows image actually
// is, and the only pass here that moves the number much. Measured
// against a real 25H2 image, one edition isolated: 12.5 GiB of unique
// content, of which WinSxS references 9.7 GiB - but 6.7 GiB of that is
// hardlinked to the live files under System32 and frees nothing when
// the store entry goes. The 3.0 GiB the store holds exclusively is the
// prize, and the keep-list below leaves 2.8 GiB of it deletable.
//
// It cannot be pruned by naming what goes: the directories are
// <arch>_<component>_<publisher>_<version>_<hash>, so the only stable
// part is the component name in the middle. Matched as a substring for
// that reason, which is loose on purpose - a name that catches a few
// extra directories keeps bytes, and a name that misses one loses a
// component.
//
// Five of these are directories rather than components. Catalogs,
// FileMaps, Fusion, InstallTemp and Manifests are the store's own
// bookkeeping; without Manifests in particular the image does not boot,
// because that is where the assembly identities live. Backup is
// deliberately not among them: it is the servicing stack's copy of
// superseded files, 185 MiB of it, and nothing reads it unless an
// update is being rolled back.
//
// The rest are the runtimes applications bind to as side-by-side
// assemblies rather than as plain DLLs. An old program that fails with
// "the application has failed to start because its side-by-side
// configuration is incorrect" is one whose CRT was taken out of here.
const QStringList kWinSxSKeep = {
    "Catalogs",
    "FileMaps",
    "Fusion",
    "InstallTemp",
    "Manifests",
    // The servicing stack itself. Removing it does not merely stop
    // updates, it stops the image finishing its own installation.
    "servicingstack",
    "servicing-stack",
    // Visual C++ runtimes, as side-by-side assemblies.
    "vc80",
    "vc90",
    "vcruntime",
    "crt",
    // Win32 UI the shell and installers draw with.
    "common-controls",
    "commoncontrols",
    "gdiplus",
    "shell32",
    "user32",
    "comctl32",
    // Accessibility, which the agent's UIA automation talks to.
    "uiautomation",
    "oleaut32",
    // .NET assemblies registered side by side.
    "netfx",
    "dotnet",
};

// Driver packages a guest on this domain will never bind.
//
// The domain gives the guest virtio storage, virtio net, a virtio
// serial channel, a balloon, an emulated PS/2-and-USB input path, no
// sound device and either no display adapter or a basic one.
//
// Worth knowing before adding to this list: on its own it frees
// nothing. Every file in the driver store is hardlinked to a copy in
// WinSxS, so a driver package deleted here still has its bytes held by
// the component store. Measured against a real 25H2 image, deleting
// the whole driver store while WinSxS stayed intact freed zero bytes,
// and deleting the list below on top of the WinSxS pass freed 142 MiB.
// It only pays in combination.
//
// Nor is there a graphics driver to find: retail media carries no
// vendor display drivers at all - Windows Update delivers those - so
// there is nothing here for NVIDIA, AMD or Intel to match, and the
// prefixes that would have caught them are not worth the risk of
// catching something else.
//
// A remove-list rather than a keep-list. The driver store is where a
// wrong deletion is unrecoverable: an image missing its boot storage or
// its input stack installs cleanly and then presents a machine that
// cannot be driven, and there is no way to put the driver back from
// inside it. Every prefix below was checked against the 713 packages in
// a real image and matches only what its comment says. Note in
// particular what is NOT here: "nv" would be the obvious way to write
// the NVIDIA rule and it catches nvmedisk, nvraid and nvdimm - the NVMe
// storage stack.
const QStringList kDriverRemove = {
    // Printing. Fourteen in-box printer INFs plus the spooler's own.
    "prn",
    "ntprint",
    "wsdprint",
    // Scanners and still-image capture.
    "sti",
    "wsdscan",
    // Bluetooth, whole stack.
    "bth",
    // Wi-Fi and cellular.
    "netwtw",   // Intel
    "netwew",   // Intel, newer
    "athw",     // Atheros
    "rtwlan",   // Realtek
    // Sensors: ambient light, HID sensor class, the sensor service.
    "sensor",
    // TPM, including the virtual smart card that rides on it. The
    // domain has no TPM and setup is told not to look for one.
    "tpm",
    // HD Audio. The domain declares no sound device; adding one later
    // means taking this line out again.
    "hdaudio",
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
    return QStringLiteral(
        "This cannot be undone from inside the guest. Gutting the component store takes "
        "Windows Update with it — including security updates — along with sfc, DISM "
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

}  // namespace lsw::host::provision::debloat
