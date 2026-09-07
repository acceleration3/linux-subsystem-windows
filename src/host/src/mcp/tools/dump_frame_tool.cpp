#include "lsw/host/mcp/tools/dump_frame_tool.hpp"

#include "lsw/host/manager/frame_tap.hpp"
#include "lsw/host/mcp/json_schema.hpp"
#include "lsw/host/mcp/tool_result.hpp"

#include <nlohmann/json.hpp>

#include <QDir>
#include <QImage>
#include <QRect>
#include <QString>

namespace lsw::host::mcp {

namespace {

// Fully transparent rows/columns at each edge, and the box the opaque
// pixels actually occupy. This is the measurement that answers "is the
// gap in the frame or around it": a band of empty pixels INSIDE the
// image is the guest's problem, an image smaller than the frame rect is
// the mirror's.
nlohmann::json alpha_report(const QImage& src) {
    nlohmann::json r;
    if (src.isNull()) {
        r["empty"] = true;
        return r;
    }
    const QImage img = src.format() == QImage::Format_ARGB32
                           ? src
                           : src.convertToFormat(QImage::Format_ARGB32);
    int min_x = img.width();
    int min_y = img.height();
    int max_x = -1;
    int max_y = -1;
    std::uint64_t transparent = 0;
    for (int y = 0; y < img.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(row[x]) == 0) {
                ++transparent;
                continue;
            }
            min_x = std::min(min_x, x);
            max_x = std::max(max_x, x);
            min_y = std::min(min_y, y);
            max_y = std::max(max_y, y);
        }
    }
    if (max_x < min_x || max_y < min_y) {
        r["all_transparent"] = true;
        return r;
    }
    r["opaque_box"] = {{"x", min_x}, {"y", min_y},
                       {"w", max_x - min_x + 1}, {"h", max_y - min_y + 1}};
    // The empty margin on each side, i.e. what a crop would remove.
    r["empty_margin"] = {{"left", min_x},
                         {"top", min_y},
                         {"right", img.width() - 1 - max_x},
                         {"bottom", img.height() - 1 - max_y}};
    r["fully_transparent_px"] = transparent;
    return r;
}

nlohmann::json rect_json(QRect r) {
    return {{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}};
}

}  // namespace

std::string DumpFrameTool::name() const { return "dump_frame"; }

std::string DumpFrameTool::description() const {
    return "Dump what a mirror is actually showing. Writes TWO PNGs per "
           "mirrored window under $TMPDIR/lsw-frames/: ``png`` is the frame "
           "as the guest sent it, ``widget_png`` is the mirror widget "
           "rendered as it appears on screen — letterbox bars, holes and all "
           "— so the frame and what was done with it can be compared "
           "directly. Returns, for each: size, and where the opaque pixels "
           "sit inside it (``opaque_box`` / ``empty_margin``; a non-zero "
           "margin is a transparent gap, and which side it is on says where "
           "it came from). Plus the mirror's geometry: widget size, the frame "
           "rect the image is drawn into, the sub-rect being shown, and the "
           "resulting scale. Read the PNGs to see them. Args: ``hwnd`` "
           "(optional; all mirrors when omitted).";
}

nlohmann::json DumpFrameTool::input_schema() const {
    return JsonSchema::object({{"hwnd", {{"type", "integer"}}}});
}

nlohmann::json DumpFrameTool::call(const nlohmann::json& args) {
    auto& tap = manager::FrameTap::instance();
    QList<std::uint64_t> targets;
    if (const auto wanted = args.value("hwnd", std::uint64_t{0}); wanted != 0) {
        targets.append(wanted);
    } else {
        targets = tap.hwnds();
    }
    if (targets.isEmpty()) {
        return ToolResult::error(
            "no mirror has received a frame — is a guest window mirrored?");
    }

    const QString dir = QDir::tempPath() + QStringLiteral("/lsw-frames");
    if (!QDir().mkpath(dir)) {
        return ToolResult::error("cannot create " + dir.toStdString());
    }

    nlohmann::json out = nlohmann::json::array();
    for (const std::uint64_t hwnd : targets) {
        const auto entry = tap.get(hwnd);
        if (!entry.has_value()) {
            out.push_back({{"hwnd", hwnd}, {"error", "no frame recorded"}});
            continue;
        }
        nlohmann::json w;
        w["hwnd"] = hwnd;
        w["title"] = entry->title.toStdString();
        w["popup"] = entry->popup;
        if (entry->owner != 0) {
            w["owner"] = entry->owner;
        }
        w["image"] = {{"w", entry->image.width()}, {"h", entry->image.height()}};
        w["alpha"] = alpha_report(entry->image);
        w["widget"] = {{"w", entry->widget.width()}, {"h", entry->widget.height()}};
        w["frame_rect"] = rect_json(entry->frame_rect);
        w["content_rect"] = rect_json(entry->content_rect);
        if (entry->image.width() > 0 && entry->frame_rect.width() > 0) {
            w["scale"] = double(entry->frame_rect.width())
                         / (entry->content_rect.isEmpty()
                                ? entry->image.width()
                                : entry->content_rect.width());
        }
        const QString path =
            QStringLiteral("%1/frame-%2.png").arg(dir).arg(hwnd, 0, 16);
        w["png"] = entry->image.save(path, "PNG") ? path.toStdString()
                                                  : std::string{"<save failed>"};

        // The widget as it renders — the frame plus whatever the mirror
        // put around it.
        const QImage shot = tap.grab_widget(hwnd);
        if (shot.isNull()) {
            w["widget_png"] = "<mirror gone>";
        } else {
            const QString wpath =
                QStringLiteral("%1/widget-%2.png").arg(dir).arg(hwnd, 0, 16);
            w["widget_png"] = shot.save(wpath, "PNG") ? wpath.toStdString()
                                                      : std::string{"<save failed>"};
            w["widget_alpha"] = alpha_report(shot);
        }
        out.push_back(std::move(w));
    }
    return ToolResult::json_body({{"frames", out}});
}

}  // namespace lsw::host::mcp
