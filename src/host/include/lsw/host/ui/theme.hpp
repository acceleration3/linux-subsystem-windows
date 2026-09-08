#pragma once

#include <QString>

namespace lsw::host::ui {

// The one stylesheet both applications wear, applied once to the
// QApplication so every dialog and every window created later inherits
// it without asking.
//
// It lives in ``embedded/theme.qss`` and is baked in at configure time,
// like the domain XML and the answer file. A stylesheet read from disk
// would be one more thing to install, and a manager whose theme is
// missing looks broken rather than plain.
class Theme {
public:
    // The stylesheet text.
    [[nodiscard]] static QString stylesheet();

    // Applies ``stylesheet()`` to the running QApplication and sets the
    // default font. Safe to call once, early, before any window exists.
    static void apply();

    // Object names the stylesheet selects on. Widgets set these instead
    // of carrying inline stylesheets of their own, so the whole look
    // stays in one file.
    struct Role {
        static constexpr const char* kPrimary = "Primary";
        static constexpr const char* kQuiet = "Quiet";
        static constexpr const char* kGo = "Go";
        static constexpr const char* kStop = "Stop";
        static constexpr const char* kIconButton = "IconButton";
        static constexpr const char* kMono = "Mono";
        static constexpr const char* kMeta = "Meta";
        static constexpr const char* kDim = "Dim";
        // Prose under a setting's name. Sans; kMeta is the mono one.
        static constexpr const char* kBody = "Body";
        static constexpr const char* kTile = "Tile";
        static constexpr const char* kStatusStrip = "StatusStrip";
        static constexpr const char* kCaption = "Caption";
        static constexpr const char* kHeading = "Heading";
        static constexpr const char* kPanel = "Panel";
        static constexpr const char* kPanelHeader = "PanelHeader";
        static constexpr const char* kSeparator = "Separator";
        static constexpr const char* kBadge = "Badge";
        static constexpr const char* kBadgeOnline = "BadgeOnline";
        static constexpr const char* kBadgeOffline = "BadgeOffline";
        static constexpr const char* kNavRail = "NavRail";
        static constexpr const char* kNavItem = "NavItem";
        static constexpr const char* kNavIdentity = "NavIdentity";
        static constexpr const char* kNavPower = "NavPower";
        static constexpr const char* kContentBar = "ContentBar";
        static constexpr const char* kWarnStrip = "WarnStrip";
        static constexpr const char* kWarnStripCritical = "WarnStripCritical";
        static constexpr const char* kWarnText = "WarnText";
        static constexpr const char* kWarnTextCritical = "WarnTextCritical";
        static constexpr const char* kApplyBar = "ApplyBar";
        static constexpr const char* kWizardSide = "WizardSide";
        static constexpr const char* kWizardSteps = "WizardSteps";
    };

    // Colours the stylesheet cannot reach: painted widgets, item
    // delegates, and the status words in the setup table.
    struct Color {
        static constexpr const char* kInk = "#0d0f14";
        static constexpr const char* kSurface = "#151922";
        static constexpr const char* kRaised = "#1c212c";
        static constexpr const char* kLine = "#272d3a";
        static constexpr const char* kFg = "#e4e8f0";
        static constexpr const char* kFgDim = "#9aa3b4";
        static constexpr const char* kFgFaint = "#646d7e";
        static constexpr const char* kAmber = "#ff9f45";
        static constexpr const char* kOk = "#4ec98a";
        static constexpr const char* kInfo = "#5aa9f0";
        static constexpr const char* kBad = "#f2555a";
    };
};

}  // namespace lsw::host::ui
