#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QStringList>

class QTableWidget;
class QTableWidgetItem;
class QWidget;

namespace lsw::host::ui {
class Theme {
public:
    [[nodiscard]] static QString stylesheet();

    static void apply();

    static void surface(QWidget* widget, const char* role);

    static void rows_table(QTableWidget* table, const QStringList& headers);

    [[nodiscard]] static QTableWidgetItem* kind_cell(const QString& text);

    static void fit_rows(QTableWidget* table);

    [[nodiscard]] static int cell_inset();

    [[nodiscard]] static QFont mono_font(int pixel_size);

    [[nodiscard]] static QColor color(const char* hex);

    struct Role {
        static constexpr const char* kPrimary = "Primary";
        static constexpr const char* kQuiet = "Quiet";
        static constexpr const char* kGo = "Go";
        static constexpr const char* kStop = "Stop";
        static constexpr const char* kIconButton = "IconButton";
        static constexpr const char* kIconTool = "IconTool";
        static constexpr const char* kMono = "Mono";
        static constexpr const char* kMeta = "Meta";
        static constexpr const char* kDim = "Dim";
        static constexpr const char* kBody = "Body";
        static constexpr const char* kTile = "Tile";
        static constexpr const char* kOptionCard = "OptionCard";
        static constexpr const char* kKpiGrid = "KpiGrid";
        static constexpr const char* kKpiValue = "KpiValue";
        static constexpr const char* kCardName = "CardName";
        static constexpr const char* kTerm = "Term";
        static constexpr const char* kScreen = "Screen";
        static constexpr const char* kMeter = "Meter";
        static constexpr const char* kCallout = "Callout";
        static constexpr const char* kCalloutCritical = "CalloutCritical";
        static constexpr const char* kStatusStrip = "StatusStrip";
        static constexpr const char* kCaption = "Caption";
        static constexpr const char* kHeading = "Heading";
        static constexpr const char* kPanel = "Panel";
        static constexpr const char* kPanelHeader = "PanelHeader";
        static constexpr const char* kListFoot = "ListFoot";
        static constexpr const char* kSeparator = "Separator";
        static constexpr const char* kBadge = "Badge";
        static constexpr const char* kBadgeOnline = "BadgeOnline";
        static constexpr const char* kBadgeOffline = "BadgeOffline";
        static constexpr const char* kNavRail = "NavRail";
        static constexpr const char* kNavItem = "NavItem";
        static constexpr const char* kNavIdentity = "NavIdentity";
        static constexpr const char* kNavPower = "NavPower";
        static constexpr const char* kNavRule = "NavRule";
        static constexpr const char* kContentBar = "ContentBar";
        static constexpr const char* kWarnStrip = "WarnStrip";
        static constexpr const char* kWarnStripCritical = "WarnStripCritical";
        static constexpr const char* kWarnText = "WarnText";
        static constexpr const char* kWarnTextCritical = "WarnTextCritical";
        static constexpr const char* kApplyBar = "ApplyBar";
        static constexpr const char* kWizardSide = "WizardSide";
        static constexpr const char* kWizardSteps = "WizardSteps";
        static constexpr const char* kWizardSideFoot = "WizardSideFoot";
        static constexpr const char* kWizardHead = "WizardHead";
        static constexpr const char* kWizardFoot = "WizardFoot";
        static constexpr const char* kWizardTitle = "WizardTitle";
        static constexpr const char* kDialogFoot = "DialogFoot";
        static constexpr const char* kPill = "Pill";
        static constexpr const char* kPillRun = "PillRun";
        static constexpr const char* kPillDone = "PillDone";
        static constexpr const char* kPillFail = "PillFail";
        static constexpr const char* kPillWarn = "PillWarn";
        static constexpr const char* kSegmented = "Segmented";
        static constexpr const char* kItemRow = "ItemRow";
        static constexpr const char* kItemName = "ItemName";
        static constexpr const char* kItemNote = "ItemNote";
        static constexpr const char* kDriveBadge = "DriveBadge";
        static constexpr const char* kStepChip = "StepChip";
    };

    struct Tone {
        static constexpr const char* kProperty = "tone";
        static constexpr const char* kAmber = "amber";
        static constexpr const char* kWarm = "warm";
        static constexpr const char* kOk = "ok";
        static constexpr const char* kInfo = "info";
        static constexpr const char* kBad = "bad";
        static constexpr const char* kFaint = "faint";
        static constexpr const char* kRestart = "restart";
        static constexpr const char* kNow = "now";
        static constexpr const char* kDim = "dim";
    };

    struct Color {
        static constexpr const char* kInk = "#0d0f14";
        static constexpr const char* kSurface = "#151922";
        static constexpr const char* kRaised = "#1c212c";
        static constexpr const char* kBase = "#0f131a";
        static constexpr const char* kLine = "#272d3a";
        static constexpr const char* kFg = "#e4e8f0";
        static constexpr const char* kFgDim = "#9aa3b4";
        static constexpr const char* kFgFaint = "#646d7e";
        static constexpr const char* kAmber = "#ff9f45";
        static constexpr const char* kOk = "#4ec98a";
        static constexpr const char* kInfo = "#5aa9f0";
        static constexpr const char* kBad = "#f2555a";
        static constexpr const char* kBadSurface = "#1f1315";
    };
};

}
