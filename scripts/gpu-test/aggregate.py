#!/usr/bin/env python3
"""One run's failures across every host and GPU, from what gpu-test.sh
collected into build/gpu-test/RUN/<host>/.

  aggregate.py build/gpu-test/RUN

Writes RUN/failures.md and RUN/summary.json and prints the markdown. A case
failing on every GPU tested is listed once under "On every GPU" - a bug in
the common path - and the rest under the GPUs they failed on.
"""
import collections
import json
import pathlib
import sys

import trace_judge


def gpu_results(run):
    passes, stopped = [], []
    for host_dir in sorted(path for path in run.iterdir() if path.is_dir()):
        found = sorted(host_dir.glob("*/results.json"))
        for path in found:
            passes.append((host_dir.name, json.loads(path.read_text())))
        status_path = host_dir / "status.json"
        status = json.loads(status_path.read_text()) if status_path.exists() else {"state": "no status"}
        if status.get("state") != "done":
            stopped.append((host_dir.name, status))
    return passes, stopped


def label(host, result):
    return f"{host} / {result['gpu']['name']}"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    run = pathlib.Path(sys.argv[1])
    passes, stopped = gpu_results(run)
    suites = sorted({name for _, result in passes for name in result.get("suites", {})})
    lines = [f"# GPU test run {run.name}", ""]

    lines += ["## Matrix", "", "| suite | " + " | ".join(label(h, r) for h, r in passes) + " |",
              "|---|" + "---|" * len(passes)]
    for name in suites:
        cells = []
        for _, result in passes:
            verdict = result.get("suites", {}).get(name)
            if verdict is None:
                cells.append("-")
            elif verdict["status"] == "pass":
                cells.append(verdict["status"])
            else:
                cells.append(f"**{verdict['status']}** ({len(verdict.get('failures', []))})")
        lines.append(f"| {name} | " + " | ".join(cells) + " |")
    lines.append("")

    measured = [name for name in suites
                if any(result.get("suites", {}).get(name, {}).get("metrics") for _, result in passes)]
    if measured:
        lines += ["## Measurements", ""]
        for name in measured:
            keys = sorted({key for _, result in passes
                           for key in result.get("suites", {}).get(name, {}).get("metrics", {})})
            lines += [f"### {name}", "", "| | " + " | ".join(label(h, r) for h, r in passes) + " |",
                      "|---|" + "---|" * len(passes)]
            for key in keys:
                lines.append(f"| {key} | " + " | ".join(
                    str(result.get("suites", {}).get(name, {}).get("metrics", {}).get(key, "-"))
                    for _, result in passes) + " |")
            lines.append("")

    lines += trace_judge.cross_gpu_section(run)

    if stopped:
        lines += ["## Runs that did not finish", ""]
        lines += [f"- **{host}**: {status.get('state')}: {status.get('reason', '')}" for host, status in stopped]
        lines.append("")
    broken = [(host, result) for host, result in passes if result.get("error")]
    if broken:
        lines += ["## GPU passes that stopped", ""]
        lines += [f"- **{label(host, result)}**: {result['error']}" for host, result in broken]
        lines.append("")

    where = collections.defaultdict(list)
    details = {}
    for host, result in passes:
        for name, verdict in result.get("suites", {}).items():
            for failure in verdict.get("failures", []):
                key = (name, failure["case"])
                where[key].append(label(host, result))
                details.setdefault(key, {})[label(host, result)] = failure["detail"]
    tested = [label(host, result) for host, result in passes if not result.get("error")]
    everywhere = sorted(key for key, labels in where.items() if tested and set(tested) <= set(labels))
    lines += ["## On every GPU", ""]
    if everywhere:
        for key in everywhere:
            sample = next(iter(details[key].values()))
            lines.append(f"- `{key[0]}` **{key[1]}**: {sample}")
    else:
        lines.append("None.")
    lines.append("")

    lines += ["## By GPU", ""]
    for host, result in passes:
        gpu_failures = [(name, failure) for name, verdict in result.get("suites", {}).items()
                        for failure in verdict.get("failures", [])
                        if (name, failure["case"]) not in everywhere]
        lines.append(f"### {label(host, result)} (`{result['gpu']['vendor']}:{result['gpu']['device']}`)")
        lines.append("")
        if result.get("error"):
            lines.append(f"Stopped: {result['error']}")
        if not gpu_failures and not result.get("error"):
            lines.append("No failure of its own.")
        for name, failure in gpu_failures:
            lines.append(f"- `{name}` **{failure['case']}**: {failure['detail']}")
        lines.append("")

    text = "\n".join(lines)
    (run / "failures.md").write_text(text)
    (run / "summary.json").write_text(json.dumps({
        "passes": [{"host": host, "gpu": result["gpu"], "error": result.get("error"),
                    "suites": {name: {"status": verdict["status"], "failures": verdict.get("failures", []),
                                      "metrics": verdict.get("metrics", {})}
                               for name, verdict in result.get("suites", {}).items()}}
                   for host, result in passes],
        "stopped": [{"host": host, "status": status} for host, status in stopped],
        "everywhere": [{"suite": suite, "case": case} for suite, case in everywhere],
    }, indent=1))
    print(text)


if __name__ == "__main__":
    main()
