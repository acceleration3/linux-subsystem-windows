#!/usr/bin/env python3
import sys
from html import escape
from pathlib import Path

FONT = "-apple-system, BlinkMacSystemFont, 'Segoe UI', 'Noto Sans', Helvetica, Arial, sans-serif"
MONO = "ui-monospace, SFMono-Regular, 'SF Mono', Menlo, Consolas, 'Liberation Mono', monospace"

THEMES = {
    "light": dict(
        text="#1f2328", muted="#59636e", faint="#818b98", box="#ffffff", line="#d1d9e0",
        desk_a="#d7e5f6", desk_b="#eef3fa", frame="#b8c7da", bar="#f6f8fa", bar_line="#d1d9e0",
        title="#f6f8fa", win_shadow="#1f232822", accent="#0969da", ok="#1a7f37", warm="#bf8700",
        purple="#8250df", grid="#e6ebf1", cell="#f6f8fa", chip="#ddf4ff", chip_text="#0a3069",
        vm_fill="#f6f8fa", vm_line="#afb8c1", host_fill="#eef8f1", host_line="#a3d4b0",
        guest_fill="#eef4fd", guest_line="#a8c3ec", game_bg="#101521", gpu_fill="#1f2328", gpu_text="#ffffff",
    ),
    "dark": dict(
        text="#e6edf3", muted="#9198a1", faint="#6e7681", box="#0d1117", line="#3d444d",
        desk_a="#16233a", desk_b="#0f1622", frame="#2f3d52", bar="#151b23", bar_line="#30363d",
        title="#1c222b", win_shadow="#00000066", accent="#4493f8", ok="#3fb950", warm="#d29922",
        purple="#ab7df8", grid="#262c36", cell="#161b22", chip="#132f57", chip_text="#cfe2ff",
        vm_fill="#151b23", vm_line="#3d444d", host_fill="#0e2418", host_line="#24583a",
        guest_fill="#0f1d33", guest_line="#264b80", game_bg="#05070c", gpu_fill="#e6edf3", gpu_text="#0d1117",
    ),
}


class Canvas:
    def __init__(self, theme, width, height, label):
        self.c = THEMES[theme]
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" aria-label="{escape(label)}">'
        ]
        self.markers = set()

    def add(self, s):
        self.parts.append(s)

    def text(self, x, y, s, size=13, weight=400, fill=None, anchor="start", mono=False, italic=False):
        style = ' font-style="italic"' if italic else ""
        self.add(f'<text x="{x}" y="{y}" font-family="{MONO if mono else FONT}" font-size="{size}" '
                 f'font-weight="{weight}" fill="{fill or self.c["text"]}" text-anchor="{anchor}"{style}>'
                 f'{escape(s)}</text>')

    def rect(self, x, y, w, h, fill, line=None, r=8, width=1.2, dash=None, opacity=None):
        stroke = f' stroke="{line}" stroke-width="{width}"' if line else ""
        dashed = f' stroke-dasharray="{dash}"' if dash else ""
        alpha = f' opacity="{opacity}"' if opacity is not None else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{fill}"{stroke}{dashed}{alpha}/>')

    def arrow(self, d, color=None, width=1.6, dash=None, both=False):
        color = color or self.c["muted"]
        key = color.strip("#")
        self.markers.add(color)
        dashed = f' stroke-dasharray="{dash}"' if dash else ""
        start = f' marker-start="url(#s{key})"' if both else ""
        self.add(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}"{dashed} '
                 f'marker-end="url(#e{key})"{start}/>')

    def finish(self):
        defs = ["<defs>"]
        for color in sorted(self.markers):
            key = color.strip("#")
            defs.append(f'<marker id="e{key}" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" '
                        f'markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="{color}"/></marker>')
            defs.append(f'<marker id="s{key}" viewBox="0 0 10 10" refX="1" refY="5" markerWidth="7" '
                        f'markerHeight="7" orient="auto"><path d="M10,0 L0,5 L10,10 z" fill="{color}"/></marker>')
        defs.append("</defs>")
        return "\n".join(self.parts[:1] + defs + self.parts[1:] + ["</svg>"]) + "\n"


