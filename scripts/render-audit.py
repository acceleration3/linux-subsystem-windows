#!/usr/bin/env python3
import argparse
import collections
import json
import os
import sys

NEAR_DISTINCT = 4
NEAR_STDDEV_INT = 0.5
NEAR_STDDEV_FLOAT = 0.002
FRAME_STATIC = 0.005
TRIVIAL = 0.98
LOOSE = ("psnr", "filter", "raster")
FLOAT_KINDS = ("rgba16f", "rgba32f", "r32f", "depth32f", "r16f", "rg16f", "rg32f")


def api_of(check_id):
    parts = check_id.split(".")
    return parts[1] if len(parts) > 1 else "?"


def family_of(check_id):
    head, _, last = check_id.rpartition(".")
    tokens = last.split("_")
    if len(tokens) < 2:
        return None
    return head + "." + "_".join(tokens[:-1])


def load_dumps(root):
    found = {}
    if not root:
        return found
    for base, _, names in os.walk(root):
        if "captures.json" not in names:
            continue
        try:
            with open(os.path.join(base, "captures.json"), encoding="utf-8") as handle:
                data = json.load(handle)
        except (OSError, ValueError) as error:
            print("render-audit: cannot read %s: %s" % (base, error), file=sys.stderr)
            continue
        if "id" in data:
            found[data["id"]] = data
    return found


def load_results(path):
    if not path:
        return {}
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    return {c["id"]: c for c in data.get("checks", []) if str(c.get("id", "")).startswith("render.")}


def key_of(capture):
    return (capture["frame"], capture["label"])


def is_float(kind):
    return kind in FLOAT_KINDS


def near_uniform(stats, kind):
    if stats["distinct"] <= NEAR_DISTINCT:
        return True
    return stats["stddev"] < (NEAR_STDDEV_FLOAT if is_float(kind) else NEAR_STDDEV_INT)


def side_hashes(dump, field):
    out = {}
    for capture in dump.get("captures", []):
        if field in capture:
            out[key_of(capture)] = capture[field]
    return out


def group_identical(table):
    groups = collections.defaultdict(list)
    for check_id, hashes in table.items():
        if hashes:
            groups[(api_of(check_id), tuple(sorted(hashes.items())))].append(check_id)
    return [sorted(ids) for ids in groups.values() if len(ids) > 1]


