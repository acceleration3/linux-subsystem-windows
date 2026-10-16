#!/usr/bin/env python3
"""The Resolve suite's captures (TRACES.md, "The Resolve suite"): each of
DaVinci Resolve's features below recorded in one Resolve session, its OpenCL
through CLIntercept (system mode) as resolve/<feature>-cl and its OpenGL
through apitrace's opengl32.dll wrapper as resolve/<feature>-gl, then both
pulled into the store with what was done as their `how`.

  resolve_captures.py list
  resolve_captures.py capture FEATURE [FEATURE...] [--no-pull]

Runs where the VM is (the builder's lsw-manager VM, or a host's under
gpu_matrix.py hold), with desk.py's elevated helper started (desk.py start):
the capture is started elevated (system mode swaps System32's OpenCL.dll,
and Resolve under the wrapper runs elevated, so its input must be sent
elevated too). The VM needs the project resolve-setup.lua builds, graded by
resolve-grade.lua, from the media resolve-media.sh makes; the Lua scripts in
C:\\av\\apps. Coordinates are those of Resolve 21.1 maximised on the VM's
2560x1440 desktop at 100%.
"""
import argparse
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from desk import Desk  # noqa: E402

RESOLVE = "C:\\Program Files\\Blackmagic Design\\DaVinci Resolve\\Resolve.exe"
KERNEL_CACHE = "%APPDATA%\\Blackmagic Design\\DaVinci Resolve\\Support\\DVIP\\Cache\\OpenCL.cache"
PROJECT_TILE = (1046, 420)
"""lsw-mimic in the project manager, the only project there."""
RENDER_DIR = "C:\\av\\media\\resolve\\render"

# Where things are, in desktop pixels.
VIEWER = {"media": (1240, 380), "edit": (2180, 400), "color": (1440, 313), "fusion": (1290, 400),
          "fairlight": (1280, 300), "deliver": (1600, 400)}
PRIMARIES_PALETTE = (295, 772)
OFFSET_WHEEL = (1165, 1096)
GAMMA_WHEEL = (500, 1096)
CONTRAST_FIELD = (788, 888)
SCOPE_MENU = (2383, 833)
SCOPE_ITEMS = {"Parade": (2343, 888), "Waveform": (2363, 928), "Vectorscope": (2375, 968),
               "Histogram": (2367, 1008), "CIE Chromaticity": (2400, 1048)}
EDIT_RULER_Y = 848
EDIT_RULER_X = (1400, 2100)


def play(page, seconds):
    """Focus the page's viewer and play for `seconds`."""
    x, y = VIEWER[page]
    return [f"Click {x} {y}", "Start-Sleep -Milliseconds 500", "Press space",
            f"Start-Sleep -Milliseconds {int(seconds * 1000)}", "Press space"]


def scrub():
    """The Edit page's playhead dragged along the ruler and back."""
    left, right = EDIT_RULER_X
    return [f"Drag {left} {EDIT_RULER_Y} {right} {EDIT_RULER_Y} 60 40",
            f"Drag {right} {EDIT_RULER_Y} {left + 100} {EDIT_RULER_Y} 60 40"]


def serial_nodes():
    """Three serial nodes graded live: offset, contrast, gamma."""
    px, py = PRIMARIES_PALETTE
    ox, oy = OFFSET_WHEEL
    gx, gy = GAMMA_WHEEL
    cx, cy = CONTRAST_FIELD
    return [f"Click {px} {py}", "Start-Sleep 1", f"Drag {ox} {oy} {ox + 25} {oy - 21}", "Start-Sleep 1",
            "Press alt+s", "Start-Sleep 1", f"DClick {cx} {cy}", "Press ctrl+a", "Type-Text '1.500'", "Press enter",
            "Start-Sleep 1", "Press alt+s", "Start-Sleep 1", f"Drag {gx} {gy} {gx - 30} {gy + 24}", "Start-Sleep 2"]


def scopes():
    x, y = SCOPE_MENU
    steps = []
    for name in ("Waveform", "Vectorscope", "Histogram", "CIE Chromaticity", "Parade"):
        ix, iy = SCOPE_ITEMS[name]
        steps += [f"Click {x} {y}", "Start-Sleep -Milliseconds 800", f"Click {ix} {iy}", "Start-Sleep 2"]
    return steps


RENDER_LUA = ("local p=resolve:GetProjectManager():GetCurrentProject(); p:DeleteAllRenderJobs(); "
              "assert(p:SetCurrentRenderFormatAndCodec([[mov]], [[ProRes422]])); "
              f"assert(p:SetRenderSettings({{SelectAllFrames=true, TargetDir=[[{RENDER_DIR}]], CustomName=[[lsw-mimic-render]]}})); "
              "local job=assert(p:AddRenderJob()); assert(p:StartRendering(job))")
RESET_GRADE_LUA = ("local p=resolve:GetProjectManager():GetCurrentProject(); "
                   "p:GetCurrentTimeline():GetItemListInTrack([[video]],1)[1]:GetNodeGraph():ResetAllGrades(); "
                   "resolve:GetProjectManager():SaveProject()")