def windows_badge(cv, x, y, size, color):
    half = size / 2 - 0.6
    for dx in (0, size / 2 + 0.6):
        for dy in (0, size / 2 + 0.6):
            cv.rect(x + dx, y + dy, half, half, color, r=0.6)


def app_window(cv, x, y, w, h, title, focused=False):
    c = cv.c
    cv.rect(x + 3, y + 6, w, h, c["win_shadow"], r=10)
    cv.rect(x, y, w, h, c["box"], c["accent"] if focused else c["line"], r=10, width=1.6 if focused else 1.2)
    cv.add(f'<path d="M{x + 10},{y} H{x + w - 10} A10,10 0 0 1 {x + w},{y + 10} V{y + 32} H{x} V{y + 10} '
           f'A10,10 0 0 1 {x + 10},{y} Z" fill="{c["title"]}"/>')
    cv.add(f'<line x1="{x}" y1="{y + 32}" x2="{x + w}" y2="{y + 32}" stroke="{c["line"]}"/>')
    windows_badge(cv, x + 12, y + 10, 12, c["accent"])
    cv.text(x + 32, y + 21, title, size=13, weight=600)
    for i, glyph in enumerate(("—", "□", "✕")):
        cv.text(x + w - 64 + i * 22, y + 21, glyph, size=12, fill=c["muted"], anchor="middle")


def tag(cv, x, y, s, color):
    c = cv.c
    width = 18 + len(s) * 6.9
    cv.rect(x, y, width, 24, c["box"], color, r=12, width=1.4)
    cv.text(x + width / 2, y + 16, s, size=12, weight=600, fill=color, anchor="middle")
    return width


