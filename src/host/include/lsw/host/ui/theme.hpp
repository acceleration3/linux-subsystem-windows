#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <QStringList>

class QTableWidget;
class QTableWidgetItem;
class QWidget;

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

    // Give ``widget`` one of the surface roles below.
    //
    // A plain QWidget does not paint a stylesheet background: QWidget's
    // own paintEvent draws nothing, and only WA_StyledBackground routes
    // it through the style. Borders come out either way, which is what
    // makes the mistake hard to see — a rail with its edge but not its
    // ground looks like a rail that is simply the wrong colour. Every
    // surface goes through here so none of them can forget.
    static void surface(QWidget* widget, const char* role);

    // Dress a table as the design's row list: no box, no grid, a rule
    // under letter-spaced mono headings, and rows tall enough to read
    // down. Every table in the application goes through here so they
    // cannot drift apart one page at a time.
    static void rows_table(QTableWidget* table, const QStringList& headers);

    // A cell for the secondary column of such a table: mono and faint,
    // because it names a kind rather than a thing.
    [[nodiscard]] static QTableWidgetItem* kind_cell(const QString& text);

    // Freeze a table to exactly the height of what it holds, so what
    // follows it sits under the last row rather than at the bottom of
    // the page.
    static void fit_rows(QTableWidget* table);

    // How far a widget set into a rows_table cell is inset from the
    // cell's edges on either side, the same as the cell's text.
    [[nodiscard]] static int cell_inset();

    // The mono face at ``pixel_size``, for the places a font is set in
    // code rather than by the stylesheet: painted headings, item
    // delegates, and widgets that want the face before they are polished.
    [[nodiscard]] static QFont mono_font(int pixel_size);

    // A hex colour, usually one of the Color constants below, as the
    // QColor a painter wants.
    [[nodiscard]] static QColor color(const char* hex);

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
        // A whole choice as one target: the option and what it costs.
        static constexpr const char* kOptionCard = "OptionCard";
        static constexpr const char* kKpiGrid = "KpiGrid";
        // The figure in a KpiGrid tile.
        static constexpr const char* kKpiValue = "KpiValue";
        // A card's own name in its header: an adapter, the device.
        static constexpr const char* kCardName = "CardName";
        // Lines a machine wrote, on black: the driver log's tail.
        static constexpr const char* kTerm = "Term";
        // The guest's console, framed like a panel and black inside.
        static constexpr const char* kScreen = "Screen";
        // Bars whose colour follows a "pressure" property.
        static constexpr const char* kMeter = "Meter";
        // A consequence, set apart from the explanation above it.
        static constexpr const char* kCallout = "Callout";
        static constexpr const char* kStatusStrip = "StatusStrip";
        static constexpr const char* kCaption = "Caption";
        static constexpr const char* kHeading = "Heading";
        static constexpr const char* kPanel = "Panel";
        static constexpr const char* kPanelHeader = "PanelHeader";
        // The add row under an editable list, on the header's ground.
        static constexpr const char* kListFoot = "ListFoot";
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
        static constexpr const char* kWizardSideFoot = "WizardSideFoot";
        static constexpr const char* kWizardHead = "WizardHead";
        static constexpr const char* kWizardFoot = "WizardFoot";
        static constexpr const char* kWizardTitle = "WizardTitle";
        // A dialog's button row, on the surface colour with a rule over it.
        static constexpr const char* kDialogFoot = "DialogFoot";
        // A status word with its dot: ONLINE, Done, Congested.
        static constexpr const char* kPill = "Pill";
        static constexpr const char* kPillRun = "PillRun";
        static constexpr const char* kPillDone = "PillDone";
        static constexpr const char* kPillFail = "PillFail";
        // Something to act on that is not broken: no driver yet.
        static constexpr const char* kPillWarn = "PillWarn";
    };

    // Values of the "tone" property, which recolours a widget for what
    // it says without changing what it is: a caption that counts
    // pending changes, a sentence that warns. The stylesheet pairs each
    // with the roles that use it; the role keeps the face and the size.
    struct Tone {
        static constexpr const char* kProperty = "tone";
        static constexpr const char* kAmber = "amber";
        // The pale amber of warning text.
        static constexpr const char* kWarm = "warm";
        static constexpr const char* kOk = "ok";
        static constexpr const char* kInfo = "info";
        static constexpr const char* kBad = "bad";
        static constexpr const char* kFaint = "faint";
        // The now-or-restart tags beside a setting's name.
        static constexpr const char* kRestart = "restart";
        static constexpr const char* kNow = "now";
    };

    // Colours the stylesheet cannot reach: painted widgets, item
    // delegates, and the status words in the setup table.
    struct Color {
        static constexpr const char* kInk = "#0d0f14";
        static constexpr const char* kSurface = "#151922";
        static constexpr const char* kRaised = "#1c212c";
        // What text fields and indicators are filled with: a shade
        // under the ground, so an input reads as a hole in it.
        static constexpr const char* kBase = "#0f131a";
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