COLOR_PLAY = play("color", 3)

FEATURES = {
    "media": dict(timeline="edit", page="media", clip="pattern.mp4", steps=play("media", 4),
                  title="the Media page: pattern.mp4 (H.264) selected in the media pool and played 4 s in its viewer",
                  how="pattern.mp4 selected in the media pool (LSW_CLIP), its viewer clicked, Space, 4 s, Space"),
    "edit": dict(timeline="edit", page="edit", steps=play("edit", 4) + scrub(),
                 title="the Edit page: a timeline of four cuts (H.264, ProRes, DNxHR, PNG) over a WAV played 4 s, "
                       "then scrubbed along the ruler and back",
                 how="timeline edit, record viewer clicked, Space, 4 s, Space; the ruler dragged from x 1400 to 2100 "
                     "and back to 1500 (60 steps each)"),
    "dissolve": dict(timeline="dissolve", page="edit", steps=play("edit", 6),
                     title="the Edit page: a 1 s Cross Dissolve between H.264 and DNxHR clips played through",
                     how="timeline dissolve, record viewer clicked, Space, 6 s (the whole timeline), Space"),
    "title": dict(timeline="title", page="edit", steps=play("edit", 4),
                  title="the Edit page: a Text+ title (Fusion) under an H.264 clip composited in Screen mode, played 4 s",
                  how="timeline title, record viewer clicked, Space, 4 s, Space"),
    "grade": dict(timeline="grade", page="color", steps=COLOR_PLAY,
                  title="the Color page: a primary grade (lift/gamma/gain wheels, contrast, custom S curve) on DNxHR, "
                        "played 3 s with the parade scope",
                  how="timeline grade (graded from resolve-grades/grade.drx), Color page viewer clicked, Space, 3 s, Space"),
    "nodes": dict(timeline="nodes", page="color", steps=serial_nodes() + COLOR_PLAY, after=RESET_GRADE_LUA,
                  title="the Color page: three serial nodes added and graded live (offset wheel, contrast 1.5, gamma "
                        "wheel), then played 3 s",
                  how="timeline nodes, primaries palette; offset wheel dragged; Alt+S; contrast typed 1.500; Alt+S; "
                      "gamma wheel dragged; viewer clicked, Space, 3 s, Space; the grade reset and saved after"),
    "qualifier": dict(timeline="qualifier", page="color", steps=COLOR_PLAY,
                      title="the Color page: an HSL qualifier (a yellow bar) and a circular power window with gain and "
                            "offset inside, on H.264, played 3 s",
                      how="timeline qualifier (graded from resolve-grades/qualifier.drx), viewer clicked, Space, 3 s, Space"),
    "fx-blur": dict(timeline="fx-blur", page="color", steps=COLOR_PLAY,
                    title="the Color page: Resolve FX Gaussian Blur on a node, played 3 s",
                    how="timeline fx-blur (resolve-grades/fx-blur.drx), viewer clicked, Space, 3 s, Space"),
    "fx-glow": dict(timeline="fx-glow", page="color", steps=COLOR_PLAY,
                    title="the Color page: Resolve FX Glow on a node, played 3 s",
                    how="timeline fx-glow (resolve-grades/fx-glow.drx), viewer clicked, Space, 3 s, Space"),
    "fx-vignette": dict(timeline="fx-vignette", page="color", steps=COLOR_PLAY,
                        title="the Color page: Resolve FX Vignette on a node, played 3 s",
                        how="timeline fx-vignette (resolve-grades/fx-vignette.drx), viewer clicked, Space, 3 s, Space"),
    "fx-sharpen": dict(timeline="fx-sharpen", page="color", steps=COLOR_PLAY,
                       title="the Color page: the Blur palette's Sharpen (radius lowered) on a node, played 3 s",
                       how="timeline fx-sharpen (resolve-grades/fx-sharpen.drx), viewer clicked, Space, 3 s, Space"),
    # Not while playing: on avgpu, playback with the scopes showing made Resolve register a new GL texture for
    # OpenCL every frame (its GLsyncs stop answering past 256 per context) and hit "Your GPU memory is full".
    "scopes": dict(timeline="grade", page="color", steps=scopes(),
                   title="the Color page's scopes over the graded clip's first frame: waveform, vectorscope, "
                         "histogram, CIE chromaticity, parade",
                   how="timeline grade, the playhead at its start, the scope menu set to Waveform, Vectorscope, "
                       "Histogram, CIE Chromaticity and Parade, 2 s each"),
    "fusion": dict(timeline="fusion", page="fusion", steps=play("fusion", 4),
                   title="the Fusion page: Background -> Transform -> Merge over a ProRes clip (MediaIn -> MediaOut), "
                         "played 4 s",
                   how="timeline fusion (its comp made by resolve-setup.lua), Fusion page viewer clicked, Space, 4 s, Space"),
    "fairlight": dict(timeline="edit", page="fairlight", steps=play("fairlight", 4),
                      title="the Fairlight page: the edit timeline's WAV track and video played 4 s",
                      how="timeline edit, Fairlight page, its viewer area clicked, Space, 4 s, Space"),
    "deliver": dict(timeline="dissolve", page="deliver", lua=RENDER_LUA, steps=["Start-Sleep 45"],
                    title="the Deliver page: the dissolve timeline (148 frames) rendered to ProRes 422 in a .mov",
                    how=f"timeline dissolve, Deliver page; a render job (mov, ProRes422, all frames, into {RENDER_DIR}) "
                        "added and started by the scripting API (RENDER_LUA in resolve_captures.py); 45 s waited"),
}