def hero(theme):
    W, H = 1200, 640
    cv = Canvas(theme, W, H, "Windows applications open as ordinary windows on the Linux desktop, "
                             "while Windows 11 runs out of sight in a virtual machine")
    c = cv.c
    cv.add(f'<defs><linearGradient id="desk" x1="0" y1="0" x2="1" y2="1">'
           f'<stop offset="0" stop-color="{c["desk_a"]}"/><stop offset="1" stop-color="{c["desk_b"]}"/>'
           f'</linearGradient><linearGradient id="sky" x1="0" y1="0" x2="0" y2="1">'
           f'<stop offset="0" stop-color="#7cc4ff"/><stop offset="1" stop-color="#ffd8a8"/></linearGradient>'
           f'<radialGradient id="shade" cx="0.35" cy="0.3" r="0.8"><stop offset="0" stop-color="#ffb86b"/>'
           f'<stop offset="1" stop-color="#e5534b"/></radialGradient></defs>')

    sx, sy, sw, sh = 20, 20, 1160, 470
    cv.rect(sx, sy, sw, sh, "url(#desk)", c["frame"], r=16, width=1.6)
    cv.text(sx + 22, sy + 36, "Your Linux desktop", size=15, weight=700, fill=c["muted"])

    ax, ay, aw, ah = 60, 80, 470, 300
    app_window(cv, ax, ay, aw, ah, "Budget.xlsx")
    gx0, gy0 = ax + 14, ay + 46
    cols, rows, cw, rh = 6, 8, 54, 24
    for r in range(rows):
        for col in range(cols):
            fill = c["cell"] if r == 0 or col == 0 else c["box"]
            cv.add(f'<rect x="{gx0 + col * cw}" y="{gy0 + r * rh}" width="{cw}" height="{rh}" fill="{fill}" '
                   f'stroke="{c["grid"]}"/>')
    for col, letter in enumerate("ABCDE", start=1):
        cv.text(gx0 + col * cw + cw / 2, gy0 + 16, letter, size=11, fill=c["muted"], anchor="middle")
    values = [1240, 980, 1530, 1110, 1675, 1320, 1890]
    for r, v in enumerate(values, start=1):
        cv.text(gx0 + cw / 2, gy0 + r * rh + 16, str(r), size=11, fill=c["muted"], anchor="middle")
        cv.text(gx0 + 2 * cw - 6, gy0 + r * rh + 16, f"{v:,}", size=11, anchor="end", mono=True)
    chart_x, chart_y, chart_w, chart_h = gx0 + 2 * cw + 10, gy0 + rh + 10, 3 * cw - 20, (rows - 1) * rh - 20
    cv.rect(chart_x, chart_y, chart_w, chart_h, c["box"], c["line"], r=4)
    for i, v in enumerate(values):
        bh = (v - 800) / 1100 * (chart_h - 24)
        cv.rect(chart_x + 12 + i * 20, chart_y + chart_h - 10 - bh, 13, bh, c["accent"], r=2, opacity=0.85)

    px, py, pw, ph = 430, 200, 400, 228
    app_window(cv, px, py, pw, ph, "Photo editor", focused=True)
    cv.rect(px + 12, py + 44, 34, ph - 56, c["cell"], c["grid"], r=6)
    for i in range(5):
        cv.rect(px + 19, py + 54 + i * 30, 20, 20, c["box"], c["line"], r=4)
    canvas_x, canvas_y, canvas_w, canvas_h = px + 58, py + 44, pw - 70, ph - 56
    cv.rect(canvas_x, canvas_y, canvas_w, canvas_h, "url(#sky)", r=6)
    cv.add(f'<circle cx="{canvas_x + canvas_w - 70}" cy="{canvas_y + 50}" r="22" fill="#fff4c2"/>')
    cv.add(f'<path d="M{canvas_x},{canvas_y + canvas_h} L{canvas_x + 90},{canvas_y + 90} '
           f'L{canvas_x + 160},{canvas_y + 150} L{canvas_x + 230},{canvas_y + 70} '
           f'L{canvas_x + canvas_w},{canvas_y + 160} L{canvas_x + canvas_w},{canvas_y + canvas_h} Z" '
           f'fill="#3d6b5a" opacity="0.9"/>')
    menu_x, menu_y = px + 120, py + 58
    cv.rect(menu_x + 3, menu_y + 4, 150, 104, c["win_shadow"], r=8)
    cv.rect(menu_x, menu_y, 150, 104, c["box"], c["line"], r=8)
    for i, item in enumerate(("Adjustments", "Filters", "Resize image", "Export as...")):
        if i == 1:
            cv.rect(menu_x + 4, menu_y + 6 + i * 24, 142, 22, c["chip"], r=5)
        cv.text(menu_x + 14, menu_y + 22 + i * 24, item, size=12)

    gx, gy, gw, gh = 860, 80, 290, 220
    app_window(cv, gx, gy, gw, gh, "Game")
    cv.rect(gx + 1, gy + 33, gw - 2, gh - 34, c["game_bg"], r=0)
    cv.add(f'<path d="M{gx + 145},{gy + 60} L{gx + 220},{gy + 190} L{gx + 70},{gy + 190} Z" fill="url(#shade)"/>')
    cv.add(f'<path d="M{gx + 145},{gy + 60} L{gx + 220},{gy + 190} L{gx + 175},{gy + 165} Z" fill="#00000040"/>')
    cv.text(gx + gw - 12, gy + 54, "144 fps", size=11, weight=600, fill="#3fb950", anchor="end", mono=True)

    tx = tag(cv, 92, 400, "its own window and taskbar entry", c["accent"])
    tag(cv, 470, 168, "menus and popups", c["purple"])
    tag(cv, 900, 316, "drawn on your GPU", c["ok"])

    tbx, tby, tbw, tbh = sx + 1, sy + sh - 44, sw - 2, 43
    cv.add(f'<path d="M{tbx},{tby} H{tbx + tbw} V{tby + tbh - 15} A15,15 0 0 1 {tbx + tbw - 15},{tby + tbh} '
           f'H{tbx + 15} A15,15 0 0 1 {tbx},{tby + tbh - 15} Z" fill="{c["bar"]}" stroke="{c["bar_line"]}"/>')
    icons = [("#8b949e", False, False), ("#d29922", False, False), ("#57606a", False, False),
             ("#1a7f37", True, False), ("#8250df", True, True), ("#cf222e", True, False)]
    ix = tbx + 20
    for color, windows, active in icons:
        cv.rect(ix, tby + 9, 26, 26, color, r=6)
        if windows:
            windows_badge(cv, ix + 17, tby + 26, 11, "#ffffff")
            cv.rect(ix + 16, tby + 25, 13, 13, c["accent"], r=2, opacity=0.9)
            windows_badge(cv, ix + 18, tby + 27, 9, "#ffffff")
        if active:
            cv.rect(ix + 6, tby + 38, 14, 3, c["accent"], r=1.5)
        ix += 40
    cv.text(ix + 10, tby + 27, "Windows apps sit in your taskbar beside Linux ones", size=12, fill=c["muted"])

    vx, vy, vw, vh = 20, 540, 1160, 80
    cv.rect(vx, vy, vw, vh, c["vm_fill"], c["vm_line"], r=14, dash="6 5")
    windows_badge(cv, vx + 22, vy + 22, 20, c["faint"])
    cv.text(vx + 56, vy + 36, "Windows 11, in a virtual machine you never have to look at", size=15, weight=700,
            fill=c["muted"])
    cv.text(vx + 56, vy + 58, "QEMU/KVM under your own user. LSW installs it, keeps it updated, and starts it "
                              "when an app needs it.", size=13, fill=c["faint"])
    for x, label, color in ((300, "windows", c["accent"]), (640, "GPU work", c["ok"]), (1000, "files", c["warm"])):
        cv.arrow(f"M{x},{vy - 2} L{x},{sy + sh + 4}", color=color, width=1.8, both=True)
        cv.text(x + 10, vy - 16, label, size=12, weight=600, fill=color)
    return cv.finish()