class Audit:
    def __init__(self, guest, host, results):
        self.guest = guest
        self.host = host
        self.results = results
        self.flags = collections.defaultdict(list)
        self.groups = []
        self.ids = sorted(set(guest) | set(host) | set(results))

    def flag(self, check_id, name, detail):
        self.flags[check_id].append({"flag": name, "detail": detail})

    def uniform(self):
        for source, dumps, stat_field in (("device", self.guest, "actual_stats"), ("oracle", self.guest, "oracle_stats"), ("host", self.host, "actual_stats")):
            for check_id, dump in dumps.items():
                for capture in dump.get("captures", []):
                    stats = capture.get(stat_field)
                    if stats is None:
                        continue
                    name = "%s f%d %s" % (source, capture["frame"], capture["label"] or "(unnamed)")
                    differing = 1.0 - stats["dominant_fraction"]
                    if stats["distinct"] == 1:
                        self.flag(check_id, "uniform", "%s is a single value %s" % (name, stats["dominant"]))
                    elif near_uniform(stats, capture["kind"]):
                        self.flag(check_id, "near_uniform", "%s has %d distinct values, %.3f%% of pixels differ from %s" %
                                  (name, stats["distinct"], differing * 100.0, stats["dominant"]))

    def indistinct(self):
        device = {i: side_hashes(d, "actual_hash") for i, d in self.guest.items()}
        oracle = {i: side_hashes(d, "oracle_hash") for i, d in self.guest.items()}
        for i, d in self.host.items():
            oracle.setdefault(i, side_hashes(d, "actual_hash"))
            if not oracle[i]:
                oracle[i] = side_hashes(d, "actual_hash")
        for side, table in (("device", device), ("oracle", oracle)):
            for ids in group_identical(table):
                self.groups.append({"kind": "identical_set", "side": side, "ids": ids})
                for check_id in ids:
                    self.flag(check_id, "indistinguishable_" + side, "all captures on the %s side are identical to %s" %
                              (side, ", ".join(i for i in ids if i != check_id)))
        families = collections.defaultdict(list)
        for check_id in device:
            family = family_of(check_id)
            if family:
                families[family].append(check_id)
        for family, members in sorted(families.items()):
            if len(members) < 2:
                continue
            for side, table in (("device", device), ("oracle", oracle)):
                per_key = collections.defaultdict(lambda: collections.defaultdict(list))
                for check_id in members:
                    for key, digest in table.get(check_id, {}).items():
                        per_key[key][digest].append(check_id)
                pairs = collections.defaultdict(list)
                identical = []
                for key, by_hash in sorted(per_key.items()):
                    for ids in by_hash.values():
                        if len(ids) > 1:
                            identical.append("f%d %s: %s" % (key[0], key[1] or "(unnamed)", ", ".join(sorted(ids))))
                            for check_id in ids:
                                pairs[check_id].append((key, sorted(set(ids) - {check_id})))
                if not identical:
                    continue
                self.groups.append({"kind": "family", "side": side, "family": family, "members": sorted(members), "identical": identical})
                for check_id, entries in pairs.items():
                    for key, others in entries:
                        self.flag(check_id, "family_identical_" + side, "f%d %s equals %s in family %s" %
                                  (key[0], key[1] or "(unnamed)", ", ".join(others), family))

    def multiframe(self):
        for check_id, dump in self.guest.items():
            frames = dump.get("captured", [])
            if len(frames) < 2:
                continue
            labels = collections.defaultdict(list)
            for capture in dump["captures"]:
                labels[capture["label"]].append(capture)
            worst = {}
            identical = True
            for label, captures in labels.items():
                captures.sort(key=lambda c: c["frame"])
                changes = [c["actual_stats"].get("changed_from_previous") for c in captures[1:] if "actual_stats" in c]
                changes = [c for c in changes if c is not None]
                if not changes:
                    continue
                worst[label] = max(changes)
                identical = identical and worst[label] == 0.0
            if worst and identical:
                self.flag(check_id, "frames_identical", "captured frames %s are identical in every label" % frames)
            elif worst and all(v < FRAME_STATIC for v in worst.values()):
                self.flag(check_id, "frames_nearly_identical", "captured frames differ by at most %.3f%% of pixels (%s)" %
                          (max(worst.values()) * 100.0, ", ".join("%s %.3f%%" % (k, v * 100.0) for k, v in sorted(worst.items()))))

    def trivial(self):
        for check_id, dump in self.guest.items():
            reference = {}
            if check_id in self.host:
                reference = {key_of(c): c.get("actual_stats") for c in self.host[check_id]["captures"]}
            details = []
            total = 0
            for capture in dump.get("captures", []):
                total += 1
                device = capture.get("actual_stats")
                other = capture.get("oracle_stats") or reference.get(key_of(capture))
                if device is None or other is None:
                    continue
                if device["dominant_fraction"] > TRIVIAL and other["dominant_fraction"] > TRIVIAL and device["dominant"] == other["dominant"]:
                    details.append("f%d %s (%s over %.1f%%)" % (capture["frame"], capture["label"] or "(unnamed)", device["dominant"],
                                                                min(device["dominant_fraction"], other["dominant_fraction"]) * 100.0))
            if total and len(details) == total:
                self.flag(check_id, "trivial_match", "every capture of device and reference is the clear colour: " + ", ".join(details))

    def looseness(self):
        for check_id, dump in self.guest.items():
            for capture in dump.get("captures", []):
                profile = capture.get("tolerance", {}).get("name", "")
                if not profile.startswith(LOOSE):
                    continue
                forgiven = capture.get("forgiven", 0)
                if forgiven > 0:
                    self.flag(check_id, "loose_forgiven", "f%d %s on %s forgave %d of %d pixels (bad %d, psnr %s)" %
                              (capture["frame"], capture["label"], profile, forgiven, capture.get("total", 0), capture.get("bad", 0),
                               capture.get("psnr")))

    def status_of(self, check_id):
        result = self.results.get(check_id)
        if result:
            return {"ok": "pass"}.get(result.get("status"), result.get("status", "?"))
        dump = self.guest.get(check_id)
        if dump:
            return dump.get("outcome", "?")
        return "no-result"

    def totals(self):
        out = collections.OrderedDict()
        for check_id in self.ids:
            api = api_of(check_id)
            row = out.setdefault(api, collections.Counter())
            row["registered"] += 1
            result = self.results.get(check_id, {})
            tags = result.get("tags", [])
            dump = self.guest.get(check_id) or self.host.get(check_id)
            if "multiframe" in tags or (dump and dump.get("frames", 1) > 1):
                row["multiframe"] += 1
            else:
                row["single"] += 1
            row[self.status_of(check_id)] += 1
        return out

    def reasons(self):
        skips = collections.defaultdict(list)
        fails = []
        for check_id, result in sorted(self.results.items()):
            status = result.get("status")
            reason = result.get("reason", "")
            if status == "skip":
                skips[reason].append(check_id)
            elif status in ("fail", "timeout", "crash"):
                fails.append((check_id, status, reason))
        return skips, fails

    def run(self):
        self.uniform()
        self.indistinct()
        self.multiframe()
        self.trivial()
        self.looseness()