def say(line):
    print(f"[{time.strftime('%H:%M:%S')}] {line}", flush=True)


def wait_window(desk, title, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if title in desk.run("Windows"):
            return True
        time.sleep(3)
    return False


def lua(desk, code):
    quoted = code.replace("'", "''")
    return desk.run(f"Resolve-Lua '{quoted}' $false", 300)


def traces(*arguments):
    say("traces.py " + " ".join(arguments))
    subprocess.run([sys.executable, str(HERE / "traces.py"), *arguments], check=True)


def capture(desk, name, pull):
    feature = FEATURES[name]
    cl, gl = f"resolve/{name}-cl", f"resolve/{name}-gl"
    if name == "deliver":
        desk.run(f"Remove-Item -Recurse -Force '{RENDER_DIR}' -ErrorAction SilentlyContinue; "
                 f"New-Item -ItemType Directory -Force '{RENDER_DIR}' | Out-Null")
    traces("capture", cl, "--tool", "clintercept", "--mode", "system", "--exe", RESOLVE, "--gl-trace", gl,
           "--clear", KERNEL_CACHE, "--repack", "--seconds", "900", "--elevated")
    if not wait_window(desk, "Project Manager", 240):
        raise SystemExit("Resolve's project manager never showed")
    time.sleep(3)
    desk.run(f"DClick {PROJECT_TILE[0]} {PROJECT_TILE[1]}")
    if not wait_window(desk, "DaVinci Resolve - lsw-mimic", 180):
        raise SystemExit("lsw-mimic never opened")
    time.sleep(6)
    go = f"LSW_TIMELINE=[[{feature['timeline']}]]; LSW_PAGE=[[{feature['page']}]]; "
    if feature.get("clip"):
        go += f"LSW_CLIP=[[{feature['clip']}]]; "
    say(lua(desk, go + "dofile([[C:\\av\\apps\\resolve-go.lua]])").strip())
    time.sleep(8)
    if feature.get("lua"):
        say(lua(desk, feature["lua"]).strip())
    say(desk.run("Focus Resolve").strip())
    for step in feature["steps"]:
        desk.run(step, 300)
    time.sleep(2)
    if feature.get("after"):
        lua(desk, feature["after"])
        time.sleep(3)
    lua(desk, "resolve:Quit()")
    if name == "deliver":
        say(desk.run(f"Get-ChildItem '{RENDER_DIR}' | % {{ \"$($_.Name) $($_.Length)\" }}").strip())
    if not pull:
        return
    how = ("Resolve 21.1 started elevated by resolve_captures.py (CLIntercept system mode, apitrace's "
           "opengl32.dll beside Resolve.exe, its OpenCL kernel cache deleted first so sources are recorded); "
           "lsw-mimic double-clicked in the project manager; in its console: resolve-go.lua with "
           f"LSW_TIMELINE={feature['timeline']} LSW_PAGE={feature['page']}"
           + (f" LSW_CLIP={feature['clip']}" if feature.get("clip") else "") + "; then: " + feature["how"]
           + "; resolve:Quit() in the console. Media: resolve-media.sh; project: resolve-setup.lua and "
             "resolve-grade.lua (scripts/gpu-test)")
    traces("pull", cl, "--wait", "300", "--title", f"DaVinci Resolve 21.1 OpenCL: {feature['title']}", "--how", how)
    traces("pull", gl, "--wait", "300", "--title", f"DaVinci Resolve 21.1 OpenGL: {feature['title']}", "--how", how)
    if name == "deliver":
        desk.run(f"Remove-Item -Recurse -Force '{RENDER_DIR}' -ErrorAction SilentlyContinue")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list")
    run = commands.add_parser("capture")
    run.add_argument("features", nargs="+", choices=sorted(FEATURES))
    run.add_argument("--no-pull", action="store_true")
    args = parser.parse_args()
    if args.command == "list":
        for name, feature in FEATURES.items():
            print(f"{name:12} {feature['timeline']:12} {feature['page']:10} {feature['title']}")
        return 0
    desk = Desk()
    for name in args.features:
        say(f"capturing {name}")
        capture(desk, name, not args.no_pull)
    return 0


if __name__ == "__main__":
    sys.exit(main())
