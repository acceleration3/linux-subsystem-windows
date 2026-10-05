#!/usr/bin/env python3
"""Summarize RWIN_TIMING_DIR CSVs (stdlib only).

Capture timestamps measure WGC's source cadence; local timestamps measure
client delivery/draw/swap cadence. Guest and host clocks are NOT synchronized.
QOpenGLWidget swaps are submission/completion notifications, not proof of scanout.
RWIN_TIMING_PIXELS=1 samples a diagnostic colour counter with GPU readback;
use a separate run because that readback changes the workload.
--check-pixels requires the WGC probe's animated modulo-32 red counter, not
arbitrary application content. It fails on repeated or backward counters.
"""

import argparse
import bisect
import csv
import json
import pathlib
import statistics
import sys


def distribution(values):
    if not values:
        return {}
    ordered = sorted(values)
    return {
        "count": len(values), "mean_ms": statistics.mean(values),
        "p50_ms": statistics.median(values),
        "p95_ms": ordered[int((len(ordered) - 1) * .95)],
        "p99_ms": ordered[int((len(ordered) - 1) * .99)],
        "max_ms": ordered[-1], "over_25ms": sum(value > 25 for value in values),
        "under_8ms": sum(value < 8 for value in values),
    }


def intervals(rows, key="local_us", divisor=1000):
    values = [row[key] for row in rows]
    answer = distribution([(b - a) / divisor for a, b in zip(values, values[1:])])
    if len(values) > 1 and values[-1] > values[0]:
        answer["fps"] = (len(values) - 1) * divisor * 1000 / (values[-1] - values[0])
    answer["non_monotonic"] = sum(b <= a for a, b in zip(values, values[1:]))
    return answer


def summarize(path, trim_seconds):
    with path.open() as file:
        rows = list(csv.DictReader(file))
    for row in rows:
        for key in ("local_us", "frame", "source_100ns", "detail"):
            row[key] = int(row[key])
    overflow = sum(row["detail"] for row in rows if row["event"] == "overflow")
    rows = [row for row in rows if row["event"] != "overflow"]
    if not rows:
        return {"overflow": overflow}
    begin = rows[0]["local_us"] + trim_seconds * 1e6
    end = rows[-1]["local_us"] - trim_seconds * 1e6
    rows = [row for row in rows if begin <= row["local_us"] <= end]
    answer = {"overflow": overflow, "events": {}}
    if path.name.startswith("feed-"):
        windows = sorted({row["detail"] for row in rows})
        if len(windows) > 1:
            answer["warning"] = "Multiple windows: aggregate intervals are not per-window FPS."
    if path.name.startswith("guest-"):
        windows = sorted({row["frame"] for row in rows})
        if len(windows) > 1:
            answer["warning"] = "Multiple windows: aggregate intervals are not per-window FPS."
    for event in dict.fromkeys(row["event"] for row in rows):
        selected = [row for row in rows if row["event"] == event]
        answer["events"][event] = intervals(selected)
        answer["events"][event]["frames"] = len(selected)
        if event == "capture":
            answer["wgc_source"] = intervals(selected, "source_100ns", 10000)
            answer["drained_frames"] = sum(row["detail"] for row in selected)
        if event in ("draw", "swap", "pixel"):
            unique = []
            for row in selected:
                if not unique or row["frame"] != unique[-1]["frame"]:
                    unique.append(row)
            answer["events"][event]["unique"] = intervals(unique)
            answer["events"][event]["redrawn"] = len(selected) - len(unique)
            answer["events"][event]["order_backsteps"] = sum(
                b["frame"] < a["frame"] for a, b in zip(unique, unique[1:]))
            if event == "pixel":
                steps = [round(row["detail"] / 8) % 32 for row in unique]
                moves = [(b - a) % 32 for a, b in zip(steps, steps[1:])]
                answer["colour_counter"] = {
                    "one_step": moves.count(1), "repeated": moves.count(0),
                    "forward_skips": sum(1 < move < 16 for move in moves),
                    "suspected_backsteps": sum(move >= 16 for move in moves),
                    "note": "Modulo-32 counter: a >16-frame stall can resemble a backstep.",
                }
    for first, last in (("received", "ready"), ("receive", "draw"),
                        ("draw", "swap"), ("copy_begin", "copy_end"),
                        ("copy_end", "send_end"), ("syscall_begin", "syscall_end"),
                        ("writable_begin", "writable_end"), ("send_begin", "send_end")):
        pending = {}
        elapsed = []
        for row in rows:
            key = (row["frame"], row["source_100ns"])
            if row["event"] == first:
                pending[key] = row["local_us"]
            elif row["event"] == last and key in pending:
                elapsed.append((row["local_us"] - pending.pop(key)) / 1000)
        if elapsed:
            answer[first + "_to_" + last] = distribution(elapsed)
    receives = [row for row in rows if row["event"] == "receive"]
    draws = [row for row in rows if row["event"] == "draw"]
    if receives and draws:
        shown = {row["frame"] for row in draws}
        answer["received_not_drawn"] = sum(row["frame"] not in shown for row in receives)
        received_times = [row["local_us"] for row in receives]
        gaps = sorted(zip(draws, draws[1:]),
                      key=lambda pair: pair[1]["local_us"] - pair[0]["local_us"], reverse=True)
        answer["largest_draw_gaps"] = []
        for first, last in gaps[:5]:
            lo = bisect.bisect_right(received_times, first["local_us"])
            hi = bisect.bisect_left(received_times, last["local_us"])
            answer["largest_draw_gaps"].append({
                "duration_ms": (last["local_us"] - first["local_us"]) / 1000,
                "received_during_gap": hi - lo,
                "receive_visibility_flags": sorted({row["detail"] for row in receives[lo:hi]}),
            })
    return answer


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=pathlib.Path)
    parser.add_argument("--trim", type=float, default=2,
                        help="discard startup and teardown seconds at each end")
    parser.add_argument("--check-pixels", action="store_true",
                        help="fail if the animated WGC probe counter repeats or moves backward")
    args = parser.parse_args()
    summary = {str(path.relative_to(args.directory)): summarize(path, args.trim)
               for path in sorted(args.directory.rglob("*.csv"))}
    print(json.dumps(summary, indent=2))
    if args.check_pixels:
        counters = [trace["colour_counter"] for trace in summary.values() if "colour_counter" in trace]
        if not counters:
            parser.error("no pixel-counter samples; run the WGC probe with RWIN_TIMING_PIXELS=1")
        if any(counter["repeated"] or counter["suspected_backsteps"] for counter in counters):
            print("FAIL: animated pixel counter repeated or moved backward", file=sys.stderr)
            sys.exit(1)
        if any(trace["overflow"] for trace in summary.values()):
            print("FAIL: timing recording overflowed", file=sys.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
