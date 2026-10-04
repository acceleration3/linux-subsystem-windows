#pragma once

#include <QString>
#include <QStringList>
#include <optional>

namespace lsw::host::config {

// One size the guest's monitor can offer.
struct DisplayMode {
    int width = 0;
    int height = 0;

    [[nodiscard]] bool operator==(const DisplayMode& other) const noexcept {
        return width == other.width && height == other.height;
    }

    // "2560x1440", the form the device's ``modes`` property and the
    // settings store take.
    [[nodiscard]] QString text() const;
    // "2560 × 1440", for people.
    [[nodiscard]] QString label() const;
    // The reduced ratio, "16:9"; "43:18" for 3440 × 1440.
    [[nodiscard]] QString aspect() const;
};

// The guest's resolution list: the rules accel-virt's driver applies to
// the device's ``modes``, and the forms the list is stored and sent in.
//
// The list is "WxH" entries, the first preferred. Empty is the host
// monitors' own modes, which is the device's default. Pure functions, so
// the settings page, the domain definition and the tests agree.
class DisplayModes {
public:
    // What avgpu.sys can describe (src/miniport/display/display_mode.*):
    // the smallest source mode dxgkrnl accepts, the largest edge an EDID
    // detailed timing holds, and its pixel clock field (10 kHz units).
    static constexpr int kMinEdge = 100;
    static constexpr int kMaxEdge = 4095;
    static constexpr qint64 kMaxPixelClockHz = 655'350'000;
    // The timing the driver gives every mode: 60 Hz, 160 pixels of
    // horizontal blanking, one blank line per 24 active ones.
    static constexpr int kRefreshHz = 60;
    static constexpr int kHorizontalBlank = 160;
    static constexpr int kLinesPerBlankLine = 24;
    // The most the device carries (DisplayModeSet::max_modes).
    static constexpr int kMaxModes = 32;

    DisplayModes() = delete;

    // "2560x1440" (either x, surrounding space allowed) as a mode, or
    // nullopt when it is not one.
    [[nodiscard]] static std::optional<DisplayMode> parse(const QString& text);

    // The pixel clock the driver times ``mode`` at, in Hz.
    [[nodiscard]] static qint64 pixel_clock_hz(const DisplayMode& mode);

    // Why the driver would leave ``mode`` out, as a sentence; empty when
    // it fits. The driver drops such a mode silently, so the editor says
    // why before it is ever offered.
    [[nodiscard]] static QString refusal(const DisplayMode& mode);

    // Why ``mode`` cannot be added to ``list``: a refusal above, a size
    // already there, or a full list. Empty when it can.
    [[nodiscard]] static QString refusal_to_add(const QStringList& list, const DisplayMode& mode);

    // ``list`` with every entry that does not parse, or that the driver
    // would refuse, dropped, the rest
    // normalised ("2560X1440 " is "2560x1440") and duplicates removed,
    // first occurrence kept.
    [[nodiscard]] static QStringList normalized(const QStringList& list);

    // What the device is given: ``list`` normalised, with ``output``
    // first when given (the monitor the manager is on, for "match the
    // monitor"), and a duplicate of it further down dropped.
    [[nodiscard]] static QStringList effective(const QStringList& list,
                                               const std::optional<DisplayMode>& output);

    // The device's property text: entries between commas.
    [[nodiscard]] static QString property(const QStringList& list);
};

}  // namespace lsw::host::config
