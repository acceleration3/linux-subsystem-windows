import argparse
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import smoke_baseline as sb  # noqa: E402

KERNEL_EVENTS = re.compile(
    r"amdgpu.*(fault|timeout|reset)|\bxid\b|NVRM: Xid|i915.*(hang|reset|fence)|gpu hang|fence expiration|ring .* timeout",
    re.IGNORECASE)
PASSING = ("ok", "skip")


def load_pass(root):
    results = {}
    for path in sorted(pathlib.Path(root).rglob("result.json")):
        results[str(path.parent.relative_to(root))] = json.loads(path.read_text())
    return results


def kernel_events(text):
    return [line.strip() for line in text.splitlines() if KERNEL_EVENTS.search(line)]


def judge_pass(results):
    out = {"rows": 0, "failed": [], "not_run": [], "crashed": [], "host": [], "incomplete": [], "times": []}
    for key, result in results.items():
        if not result.get("complete", True):
            out["incomplete"].append(key)
        for crash in list(result.get("host_crashes", [])) + list(result.get("incidents", [])):
            out["host"].append(f"{key}: {crash if isinstance(crash, str) else crash.get('evidence') or crash.get('reason') or 'VM died'}")
        for check in result["checks"]:
            out["rows"] += 1
            label = f"{key}:{check['id']}"
            status = check["status"]
            if status == "not_run":
                out["not_run"].append(label)
            elif status == "crash":
                out["crashed"].append(label)
            elif status not in PASSING:
                out["failed"].append(f"{label} ({status})")
            if status == "ok":
                out["times"].append((check["id"], check.get("ms", 0)))
    return out


def degraded_rows(first, second):
    degraded = []
    for key, result in second.items():
        before = {c["id"]: c["status"] for c in first.get(key, {}).get("checks", [])}
        for check in result["checks"]:
            if before.get(check["id"]) == "ok" and check["status"] not in PASSING:
                degraded.append(f"{key}:{check['id']} ({check['status']})")
    return degraded


def missing_results(first, second, second_bits="both"):
    expected = {k for k in first if second_bits == "both" or k.rsplit("/", 1)[-1] == f"x{second_bits}"}
    return sorted(expected ^ set(second))


def verdict(passes, baseline, kernel_text, second_bits="both"):
    judged = [judge_pass(p) for p in passes]
    times = [t for j in judged for t in j["times"]]
    budget = sb.check_budget(times, baseline)
    degraded = degraded_rows(passes[0], passes[1]) if len(passes) > 1 else []
    absent = missing_results(passes[0], passes[1], second_bits) if len(passes) > 1 else []
    events = kernel_events(kernel_text)
    problems = []
    for index, j in enumerate(judged, 1):
        problems += [f"pass {index} incomplete: {x}" for x in j["incomplete"]]
        problems += [f"pass {index} QEMU crash or VM death: {x}" for x in j["host"]]
        problems += [f"pass {index} suite crash: {x}" for x in j["crashed"]]
        problems += [f"pass {index} not run: {x}" for x in j["not_run"]]
        problems += [f"pass {index} failed: {x}" for x in j["failed"]]
    problems += [f"degraded during the run: {x}" for x in degraded]
    problems += [f"result set differs between the passes: {x}" for x in absent]
    problems += [f"host GPU kernel event: {x}" for x in events]
    problems += [f"row time {r['id']}: {r['ms']:.0f} ms, baseline {r['baseline_ms']:.0f} ms, limit {r['limit_ms']:.0f} ms" for r in budget["slow_rows"]]
    if budget["median_exceeded"]:
        problems.append(f"median row time {budget['median_ms']:.0f} ms exceeds {sb.MEDIAN_FACTOR:g} x the baseline median {budget['baseline_median_ms']:.0f} ms")
    if not times and not any(j["rows"] for j in judged):
        problems.append("no rows ran")
    return {"problems": problems, "judged": judged, "budget": budget, "degraded": degraded, "kernel_events": events}


def summary(result):
    judged = result["judged"]
    budget = result["budget"]
    lines = ["smoke summary"]
    for index, j in enumerate(judged, 1):
        lines.append(f"  pass {index}: {j['rows']} rows, {len(j['failed'])} failed, {len(j['crashed'])} crashed, "
                     f"{len(j['not_run'])} not run, {len(j['host'])} VM incidents")
    slow = len(budget["slow_rows"]) + (1 if budget["median_exceeded"] else 0)
    lines.append(f"  time budget: {slow} violations ({budget['compared']} rows compared, median {budget['median_ms']:.0f} ms "
                 f"against baseline {budget['baseline_median_ms']:.0f} ms, {len(budget['unbaselined'])} rows without a baseline)")
    lines.append(f"  degraded during the run: {len(result['degraded'])}")
    lines.append(f"  host GPU kernel events: {len(result['kernel_events'])}")
    for problem in result["problems"][:25]:
        lines.append(f"  - {problem}")
    if len(result["problems"]) > 25:
        lines.append(f"  ... {len(result['problems']) - 25} more")
    lines.append("smoke: PASS" if not result["problems"] else f"smoke: FAIL ({len(result['problems'])} problems)")
    return "\n".join(lines)


def filter_for(globs, suite):
    return ",".join(g for g in globs if g.startswith(f"{suite}."))


def run_verdict(args):
    baseline = json.loads(pathlib.Path(args.baseline).read_text())
    passes = [load_pass(root) for root in args.passes]
    kernel_text = pathlib.Path(args.kernel_log).read_text(errors="replace") if args.kernel_log and pathlib.Path(args.kernel_log).exists() else ""
    result = verdict(passes, baseline, kernel_text, args.pass2_bits)
    text = summary(result)
    print(text)
    if args.summary:
        pathlib.Path(args.summary).write_text(text + "\n")
    return 1 if result["problems"] else 0


def run_filter(args):
    print(filter_for(sb.load_selection(args.selection), args.suite))
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="the smoke selection and its verdict")
    sub = parser.add_subparsers(dest="command", required=True)
    flt = sub.add_parser("filter", help="print the globs of one suite as a --filter value")
    flt.add_argument("--suite", required=True, choices=("render", "sanity"))
    flt.add_argument("--selection", default=str(sb.DEFAULT_SELECTION))
    flt.set_defaults(func=run_filter)
    ver = sub.add_parser("verdict", help="judge the result trees of the smoke passes")
    ver.add_argument("--pass", dest="passes", action="append", required=True)
    ver.add_argument("--baseline", default=str(sb.DEFAULT_BASELINE))
    ver.add_argument("--kernel-log")
    ver.add_argument("--summary")
    ver.add_argument("--pass2-bits", choices=("64", "32", "both"), default="both")
    ver.set_defaults(func=run_verdict)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