def box(cv, x, y, w, h, title, lines=(), accent=None, mono=True, dash=None):
    c = cv.c
    cv.rect(x, y, w, h, c["box"], c["line"], r=8, dash=dash)
    if accent:
        cv.rect(x, y + 8, 3, h - 16, accent, r=1.5)
    total = 1 + len(lines)
    top = y + h / 2 - (total - 1) * 10 + 6
    cv.text(x + 16, top, title, size=16, weight=600, mono=mono)
    for i, line in enumerate(lines):
        cv.text(x + 16, top + 20 * (i + 1), line, size=13.5, fill=c["muted"])


def lane(cv, y, label, sub, color, x0, x1):
    c = cv.c
    cv.arrow(f"M{x0 + 2},{y} L{x1 - 2},{y}", color=color, width=2, both=True)
    mid = (x0 + x1) / 2
    width = max(len(label) * 8.6, len(sub) * 7.2) + 24
    cv.rect(mid - width / 2, y - 28, width, 56, c["box"], color, r=10, width=1.4)
    cv.text(mid, y - 6, label, size=15, weight=700, fill=color, anchor="middle")
    cv.text(mid, y + 14, sub, size=13, fill=c["muted"], anchor="middle")


def architecture(theme):
    W, H = 1200, 556
    cv = Canvas(theme, W, H, "The LSW manager, the window mirrors, QEMU with accel-virt and virtiofsd on the Linux "
                             "host, linked to the guest agent, the mirrors' guest halves, the GPU drivers and the "
                             "shared folders in Windows")
    c = cv.c
    hx, hw, gx, gw = 16, 400, 784, 400
    top, bottom = 16, 540
    cv.rect(hx, top, hw, bottom - top, c["host_fill"], c["host_line"], r=14)
    cv.rect(gx, top, gw, bottom - top, c["guest_fill"], c["guest_line"], r=14)
    cv.text(hx + 20, top + 32, "Linux host", size=18, weight=700, fill=c["ok"])
    cv.text(hx + hw - 20, top + 32, "your user, no root", size=12, fill=c["muted"], anchor="end", italic=True)
    cv.text(gx + 20, top + 32, "Windows 11 VM", size=18, weight=700, fill=c["accent"])
    cv.text(gx + gw - 20, top + 32, "libvirt session, QEMU/KVM", size=12, fill=c["muted"], anchor="end", italic=True)

    bx, bw = hx + 20, hw - 40
    rows = [112, 230, 348, 490]
    box(cv, bx, rows[0] - 44, bw, 88, "lsw-manager",
        ["installs Windows, runs the VM, lists apps,", "opens files, keeps Windows' side current"], c["ok"])
    box(cv, bx, rows[1] - 44, bw, 88, "remotewin",
        ["one per app: a native Wayland window with", "its pixels, input, menus and taskbar entry"], c["ok"])
    box(cv, bx, rows[2] - 44, bw, 88, "QEMU + accel-virt",
        ["runs Direct3D, Vulkan, OpenGL and OpenCL", "on the host's own drivers"], c["ok"])
    cv.rect(bx, rows[2] + 62, bw, 40, c["gpu_fill"], r=8)
    cv.text(bx + bw / 2, rows[2] + 87, "your GPU", size=15, weight=700, fill=c["gpu_text"], anchor="middle")
    cv.arrow(f"M{bx + bw / 2},{rows[2] + 44} L{bx + bw / 2},{rows[2] + 60}")
    box(cv, bx, rows[3] - 30, bw, 60, "virtiofsd", ["one per shared folder"], c["ok"])
    cv.arrow(f"M{bx + bw - 30},{rows[2] - 46} L{bx + bw - 30},{rows[1] + 46}", color=c["ok"], width=1.6, dash="4 3")
    cv.text(bx + bw - 38, rows[1] + 72, "frames stay on the GPU", size=12.5, fill=c["ok"], anchor="end", italic=True)

    gbx = gx + 20
    box(cv, gbx, rows[0] - 44, bw, 88, "lsw-guest-agent.exe",
        ["a Windows service: launches apps, reads", "the Start menu, runs LSW's updates"], c["accent"])
    box(cv, gbx, rows[1] - 44, bw, 88, "remotewin-guest.exe",
        ["beside each application: captures its", "windows and replays your input"], c["accent"])
    box(cv, gbx, rows[2] - 44, bw, 88, "avgpu.sys",
        ["a display adapter named after your GPU, with", "Direct3D, Vulkan, OpenGL and OpenCL drivers"], c["accent"])
    cv.rect(gbx, rows[2] + 62, bw, 40, c["box"], c["line"], r=8)
    cv.text(gbx + bw / 2, rows[2] + 87, "your Windows applications", size=15, weight=600, anchor="middle")
    box(cv, gbx, rows[3] - 30, bw, 60, "WinFsp", ["with virtio-win's virtio-fs driver"], c["accent"])

    x0, x1 = hx + hw, gx
    lane(cv, rows[0], "guest link", "control, apps, files, updates", c["purple"], x0, x1)
    lane(cv, rows[1], "window link", "frames, input, menus, icons", c["accent"], x0, x1)
    lane(cv, rows[2], "accel-virt", "GPU commands and shared memory", c["ok"], x0, x1)
    lane(cv, rows[3], "shared folders", "a drive letter per folder", c["warm"], x0, x1)
    return cv.finish()


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "docs" / "images"
    out.mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        (out / f"hero-{theme}.svg").write_text(hero(theme))
        (out / f"architecture-{theme}.svg").write_text(architecture(theme))
