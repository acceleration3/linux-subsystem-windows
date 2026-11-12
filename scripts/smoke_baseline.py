import argparse
import fnmatch
import json
import pathlib
import statistics
import sys

HERE = pathlib.Path(__file__).resolve().parent
DEFAULT_BASELINE = HERE / "smoke-baseline.json"
DEFAULT_SELECTION = HERE / "smoke-selection.txt"
MEDIAN_FACTOR = 2.0
ROW_FACTOR = 5.0
ROW_SLACK_MS = 3000.0


def load_selection(path=DEFAULT_SELECTION):
    globs = []
    for line in pathlib.Path(path).read_text().splitlines():
        line = line.strip()
        if line:
            globs.append(line)
    return globs


def selected(globs, check_id):
    return any(fnmatch.fnmatchcase(check_id, glob) for glob in globs)


def ok_times(paths, globs=None):
    times = {}
    for path in paths:
        for check in json.loads(pathlib.Path(path).read_text())["checks"]:
            if check["status"] != "ok" or check.get("ms") is None:
                continue
            if globs is not None and not selected(globs, check["id"]):
                continue
            times.setdefault(check["id"], []).append(float(check["ms"]))
    return times


def build(paths, globs=None):
    times = ok_times(paths, globs)
    return {"rows": {check_id: statistics.median(values) for check_id, values in sorted(times.items())}}


def row_limit(baseline_ms):
    return max(ROW_FACTOR * baseline_ms, baseline_ms + ROW_SLACK_MS)


def check_budget(rows, baseline):
    known = baseline["rows"]
    measured = [(check_id, float(ms)) for check_id, ms in rows if check_id in known]
    unbaselined = sorted({check_id for check_id, _ in rows if check_id not in known})
    slow = []
    for check_id, ms in measured:
        limit = row_limit(known[check_id])
        if ms > limit:
            slow.append({"id": check_id, "ms": ms, "baseline_ms": known[check_id], "limit_ms": limit})
    median_now = statistics.median(ms for _, ms in measured) if measured else 0.0
    median_base = statistics.median(known[check_id] for check_id, _ in measured) if measured else 0.0
    median_slow = bool(measured) and median_now > MEDIAN_FACTOR * median_base
    return {"slow_rows": slow, "median_ms": median_now, "baseline_median_ms": median_base,
            "median_exceeded": median_slow, "unbaselined": unbaselined, "compared": len(measured)}


def update(args):
    globs = None if args.all_rows else load_selection(args.selection)
    baseline = build(args.results, globs)
    if not baseline["rows"]:
        print("smoke_baseline: no ok rows in the given results", file=sys.stderr)
        return 1
    pathlib.Path(args.out).write_text(json.dumps(baseline, indent=0, sort_keys=True) + "\n")
    print(f"smoke_baseline: {len(baseline['rows'])} rows written to {args.out}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="per-row time baseline of the smoke selection")
    sub = parser.add_subparsers(dest="command", required=True)
    up = sub.add_parser("update", help="write the baseline from result.json files (rows with status ok only)")
    up.add_argument("results", nargs="+")
    up.add_argument("--out", default=str(DEFAULT_BASELINE))
    up.add_argument("--selection", default=str(DEFAULT_SELECTION))
    up.add_argument("--all-rows", action="store_true", help="keep every ok row, not only the smoke selection")
    up.set_defaults(func=update)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
