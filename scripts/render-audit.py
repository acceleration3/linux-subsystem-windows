#!/usr/bin/env python3
import argparse
import array
import collections
import json
import math
import os
import struct
import sys

NEAR_DISTINCT = 4
NEAR_STDDEV_INT = 0.5
NEAR_STDDEV_FLOAT = 0.002
FRAME_STATIC = 0.005
TRIVIAL = 0.98
LOOSE = ("psnr", "filter", "raster")
FLOAT_KINDS = ("rgba16f", "rgba32f", "r32f", "depth32f", "r16f", "rg16f", "rg32f")
GATING = ("uniform", "indistinguishable_device", "indistinguishable_oracle", "family_identical_device", "family_identical_oracle",
          "cross_family_device", "cross_family_oracle", "frames_identical", "trivial_match", "swap_pass")

KINDS = [
    ("rgba8", 4, "B", "unorm8", False), ("bgra8", 4, "B", "unorm8", True), ("rgba16f", 4, "e", "float", False),
    ("rgba32f", 4, "f", "float", False), ("r32f", 1, "f", "float", False), ("depth32f", 1, "f", "float", False),
    ("rgba32ui", 4, "I", "integer", False), ("r8ui", 1, "B", "integer", False), ("r8", 1, "B", "unorm8", False),
    ("rg8", 2, "B", "unorm8", False), ("r16f", 1, "e", "float", False), ("rg16f", 2, "e", "float", False),
    ("r16", 1, "H", "unorm16", False), ("r16ui", 1, "H", "integer", False), ("rg32f", 2, "f", "float", False),
    ("rgba16", 4, "H", "unorm16", False), ("rgba8ui", 4, "B", "integer", False), ("rgba16ui", 4, "H", "integer", False),
]
KIND_ROW = {row[0]: row for row in KINDS}
FIELD_SIZE = {"B": 1, "H": 2, "e": 2, "f": 4, "I": 4}


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


class Raw:
    def __init__(self, width, height, kind, data, payload):
        self.width = width
        self.height = height
        self.kind = kind
        self.channels = KIND_ROW[kind][1]
        self.klass = KIND_ROW[kind][3]
        self.data = data
        self.payload = payload


def read_rref(path):
    with open(path, "rb") as handle:
        blob = handle.read()
    if len(blob) < 32 or blob[:4] != b"LSWR":
        raise ValueError("bad rref header in %s" % path)
    version, width, height, kind = struct.unpack_from("<IIII", blob, 4)
    if version != 1 or kind >= len(KINDS):
        raise ValueError("unsupported rref %s" % path)
    name, channels, element, klass, swapped = KINDS[kind]
    payload = blob[32:]
    count = width * height * channels
    if len(payload) != count * FIELD_SIZE[element]:
        raise ValueError("rref size mismatch in %s" % path)
    values = struct.unpack("<%d%s" % (count, element), payload)
    data = array.array("d", values)
    if swapped:
        fixed = array.array("d", data)
        fixed[0::4] = data[2::4]
        fixed[2::4] = data[0::4]
        data = fixed
    return Raw(width, height, name, data, payload)


def channel_close(klass, a, b, tol):
    a_nan = a != a
    b_nan = b != b
    if a_nan or b_nan:
        both = a_nan and b_nan
        return both, 0.0 if both else 255.0
    if a == b:
        return True, 0.0
    if math.isinf(a) or math.isinf(b):
        return False, 1e9
    delta = abs(a - b)
    if klass == "float":
        return delta <= tol["absolute"] + tol["relative"] * max(abs(a), abs(b)), delta * 255.0
    if klass == "integer":
        return False, delta
    if klass == "unorm16":
        units = delta * 255.0 / 65535.0
        return units <= tol["byte_delta"], units
    return delta <= tol["byte_delta"], delta


def normalize_tolerance(raw):
    return {
        "byte_delta": raw.get("byte_delta", 0),
        "absolute": f32(raw.get("absolute", 0.0)),
        "relative": f32(raw.get("relative", 0.0)),
        "outlier_fraction": raw.get("outlier_fraction", 0.0),
        "edge_radius": raw.get("edge_radius", 0),
        "forgive_fraction": raw.get("forgive_fraction", 0.0),
        "outlier_max_delta": raw.get("outlier_max_delta", 0.0),
        "max_outliers": raw.get("max_outliers", 0),
        "min_psnr": raw.get("min_psnr", 0.0),
        "name": raw.get("name", "?"),
    }


