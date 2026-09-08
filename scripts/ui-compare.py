#!/usr/bin/env python3
"""Put the design and the built UI side by side, one image per screen.

The design lives in docs/design/lsw-ui.html as a set of mockup windows;
the application renders the same screens with ``lsw-manager --screenshot``.
Comparing them by memory does not work, and comparing them by opening two
windows is a thing nobody repeats. This does both captures and writes one
labelled pair per screen.

    scripts/ui-compare.py [--out DIR] [only-these-names...]

Needs firefox (headless, for the design) and ImageMagick.
"""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
DESIGN = ROOT / "docs" / "design" / "lsw-ui.html"

# Each screen: the app's screenshot basename, the design view id, and the
# size the design frame is specified at.
SCREENS = [
    ("manager-01-overview", "m-main", 1000, 700),
    ("manager-02-console", "m-console", 1000, 660),
    ("manager-03-log", "m-log", 1000, 660),
    ("manager-04-render", "m-render", 1000, 760),
    ("manager-05-settings", "m-settings", 1000, 900),
    ("wizard-01-page", "w-1", 880, 660),
    ("wizard-02-page", "w-2", 880, 620),
    ("wizard-03-page", "w-3", 880, 660),
    ("wizard-04-page", "w-4", 880, 700),
    ("wizard-05-page", "w-7", 880, 620),
    ("wizard-06-page", "w-8", 880, 660),
    ("wizard-07-page", "w-9", 880, 660),
    ("wizard-08-page", "w-10", 880, 620),
    ("dialog-1-download", "w-download", 660, 700),
    ("dialog-2-trim", "w-trim", 660, 860),
    ("dialog-3-application", "m-cfg", 600, 640),
    ("dialog-4-browse", "m-picker", 800, 620),
    ("dialog-5-starting", "m-splash", 440, 560),
]


def run(cmd, **kw):
    return subprocess.run(cmd, check=False, capture_output=True, text=True, **kw)


def capture_design(out_dir):
    """One PNG per design view, rendered headless."""
    firefox = shutil.which("firefox")
    if firefox is None:
        sys.exit("firefox is needed to render the design")
    done = []
    for name, view, width, height in SCREENS:
        target = out_dir / f"{name}.png"
        with tempfile.TemporaryDirectory() as profile:
            run([firefox, "--headless", "--profile", profile,
                 "--window-size", f"{width},{height}",
                 "--screenshot", str(target),
                 f"file://{DESIGN}?shot={view}"])
        if target.exists():
            done.append(name)
        else:
            print(f"  design capture failed: {name}", file=sys.stderr)
    return done


def capture_app(out_dir):
    binary = ROOT / "build" / "src" / "host" / "lsw-manager"
    if not binary.exists():
        sys.exit(f"build the manager first: {binary} is not there")
    result = run(["dbus-run-session", "--", "env", "QT_QPA_PLATFORM=offscreen",
                  str(binary), "--screenshot", str(out_dir)])
    if not any(out_dir.glob("*.png")):
        sys.exit(f"the manager wrote no screenshots:\n{result.stderr[-2000:]}")


def compose(design_dir, app_dir, out_dir, wanted):
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name, _view, _w, _h in SCREENS:
        if wanted and name not in wanted:
            continue
        design = design_dir / f"{name}.png"
        app = app_dir / f"{name}.png"
        if not design.exists() or not app.exists():
            continue
        target = out_dir / f"{name}.png"
        # Same height, a gap between, and a label over each half so the
        # pair is readable on its own.
        run(["magick",
             "(", str(design), "-resize", "x760>", "-background", "#0b0c10",
             "-gravity", "north", "-splice", "0x28",
             "-fill", "#8b93a4", "-pointsize", "16", "-annotate", "+8+20", "design", ")",
             "(", str(app), "-resize", "x760>", "-background", "#0b0c10",
             "-gravity", "north", "-splice", "0x28",
             "-fill", "#ff9f45", "-pointsize", "16", "-annotate", "+8+20", "built", ")",
             "-background", "#000000", "-gravity", "north", "+append",
             str(target)])
        if target.exists():
            written.append(target)
    return written


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", default=None, help="where the pairs go")
    parser.add_argument("names", nargs="*", help="only these screens")
    args = parser.parse_args()

    out_dir = pathlib.Path(args.out) if args.out else ROOT / "build" / "ui-compare"
    work = out_dir / "raw"
    design_dir = work / "design"
    app_dir = work / "app"
    for path in (design_dir, app_dir):
        path.mkdir(parents=True, exist_ok=True)

    print("rendering the design")
    capture_design(design_dir)
    print("rendering the application")
    capture_app(app_dir)
    print("composing")
    pairs = compose(design_dir, app_dir, out_dir, set(args.names))
    for path in pairs:
        print(f"  {path}")
    print(f"{len(pairs)} pairs in {out_dir}")


if __name__ == "__main__":
    main()