def render(audit):
    lines = ["# Render audit", ""]
    totals = audit.totals()
    lines += ["## Totals per API", "", "| api | registered | single | multiframe | pass | fail | skip | other |", "|---|---|---|---|---|---|---|---|"]
    for api, row in totals.items():
        other = sum(v for k, v in row.items() if k not in ("registered", "single", "multiframe", "pass", "fail", "skip"))
        lines.append("| %s | %d | %d | %d | %d | %d | %d | %d |" % (api, row["registered"], row["single"], row["multiframe"], row["pass"],
                                                                    row["fail"], row["skip"], other))
    skips, fails = audit.reasons()
    lines += ["", "## Skips by reason", ""]
    if not skips:
        lines.append("none")
    for reason, ids in sorted(skips.items(), key=lambda kv: -len(kv[1])):
        lines.append("- %d x %s" % (len(ids), reason or "(no reason)"))
        for check_id in ids[:8]:
            lines.append("  - %s" % check_id)
        if len(ids) > 8:
            lines.append("  - ... %d more" % (len(ids) - 8))
    lines += ["", "## Failures", ""]
    if not fails:
        lines.append("none")
    for check_id, status, reason in fails:
        lines.append("- %s [%s]: %s" % (check_id, status, reason))
    titles = [
        ("uniform", "Uniform captures"),
        ("near_uniform", "Near-uniform captures"),
        ("indistinguishable_device", "Indistinguishable checks, device side"),
        ("indistinguishable_oracle", "Indistinguishable checks, oracle or host side"),
        ("family_identical_device", "Identical captures inside a matrix family, device side"),
        ("family_identical_oracle", "Identical captures inside a matrix family, oracle or host side"),
        ("frames_identical", "Multi-frame checks whose frames are all identical"),
        ("frames_nearly_identical", "Multi-frame checks whose frames differ by under 0.5% of pixels"),
        ("trivial_match", "Trivial device and oracle match (both are the clear colour)"),
        ("loose_forgiven", "Loose tolerance with forgiven pixels"),
    ]
    for flag, title in titles:
        rows = [(i, f["detail"]) for i in sorted(audit.flags) for f in audit.flags[i] if f["flag"] == flag]
        lines += ["", "## %s (%d)" % (title, len(rows)), ""]
        if not rows:
            lines.append("none")
        for check_id, detail in rows:
            lines.append("- %s: %s" % (check_id, detail))
    audited = set(audit.guest) | set(audit.host)
    lines += ["", "## Summary", "", "%d checks with captures audited, %d with at least one flag." % (len(audited), len(audit.flags)), ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Audit render suite captures")
    parser.add_argument("--guest", required=True, help="guest artifacts tree produced with lsw-suites --dump-captures")
    parser.add_argument("--host", help="reference directory produced by lsw-render-ref --png")
    parser.add_argument("--json", help="lsw-suites result JSON")
    parser.add_argument("--out", help="markdown report path (default stdout)")
    parser.add_argument("--flags", help="machine-readable JSON path (default <out>.json, or render-audit.json)")
    args = parser.parse_args()
    audit = Audit(load_dumps(args.guest), load_dumps(args.host), load_results(args.json))
    audit.run()
    report = render(audit)
    if args.out:
        with open(args.out, "w", encoding="utf-8") as handle:
            handle.write(report)
    else:
        sys.stdout.write(report)
    skips, fails = audit.reasons()
    machine = {
        "totals": {api: dict(row) for api, row in audit.totals().items()},
        "checks": {i: {"status": audit.status_of(i), "flags": audit.flags.get(i, [])} for i in audit.ids},
        "groups": audit.groups,
        "skips": {reason: ids for reason, ids in skips.items()},
        "failures": [{"id": i, "status": s, "reason": r} for i, s, r in fails],
    }
    target = args.flags or ((args.out.rsplit(".", 1)[0] + ".json") if args.out else "render-audit.json")
    with open(target, "w", encoding="utf-8") as handle:
        json.dump(machine, handle, indent=1, sort_keys=True)


if __name__ == "__main__":
    main()