def pixel_close(expected, ex, ey, actual, ax, ay, tol):
    channels = expected.channels
    klass = expected.klass
    eo = (ey * expected.width + ex) * channels
    ao = (ay * actual.width + ax) * channels
    for c in range(channels):
        if not channel_close(klass, expected.data[eo + c], actual.data[ao + c], tol)[0]:
            return False
    return True


def judge(expected, actual, tol, quick=False):
    result = {"pass": False, "total": 0, "bad": 0, "forgiven": 0, "psnr": 0.0, "max_delta": 0.0, "shape": False}
    if expected.width != actual.width or expected.height != actual.height or expected.kind != actual.kind:
        result["shape"] = True
        return result
    width, height, channels, klass = expected.width, expected.height, expected.channels, expected.klass
    total = width * height
    result["total"] = total
    if total == 0:
        return result
    integer = klass == "integer"
    ed, ad = expected.data, actual.data
    if klass != "float" and expected.payload == actual.payload:
        result.update({"pass": True, "psnr": 999.0})
        return result
    state = {}
    worst_of = {}
    squared = 0.0
    max_delta = 0.0
    candidates = [i for i, (a, b) in enumerate(zip(ed, ad)) if a != b or a - b != a - b]
    by_pixel = collections.defaultdict(list)
    for i in candidates:
        by_pixel[i // channels].append(i % channels)
    for pixel, cs in by_pixel.items():
        worst = 0.0
        ok = True
        for c in cs:
            a = ed[pixel * channels + c]
            b = ad[pixel * channels + c]
            close, units = channel_close(klass, a, b, tol)
            if not close:
                ok = False
            worst = max(worst, units)
            d = a - b
            if d != d:
                d = 1.0
            if klass == "float":
                d = max(-1.0, min(1.0, d))
            elif klass == "unorm16":
                d /= 65535.0
            else:
                d = max(-1.0, min(1.0, d / 255.0))
            squared += d * d
        if not ok:
            state[pixel] = 1
        worst_of[pixel] = worst
        max_delta = max(max_delta, worst)
    bad_first = len(state)
    allowed = tol["outlier_fraction"] * total
    if tol["max_outliers"] > 0:
        allowed = min(allowed, float(tol["max_outliers"]))
    radius = tol["edge_radius"]
    cap = int(tol["forgive_fraction"] * total)
    forgiven = 0
    if radius > 0 and not integer and not (quick and bad_first - cap > allowed):
        for pixel in sorted(state):
            if forgiven >= cap:
                break
            if state[pixel] != 1:
                continue
            y, x = divmod(pixel, width)
            e_in_a = a_in_e = edge = False
            for dy in range(-radius, radius + 1):
                for dx in range(-radius, radius + 1):
                    nx, ny = x + dx, y + dy
                    if nx < 0 or ny < 0 or nx >= width or ny >= height:
                        continue
                    if pixel_close(expected, x, y, actual, nx, ny, tol):
                        e_in_a = True
                    if pixel_close(expected, nx, ny, actual, x, y, tol):
                        a_in_e = True
                    if not pixel_close(expected, x, y, expected, nx, ny, tol):
                        edge = True
            if e_in_a and a_in_e and edge:
                state[pixel] = 2
                forgiven += 1
    bad = 0
    hard = 0
    for pixel, value in state.items():
        if value == 1:
            bad += 1
            if tol["outlier_max_delta"] > 0.0 and worst_of[pixel] > tol["outlier_max_delta"]:
                hard += 1
    mse = squared / (total * channels)
    psnr = 999.0 if mse <= 0.0 else 10.0 * math.log10(1.0 / mse)
    psnr_ok = integer or psnr >= tol["min_psnr"]
    result.update({"pass": bad <= allowed and hard == 0 and psnr_ok, "bad": bad, "forgiven": forgiven, "psnr": psnr, "max_delta": max_delta})
    return result


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
            data["_dir"] = base
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


def stem_of(frame, label):
    return "f%03d" % frame + ("_" + label if label else "")


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
        self.swap_log = []
        self.groups = []
        self.related = collections.defaultdict(set)
        self.cache = collections.OrderedDict()
        self.ids = sorted(set(guest) | set(host) | set(results))

    def flag(self, check_id, name, detail, label=None):
        entry = {"flag": name, "detail": detail}
        if label is not None:
            entry["label"] = label
        self.flags[check_id].append(entry)

    def spec_of(self, check_id):
        return self.guest.get(check_id) or self.host.get(check_id) or {}

    def explanation(self, check_id, entry):
        spec = self.spec_of(check_id)
        reasons = spec.get("audit_ok", [])
        if reasons:
            return "; ".join(reasons)
        if entry["flag"] == "uniform":
            labels = spec.get("uniform_ok", [])
            if "*" in labels or entry.get("label") in labels:
                return "uniform_ok"
        return None

    def unexplained(self):
        out = []
        for check_id in sorted(self.flags):
            for entry in self.flags[check_id]:
                if entry["flag"] in GATING and self.explanation(check_id, entry) is None:
                    out.append((check_id, entry["flag"], entry["detail"]))
        return out

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
                        self.flag(check_id, "uniform", "%s is a single value %s" % (name, stats["dominant"]), capture["label"])
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

    def cross_family(self):
        uniform_digests = set()
        for dumps, stat_field, hash_field in ((self.guest, "actual_stats", "actual_hash"), (self.guest, "oracle_stats", "oracle_hash"),
                                              (self.host, "actual_stats", "actual_hash")):
            for dump in dumps.values():
                for capture in dump.get("captures", []):
                    stats = capture.get(stat_field)
                    if stats is not None and stats["distinct"] == 1 and hash_field in capture:
                        uniform_digests.add(capture[hash_field])
        whole = [set(g["ids"]) for g in self.groups if g["kind"] == "identical_set"]
        device = {i: side_hashes(d, "actual_hash") for i, d in self.guest.items()}
        oracle = {i: side_hashes(d, "oracle_hash") for i, d in self.guest.items()}
        for i, d in self.host.items():
            if not oracle.get(i):
                oracle[i] = side_hashes(d, "actual_hash")
        for side, table in (("device", device), ("oracle", oracle)):
            by_digest = collections.defaultdict(list)
            for check_id, hashes in table.items():
                for key, digest in hashes.items():
                    if digest not in uniform_digests:
                        by_digest[(api_of(check_id), digest)].append((check_id, key))
            for (_, digest), members in sorted(by_digest.items()):
                ids = sorted({m[0] for m in members})
                if len(ids) < 2 or any(set(ids) <= w for w in whole):
                    continue
                families = {m[0]: family_of(m[0]) or m[0] for m in members}
                if len(set(families.values())) < 2:
                    continue
                self.groups.append({"kind": "cross_family", "side": side, "digest": digest, "ids": ids})
                for check_id, key in members:
                    others = sorted({o for o, _ in members if families[o] != families[check_id]})
                    if others:
                        self.flag(check_id, "cross_family_" + side, "f%d %s equals other families: %s" %
                                  (key[0], key[1] or "(unnamed)", ", ".join(others[:6]) + (" ..." if len(others) > 6 else "")))
                        self.related[check_id].update(others)

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

    def load(self, path):
        found = self.cache.get(path)
        if found is None:
            found = read_rref(path)
            self.cache[path] = found
            if len(self.cache) > 160:
                self.cache.popitem(last=False)
        else:
            self.cache.move_to_end(path)
        return found

    def oracle_images(self, check_id):
        out = {}
        guest = self.guest.get(check_id)
        host = self.host.get(check_id)
        for source, suffix, field in ((guest, ".oracle.rref", "oracle_hash"), (host, ".rref", "actual_hash")):
            if source is None:
                continue
            for capture in source.get("captures", []):
                key = key_of(capture)
                path = os.path.join(source["_dir"], stem_of(*key) + suffix)
                if key not in out and field in capture and os.path.exists(path):
                    out[key] = (path, capture.get("tolerance"))
        for capture in (guest or {}).get("captures", []):
            key = key_of(capture)
            if key in out and capture.get("tolerance"):
                out[key] = (out[key][0], capture["tolerance"])
        return out

    def swap(self):
        images = {i: self.oracle_images(i) for i in self.ids}
        images = {i: v for i, v in images.items() if v}
        families = collections.defaultdict(set)
        for check_id in images:
            family = family_of(check_id)
            if family:
                families[family].add(check_id)
        for check_id in sorted(images):
            siblings = set(self.related.get(check_id, ()))
            family = family_of(check_id)
            if family:
                siblings |= families[family]
            siblings.discard(check_id)
            siblings = sorted(s for s in siblings if s in images)
            if not siblings:
                continue
            mine = images[check_id]
            passing = []
            for sibling in siblings:
                theirs = images[sibling]
                if set(mine) - set(theirs):
                    continue
                everything = True
                for key, (path, raw_tolerance) in sorted(mine.items()):
                    if raw_tolerance is None:
                        everything = False
                        break
                    try:
                        verdict = judge(self.load(path), self.load(theirs[key][0]), normalize_tolerance(raw_tolerance), quick=True)
                    except (OSError, ValueError, KeyError):
                        everything = False
                        break
                    if not verdict["pass"]:
                        everything = False
                        break
                if everything:
                    passing.append(sibling)
            self.swap_log.append({"id": check_id, "siblings": len(siblings), "passing": passing})
            if passing:
                names = ", ".join(passing[:6]) + (" ..." if len(passing) > 6 else "")
                self.flag(check_id, "swap_pass", "sibling %s passes this check with its own image, so replacing the feature by the sibling's state would not be caught" % names)

    def verify(self):
        checked = 0
        mismatches = []
        for check_id, dump in sorted(self.guest.items()):
            for capture in dump.get("captures", []):
                if "pass" not in capture or "oracle_hash" not in capture:
                    continue
                key = key_of(capture)
                stem = os.path.join(dump["_dir"], stem_of(*key))
                if not (os.path.exists(stem + ".oracle.rref") and os.path.exists(stem + ".actual.rref")):
                    continue
                verdict = judge(read_rref(stem + ".oracle.rref"), read_rref(stem + ".actual.rref"), normalize_tolerance(capture["tolerance"]))
                checked += 1
                wrong = []
                if verdict["pass"] != capture["pass"]:
                    wrong.append("pass %s vs %s" % (verdict["pass"], capture["pass"]))
                for field in ("bad", "forgiven", "total"):
                    if verdict[field] != capture[field]:
                        wrong.append("%s %s vs %s" % (field, verdict[field], capture[field]))
                for field in ("psnr", "max_delta"):
                    if not math.isclose(verdict[field], capture[field], rel_tol=1e-6, abs_tol=1e-6):
                        wrong.append("%s %s vs %s" % (field, verdict[field], capture[field]))
                if wrong:
                    mismatches.append("%s f%d %s: %s" % (check_id, key[0], key[1], ", ".join(wrong)))
        return checked, mismatches

    def run(self, swap=True):
        self.uniform()
        self.indistinct()
        self.cross_family()
        self.multiframe()
        self.trivial()
        self.looseness()
        if swap:
            self.swap()


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
        ("cross_family_device", "Identical captures across matrix families, device side"),
        ("cross_family_oracle", "Identical captures across matrix families, oracle or host side"),
        ("swap_pass", "Swap test: a sibling's oracle image passes this check's tolerance"),
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
    open_items = audit.unexplained()
    lines += ["", "## Unexplained gating flags (%d)" % len(open_items), ""]
    if not open_items:
        lines.append("none")
    for check_id, name, detail in open_items:
        lines.append("- %s [%s]: %s" % (check_id, name, detail))
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
    parser.add_argument("--strict", action="store_true", help="exit 1 when a gating flag has no audit_ok explanation")
    parser.add_argument("--no-swap", action="store_true", help="skip the swap test")
    parser.add_argument("--verify-judge", action="store_true", help="check the python judge against the verdicts recorded in captures.json")
    args = parser.parse_args()
    audit = Audit(load_dumps(args.guest), load_dumps(args.host), load_results(args.json))
    if args.verify_judge:
        checked, mismatches = audit.verify()
        print("render-audit: judge verified on %d captures, %d mismatches" % (checked, len(mismatches)))
        for line in mismatches:
            print("  " + line)
        return 1 if mismatches or checked == 0 else 0
    audit.run(swap=not args.no_swap)
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
        "swap": audit.swap_log,
        "unexplained": [{"id": i, "flag": f, "detail": d} for i, f, d in audit.unexplained()],
        "skips": {reason: ids for reason, ids in skips.items()},
        "failures": [{"id": i, "status": s, "reason": r} for i, s, r in fails],
    }
    target = args.flags or ((args.out.rsplit(".", 1)[0] + ".json") if args.out else "render-audit.json")
    with open(target, "w", encoding="utf-8") as handle:
        json.dump(machine, handle, indent=1, sort_keys=True)
    if args.strict and audit.unexplained():
        print("render-audit: %d unexplained gating flags" % len(audit.unexplained()), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
