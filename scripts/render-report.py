#!/usr/bin/env python3
import argparse
import array
import base64
import collections
import datetime
import hashlib
import html
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import zipfile
import zlib

WARN_BYTES = 100 << 20
MAX_EMBED_SIDE = 256
KINDS = [
    ("rgba8", 4, "B"), ("bgra8", 4, "B"), ("rgba16f", 4, "e"), ("rgba32f", 4, "f"), ("r32f", 1, "f"), ("depth32f", 1, "f"),
    ("rgba32ui", 4, "I"), ("r8ui", 1, "B"), ("r8", 1, "B"), ("rg8", 2, "B"), ("r16f", 1, "e"), ("rg16f", 2, "e"),
    ("r16", 1, "H"), ("r16ui", 1, "H"), ("rg32f", 2, "f"), ("rgba16", 4, "H"), ("rgba8ui", 4, "B"), ("rgba16ui", 4, "H"),
]
SIZES = {"B": 1, "H": 2, "e": 2, "f": 4, "I": 4}


def png_decode(blob):
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    header = None
    palette = None
    idat = bytearray()
    while pos + 8 <= len(blob):
        length, kind = struct.unpack_from(">I4s", blob, pos)
        data = blob[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", data)
        elif kind == b"PLTE":
            palette = data
        elif kind == b"IDAT":
            idat += data
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError("no IHDR")
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or interlace != 0 or color not in (0, 2, 3, 4, 6):
        raise ValueError("unsupported PNG")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        mode = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        if mode == 1:
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 255
        elif mode == 2:
            for i in range(stride):
                line[i] = (line[i] + previous[i]) & 255
        elif mode == 3:
            for i in range(stride):
                left = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((left + previous[i]) >> 1)) & 255
        elif mode == 4:
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                b = previous[i]
                c = previous[i - channels] if i >= channels else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 255
        elif mode != 0:
            raise ValueError("bad filter")
        rows.append(line)
        previous = line
    out = []
    for line in rows:
        pixels = []
        for x in range(width):
            if color == 6:
                pixels.append(tuple(line[x * 4:x * 4 + 4]))
            elif color == 2:
                pixels.append((line[x * 3], line[x * 3 + 1], line[x * 3 + 2], 255))
            elif color == 0:
                pixels.append((line[x], line[x], line[x], 255))
            elif color == 4:
                pixels.append((line[x * 2], line[x * 2], line[x * 2], line[x * 2 + 1]))
            else:
                i = line[x] * 3
                pixels.append((palette[i], palette[i + 1], palette[i + 2], 255))
        out.append(pixels)
    return width, height, out



def swar_sub(a, b):
    n = len(a)
    mask = (1 << (8 * n)) - 1
    high = int.from_bytes(b"\x80" * n, "little")
    x = int.from_bytes(a, "little")
    y = int.from_bytes(b, "little")
    d = ((x | high) - (y & ~high & mask)) ^ ((x ^ ~y) & high)
    return (d & mask).to_bytes(n, "little")


def png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)


def png_encode_rgba(width, height, rgba):
    colors = array.array("I", rgba)
    palette = {}
    for value in colors:
        if value not in palette:
            palette[value] = len(palette)
            if len(palette) > 256:
                break
    signature = b"\x89PNG\r\n\x1a\n"
    if len(palette) <= 256:
        table = sorted(palette, key=palette.get)
        plte = b"".join(struct.pack("<I", v)[:3] for v in table)
        trns = bytes(struct.pack("<I", v)[3] for v in table)
        lookup = bytes(palette[v] for v in colors)
        raw = b"".join(b"\x00" + lookup[y * width:(y + 1) * width] for y in range(height))
        extra = png_chunk(b"PLTE", plte) + (png_chunk(b"tRNS", trns) if any(b != 255 for b in trns) else b"")
        header = struct.pack(">IIBBBBB", width, height, 8, 3, 0, 0, 0)
        return signature + png_chunk(b"IHDR", header) + extra + png_chunk(b"IDAT", zlib.compress(raw, 9)) + png_chunk(b"IEND", b"")
    opaque = rgba[3::4] == b"\xff" * (len(rgba) // 4)
    if opaque:
        pixels = bytearray(len(rgba) // 4 * 3)
        pixels[0::3] = rgba[0::4]
        pixels[1::3] = rgba[1::4]
        pixels[2::3] = rgba[2::4]
        pixels = bytes(pixels)
        channels, color = 3, 2
    else:
        pixels, channels, color = bytes(rgba), 4, 6
    stride = width * channels
    plain = b"".join(b"\x00" + pixels[y * stride:(y + 1) * stride] for y in range(height))
    up = swar_sub(pixels, b"\x00" * stride + pixels[:-stride])
    filtered = b"".join(b"\x02" + up[y * stride:(y + 1) * stride] for y in range(height))
    best = min(zlib.compress(plain, 9), zlib.compress(filtered, 9), key=len)
    header = struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0)
    return signature + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", best) + png_chunk(b"IEND", b"")


def png_to_rgba(blob):
    width, height, rows = png_decode(blob)
    out = bytearray()
    for row in rows:
        for pixel in row:
            out += bytes(pixel)
    return width, height, bytes(out)


def rref_to_rgba(blob):
    if len(blob) < 32 or blob[:4] != b"LSWR":
        raise ValueError("bad rref")
    _, width, height, kind = struct.unpack_from("<IIII", blob, 4)
    name, channels, element = KINDS[kind]
    count = width * height * channels
    payload = blob[32:32 + count * SIZES[element]]
    pixels = width * height
    out = bytearray(pixels * 4)
    out[3::4] = b"\xff" * pixels
    if name in ("depth32f", "r32f"):
        values = array.array("f", payload)
        finite = [v for v in values if v == v and abs(v) != float("inf")]
        low, high = (min(finite), max(finite)) if finite else (0.0, 1.0)
        span = (high - low) or 1.0
        gray = bytes(int(min(1.0, max(0.0, (v - low) / span)) * 255 + 0.5) if v == v and abs(v) != float("inf") else 255 for v in values)
        out[0::4] = gray
        out[1::4] = gray
        out[2::4] = gray
        return width, height, bytes(out)
    if element == "I" or name.endswith("ui"):
        step = {"B": "B", "H": "H", "I": "I"}[element]
        values = array.array(step, payload)
        for i in range(pixels):
            chunk_values = values[i * channels:(i + 1) * channels]
            if any(chunk_values):
                h = zlib.crc32(bytes(str(tuple(chunk_values)), "ascii"))
                out[i * 4:i * 4 + 3] = bytes((64 + (h >> s) % 192) & 255 for s in (0, 8, 16))
        return width, height, bytes(out)
    if element == "B":
        raw = payload
    elif element == "H":
        raw = bytes(v >> 8 for v in array.array("H", payload))
    else:
        fmt = "<%d%s" % (count, "e" if element == "e" else "f")
        raw = bytes(int(min(1.0, max(0.0, v if v == v else 0.0)) * 255 + 0.5) for v in struct.unpack(fmt, payload))
    if name == "bgra8":
        out[0::4] = raw[2::4]
        out[1::4] = raw[1::4]
        out[2::4] = raw[0::4]
        out[3::4] = raw[3::4]
    elif channels == 4:
        out[:] = raw
    elif channels == 1:
        out[0::4] = raw
        out[1::4] = raw
        out[2::4] = raw
    elif channels == 2:
        out[0::4] = raw[0::2]
        out[1::4] = raw[1::2]
        out[2::4] = b"\x00" * pixels
    return width, height, bytes(out)


class Artifacts:
    def __init__(self, path):
        self.dir = None
        self.zip = None
        self.index = {}
        if not path:
            return
        if os.path.isdir(path):
            self.dir = path
        elif os.path.isfile(path) and path.endswith(".zip"):
            self.zip = zipfile.ZipFile(path)
            for name in self.zip.namelist():
                parts = [p for p in name.replace("\\", "/").split("/") if p]
                for part in parts[:-1]:
                    if part.startswith("render."):
                        self.index.setdefault(part, {})[parts[-1]] = name
                        break

    def files(self, check_id):
        if self.dir is not None:
            folder = os.path.join(self.dir, check_id)
            return sorted(os.listdir(folder)) if os.path.isdir(folder) else []
        return sorted(self.index.get(check_id, {}))

    def read(self, check_id, name):
        if self.dir is not None:
            with open(os.path.join(self.dir, check_id, name), "rb") as handle:
                return handle.read()
        return self.zip.read(self.index[check_id][name])


class Refs:
    def __init__(self, path):
        self.path = path

    def read(self, check_id, stem):
        if not self.path:
            return None
        for base in self.roots():
            candidate = os.path.join(base, check_id, stem + ".rref")
            if os.path.isfile(candidate):
                with open(candidate, "rb") as handle:
                    return handle.read()
        return None

    def roots(self):
        if not self.path or not os.path.isdir(self.path):
            return []
        found = [self.path]
        found += [os.path.join(self.path, d) for d in sorted(os.listdir(self.path)) if os.path.isfile(os.path.join(self.path, d, "device.json"))]
        return found[1:] + found[:1] if len(found) > 1 else found


class Run:
    def __init__(self, spec):
        self.host = spec.get("host", "local")
        self.gpu = spec.get("gpu", "")
        self.bits = spec.get("bits", "")
        self.path = spec["json"]
        with open(self.path, encoding="utf-8") as handle:
            self.data = json.load(handle)
        adapter = self.data.get("adapter", {})
        if not self.gpu:
            self.gpu = adapter.get("name", "gpu")
        if not self.bits:
            self.bits = str(self.data.get("bitness", ""))
        self.bits = self.bits.lstrip("x")
        self.artifacts = Artifacts(spec.get("artifacts"))
        self.refs = Refs(spec.get("refs"))
        self.audit = None
        if spec.get("audit") and os.path.exists(spec["audit"]):
            with open(spec["audit"], encoding="utf-8") as handle:
                self.audit = json.load(handle)
        self.checks = {c["id"]: c for c in self.data.get("checks", [])}

    @property
    def label(self):
        return "%s:%s x%s" % (self.host, self.gpu, self.bits)

    @property
    def key(self):
        return (self.host, self.gpu, self.bits)


def parse_spec(text):
    spec = {}
    for part in text.split(","):
        key, _, value = part.partition("=")
        spec[key.strip()] = value.strip()
    if "json" not in spec:
        raise SystemExit("render-report: --run needs json=PATH: " + text)
    return spec


def load_tree(root):
    specs = []
    for host in sorted(os.listdir(root)):
        host_dir = os.path.join(root, host)
        if not os.path.isdir(host_dir) or host in ("sanity", "evidence", "logs"):
            continue
        for gpu in sorted(os.listdir(host_dir)):
            if not os.path.isdir(os.path.join(host_dir, gpu)):
                continue
            for bits in sorted(os.listdir(os.path.join(host_dir, gpu))):
                base = os.path.join(host_dir, gpu, bits)
                result = os.path.join(base, "result.json")
                if not os.path.isfile(result):
                    continue
                meta = {}
                if os.path.isfile(os.path.join(base, "meta.json")):
                    with open(os.path.join(base, "meta.json"), encoding="utf-8") as handle:
                        meta = json.load(handle)
                artifacts = os.path.join(base, "artifacts")
                if not os.path.isdir(artifacts):
                    artifacts = os.path.join(base, "artifacts.zip")
                specs.append({"json": result, "artifacts": artifacts, "audit": os.path.join(base, "audit.json"), "refs": meta.get("refs", ""),
                              "host": meta.get("host", host), "gpu": meta.get("gpu", gpu), "bits": meta.get("bits", bits)})
    return specs


def import_matrix(source, target):
    imported = 0
    for host in sorted(os.listdir(source)):
        host_dir = os.path.join(source, host)
        if not os.path.isdir(host_dir):
            continue
        for gpu in sorted(os.listdir(host_dir)):
            suites = os.path.join(host_dir, gpu, "suites")
            results = os.path.join(host_dir, gpu, "results.json")
            gpu_name = gpu
            if os.path.isfile(results):
                with open(results, encoding="utf-8") as handle:
                    gpu_name = json.load(handle).get("gpu", {}).get("name", gpu)
            for bits in ("64", "32"):
                result = os.path.join(suites, "lsw-suites%s-main.json" % bits)
                if not os.path.isfile(result):
                    continue
                base = os.path.join(target, host, gpu, "x" + bits)
                os.makedirs(base, exist_ok=True)
                shutil.copyfile(result, os.path.join(base, "result.json"))
                archive = os.path.join(suites, "lsw-suites%s-main-artifacts.zip" % bits)
                if os.path.isfile(archive):
                    shutil.copyfile(archive, os.path.join(base, "artifacts.zip"))
                refs = os.path.join(suites, "render-refs")
                with open(os.path.join(base, "meta.json"), "w", encoding="utf-8") as handle:
                    json.dump({"host": host, "gpu": gpu_name, "bits": bits, "refs": os.path.abspath(refs) if os.path.isdir(refs) else ""}, handle)
                imported += 1
    return imported


def esc(text):
    return html.escape(str(text), quote=True)


def api_of(check_id):
    parts = check_id.split(".")
    return parts[1] if len(parts) > 2 else "?"


def status_class(status):
    return {"ok": "ok", "skip": "skip"}.get(status, "bad")


def counts(run, api=None):
    out = collections.Counter()
    for check_id, check in run.checks.items():
        if api is None or api_of(check_id) == api:
            out[check["status"]] += 1
    return out


def git_revision(root):
    try:
        return subprocess.run(["git", "-C", root, "describe", "--always", "--dirty"], capture_output=True, text=True, timeout=20).stdout.strip() or "unknown"
    except Exception:
        return "unknown"


def differing(runs):
    rows = []
    for check_id in sorted({i for r in runs for i in r.checks}):
        states = [r.checks[check_id]["status"] if check_id in r.checks else "-" for r in runs]
        if len(set(states)) > 1:
            rows.append((check_id, states))
    return rows


def baseline_changes(runs, baseline):
    by_key = {r.key: r for r in baseline}
    changes = []
    for run in runs:
        old = by_key.get(run.key)
        if old is None:
            continue
        for check_id in sorted(set(run.checks) | set(old.checks)):
            before = old.checks.get(check_id, {}).get("status", "-")
            after = run.checks.get(check_id, {}).get("status", "-")
            if before != after:
                changes.append((run.label, check_id, before, after, run.checks.get(check_id, {}).get("reason", "")))
    return changes


class ImageTable:
    def __init__(self):
        self.images = {}
        self.bytes = 0

    def add(self, width, height, rgba):
        if width > MAX_EMBED_SIDE or height > MAX_EMBED_SIDE:
            factor = -(-max(width, height) // MAX_EMBED_SIDE)
            nw, nh = width // factor, height // factor
            small = bytearray()
            for y in range(nh):
                row = (y * factor) * width * 4
                for x in range(nw):
                    small += rgba[row + x * factor * 4:row + x * factor * 4 + 4]
            width, height, rgba = nw, nh, bytes(small)
        key = hashlib.sha1(struct.pack("<II", width, height) + rgba).hexdigest()[:16]
        if key not in self.images:
            text = base64.b64encode(png_encode_rgba(width, height, rgba)).decode()
            self.images[key] = [width, height, text]
            self.bytes += len(text)
        return key


def stem_of(frame, label):
    return "f%03d" % frame + ("_" + label if label else "")


def capture_images(run, check, record, table, stem, want_images):
    result = {}
    if not want_images:
        return result
    check_id = check["id"]
    files = set(run.artifacts.files(check_id)) if run.artifacts else set()

    def load(name_rref, name_png):
        try:
            if name_rref in files:
                return rref_to_rgba(run.artifacts.read(check_id, name_rref))
            if name_png in files:
                return png_to_rgba(run.artifacts.read(check_id, name_png))
        except Exception:
            return None
        return None

    actual = load(stem + ".actual.rref", stem + ".actual.png")
    reference = load(stem + ".oracle.rref", stem + ".oracle.png")
    if reference is None:
        blob = run.refs.read(check_id, stem)
        if blob is not None:
            try:
                reference = rref_to_rgba(blob)
            except Exception:
                reference = None
    if reference is None and stem + ".expected.png" in files:
        try:
            reference = png_to_rgba(run.artifacts.read(check_id, stem + ".expected.png"))
        except Exception:
            reference = None
    if actual is not None:
        result["dev"] = table.add(*actual)
    if reference is not None:
        result["ref"] = table.add(*reference)
    return result


def split_id(check_id):
    parts = check_id.split(".", 3)
    while len(parts) < 4:
        parts.append("")
    return parts[1], parts[2], parts[3]


def build_data(runs, baseline, mode):
    table = ImageTable()
    rows = {check_id for check_id, _ in differing(runs)}
    base_by_key = {r.key: r for r in baseline} if baseline else {}
    ids = sorted({i for r in runs for i in r.checks})
    tests = []
    for check_id in ids:
        api, group, name = split_id(check_id)
        cells = []
        for run in runs:
            check = run.checks.get(check_id)
            if check is None:
                cells.append(None)
                continue
            status = check["status"]
            want = mode == "all" or (mode == "failed" and status not in ("ok", "skip")) or (mode == "differing" and check_id in rows)
            cell = {"s": status, "r": check.get("reason", ""), "ms": check.get("ms", 0)}
            if check.get("host_crash"):
                cell["h"] = check.get("evidence", "")
            old = base_by_key.get(run.key)
            if old is not None:
                cell["b"] = old.checks.get(check_id, {}).get("status", "-")
            caps = []
            if want and status != "skip" and run.artifacts is not None:
                record = None
                if run.artifacts.dir is not None or run.artifacts.zip is not None:
                    try:
                        record = json.loads(run.artifacts.read(check_id, "captures.json"))
                    except Exception:
                        record = None
                for c in (record or {}).get("captures", []):
                    item = {"f": c.get("frame", 0), "l": c.get("label", ""), "w": c.get("width"), "h": c.get("height"), "k": c.get("kind"),
                            "t": c.get("tolerance", {}), "p": c.get("psnr"), "b": c.get("bad"), "m": c.get("max_delta"), "g": c.get("forgiven"),
                            "n": c.get("total"), "ok": c.get("pass")}
                    item.update(capture_images(run, check, c, table, stem_of(item["f"], item["l"]), True))
                    caps.append(item)
            elif not want and status != "skip" and run.artifacts is not None and run.artifacts.dir is not None:
                try:
                    record = json.loads(run.artifacts.read(check_id, "captures.json"))
                    for c in record.get("captures", []):
                        caps.append({"f": c.get("frame", 0), "l": c.get("label", ""), "w": c.get("width"), "h": c.get("height"), "k": c.get("kind"),
                                     "t": c.get("tolerance", {}), "p": c.get("psnr"), "b": c.get("bad"), "m": c.get("max_delta"),
                                     "g": c.get("forgiven"), "n": c.get("total"), "ok": c.get("pass")})
                except Exception:
                    pass
            if caps:
                cell["c"] = caps
            measures = {k: v for k, v in check.get("measures", {}).items() if not k.startswith(("psnr.", "bad_pixels.", "max_delta.", "forgiven."))}
            if measures:
                cell["m"] = measures
            cells.append(cell)
        tests.append({"id": check_id, "a": api, "g": group, "n": name, "c": cells})
    return table, tests


def runs_json(runs):
    out = []
    for run in runs:
        adapter = run.data.get("adapter", {})
        out.append({"host": run.host, "gpu": run.gpu, "bits": run.bits, "label": run.label, "exe": run.data.get("exe", ""),
                    "started": run.data.get("started", ""), "complete": bool(run.data.get("complete")), "adapter": adapter})
    return out


def skips_section(runs):
    groups = collections.OrderedDict()
    for run in runs:
        for check in run.checks.values():
            if check["status"] == "skip":
                groups.setdefault(check.get("reason", ""), collections.OrderedDict()).setdefault(run.label, []).append(check["id"])
    if not groups:
        return "<p>No skipped checks.</p>"
    out = []
    for reason, per_run in sorted(groups.items(), key=lambda kv: -sum(len(v) for v in kv[1].values())):
        total = sum(len(v) for v in per_run.values())
        out.append("<details class=\"f\" data-k=\"%s\"><summary><span class=\"skip\">%d</span> %s</summary>" %
                   (esc(("skip " + reason + " " + " ".join(per_run)).lower()), total, esc(reason or "(no reason)")))
        for label, ids in per_run.items():
            out.append("<div><b>%s</b> (%d): <span class=\"id mute\">%s</span></div>" % (esc(label), len(ids), esc(", ".join(sorted(ids)))))
        out.append("</details>")
    return "".join(out)


def crashes_section(runs):
    rows = []
    for run in runs:
        incidents = run.data.get("incidents", [])
        confirmed = {c["id"]: c for c in run.checks.values() if c.get("host_crash")}
        for crash in run.data.get("crashes", []):
            incident = crash.get("incident", {})
            rows.append((run.label, crash.get("segment"), incident, crash.get("in_flight", []), crash.get("confirmed"), confirmed))
        if not run.data.get("crashes") and incidents:
            for incident in incidents:
                rows.append((run.label, None, incident, [], None, confirmed))
    if not rows:
        return "<p>No run lost its VM.</p>"
    out = ["<div class=\"scroll\"><table class=\"plain\"><tr><th>run</th><th>segment</th><th>VM death</th><th>in flight</th><th>culprit</th><th>evidence</th></tr>"]
    for label, segment, incident, in_flight, culprit, confirmed in rows:
        evidence = incident.get("evidence", "")
        core = incident.get("core", "")
        out.append("<tr><td>%s</td><td class=\"n\">%s</td><td>%s</td><td class=\"id\">%s</td><td class=\"id bad\">%s</td><td><a href=\"%s\">%s</a>%s</td></tr>" %
                   (esc(label), esc(str(segment or "")), esc("%s %s" % (incident.get("reason", ""), incident.get("signal", ""))),
                    esc(", ".join(in_flight)), esc(culprit or ""), esc(evidence), esc(evidence), esc(" core " + core if core else "")))
    out.append("</table></div>")
    alone = sorted({(r.label, c["id"]) for r in runs for c in r.checks.values() if c.get("host_crash") and "when run alone" in c.get("reason", "")})
    flying = sorted({(r.label, c["id"]) for r in runs for c in r.checks.values() if c.get("host_crash") and "when run alone" not in c.get("reason", "")})
    if alone:
        out.append("<p>Checks that killed the VM when run alone: <span class=\"id bad\">%s</span></p>" % esc(", ".join("%s %s" % h for h in alone)))
    if flying:
        out.append("<p>Checks in flight when the VM died, not confirmed alone: <span class=\"id\">%s</span></p>" % esc(", ".join("%s %s" % h for h in flying)))
    return "".join(out)


def audit_section(runs):
    with_audit = [r for r in runs if r.audit]
    if not with_audit:
        return "<p class=\"mute\">No audit results were given.</p>"
    out = ["<table><tr><th>run</th><th>checks with flags</th><th>unexplained gating flags</th><th>flags by kind</th></tr>"]
    for run in with_audit:
        flags = run.audit.get("checks", {})
        kinds = collections.Counter(f["flag"] for item in flags.values() for f in item.get("flags", []))
        flagged = sum(1 for item in flags.values() if item.get("flags"))
        unexplained = run.audit.get("unexplained", [])
        out.append("<tr><td>%s</td><td class=\"n\">%d</td><td class=\"n %s\">%d</td><td>%s</td></tr>" %
                   (esc(run.label), flagged, "bad" if unexplained else "ok", len(unexplained),
                    esc(", ".join("%s %d" % (k, v) for k, v in sorted(kinds.items())) or "none")))
    out.append("</table>")
    for run in with_audit:
        unexplained = run.audit.get("unexplained", [])
        if unexplained:
            out.append("<details><summary>%s: unexplained flags</summary>" % esc(run.label))
            for item in unexplained[:200]:
                out.append('<div><span class="id">%s</span> [%s] %s</div>' % (esc(item["id"]), esc(item["flag"]), esc(item["detail"])))
            out.append("</details>")
    return "".join(out)



def build(runs, baseline, revision, title, mode):
    table, tests = build_data(runs, baseline, mode)
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    meta = {"title": title, "date": now, "revision": revision, "machine": socket.gethostname(), "runs": runs_json(runs), "baseline": baseline is not None,
            "images_mode": mode}

    def blob(value):
        return json.dumps(value, separators=(",", ":")).replace("</", "<\\/")

    static = ["<details id=\"crashes\" open><summary>Host crashes</summary>%s</details>" % crashes_section(runs),
              "<details id=\"skips\"><summary>Skipped checks by reason</summary>%s</details>" % skips_section(runs),
              "<details id=\"audit\"><summary>Audit flags</summary>%s</details>" % audit_section(runs)]
    if baseline is not None:
        changes = baseline_changes(runs, baseline)
        body = ["<p>%d rows changed status since the baseline.</p>" % len(changes)]
        if changes:
            body.append("<div class=\"scroll\"><table class=\"plain\"><tr><th>run</th><th>row</th><th>before</th><th>now</th><th>reason now</th></tr>")
            for label, check_id, before, after, reason in changes:
                body.append("<tr><td>%s</td><td class=\"id\">%s</td><td class=\"%s\">%s</td><td class=\"%s\">%s</td><td>%s</td></tr>" %
                            (esc(label), esc(check_id), status_class(before), esc(before), status_class(after), esc(after), esc(reason)))
            body.append("</table></div>")
        static.insert(0, "<details id=\"baseline\"><summary>Changes since the baseline</summary>%s</details>" % "".join(body))
    page = (TEMPLATE.replace("@@TITLE@@", esc(title)).replace("@@CSS@@", CSS).replace("@@STATIC@@", "".join(static))
            .replace("@@META@@", blob(meta)).replace("@@TESTS@@", blob(tests)).replace("@@IMAGES@@", blob(table.images)).replace("@@SCRIPT@@", SCRIPT))
    return page, differing(runs)


def summary(runs, rows, baseline, path, size):
    lines = ["render report: %d runs" % len(runs)]
    for run in runs:
        c = counts(run)
        lines.append("  %-44s pass %d  fail %d  skip %d" % (run.label, c["ok"], sum(v for k, v in c.items() if k not in ("ok", "skip")), c["skip"]))
    lines.append("  rows differing between runs: %d" % len(rows))
    if baseline is not None:
        lines.append("  rows changed since the baseline: %d" % len(baseline_changes(runs, baseline)))
    lines.append("  report: %s (%.1f MB)" % (path, size / (1 << 20)))
    if size > WARN_BYTES:
        lines.append("  warning: the report is over %d MB; use --images failed or differing" % (WARN_BYTES >> 20))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Single-page HTML report for lsw-suites render runs")
    parser.add_argument("--out", help="report.html path")
    parser.add_argument("--run", action="append", default=[], metavar="SPEC", help="json=PATH[,artifacts=DIR|ZIP][,refs=DIR][,audit=PATH][,host=H][,gpu=G][,bits=N]")
    parser.add_argument("--tree", action="append", default=[], metavar="ROOT", help="directory of <host>/<gpu>/<bits>/result.json runs")
    parser.add_argument("--baseline-run", action="append", default=[], metavar="SPEC")
    parser.add_argument("--baseline-tree", action="append", default=[], metavar="ROOT")
    parser.add_argument("--import-matrix", metavar="DIR", help="a gpu-test.sh run directory whose suites results are copied into --into")
    parser.add_argument("--into", metavar="ROOT")
    parser.add_argument("--images", choices=("all", "differing", "failed", "none"), default="all")
    parser.add_argument("--revision")
    parser.add_argument("--title", default="Render suite report")
    args = parser.parse_args()
    if args.import_matrix:
        if not args.into:
            raise SystemExit("render-report: --import-matrix needs --into")
        print("render report: imported %d runs from %s" % (import_matrix(args.import_matrix, args.into), args.import_matrix))
        if not args.out and not args.tree and not args.run:
            return 0
    specs = [parse_spec(s) for s in args.run]
    for root in args.tree:
        specs += load_tree(root)
    if not specs:
        raise SystemExit("render-report: no runs given")
    if not args.out:
        raise SystemExit("render-report: --out is required")
    runs = sorted((Run(s) for s in specs), key=lambda r: (r.host, r.gpu, r.bits))
    baseline = None
    if args.baseline_run or args.baseline_tree:
        base_specs = [parse_spec(s) for s in args.baseline_run]
        for root in args.baseline_tree:
            base_specs += load_tree(root)
        baseline = [Run(s) for s in base_specs]
    revision = args.revision or git_revision(os.path.dirname(os.path.abspath(__file__)))
    page, rows = build(runs, baseline, revision, args.title, args.images)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    data = page.encode("utf-8")
    with open(args.out, "wb") as handle:
        handle.write(data)
    print(summary(runs, rows, baseline, args.out, len(data)))
    return 0


CSS = """
:root{--bg:#fff;--fg:#1b1f23;--mute:#59636e;--line:#d0d7de;--card:#f6f8fa;--ok:#1a7f37;--okbg:#dafbe1;--bad:#cf222e;--badbg:#ffebe9;--skip:#7d4e00;--skipbg:#fff8c5;--chg:#0969da;--sel:#ddf4ff}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--mute:#8b949e;--line:#30363d;--card:#161b22;--ok:#56d364;--okbg:#12261a;--bad:#ff7b72;--badbg:#2d1214;--skip:#e3b341;--skipbg:#2b2110;--chg:#58a6ff;--sel:#0c2d4a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:13px/1.4 system-ui,-apple-system,"Segoe UI",sans-serif}
header{padding:8px 16px 0}h1{font-size:18px;margin:0}.meta{color:var(--mute);font-size:12px}
.bar{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line);padding:6px 16px;display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.bar input[type=search]{width:260px}input,select,button{font:inherit;color:var(--fg);background:var(--card);border:1px solid var(--line);border-radius:4px;padding:2px 6px}
button{cursor:pointer}button.on{background:var(--sel);border-color:var(--chg)}label.chip{display:inline-flex;align-items:center;gap:3px;border:1px solid var(--line);border-radius:10px;padding:0 8px;background:var(--card)}
label.chip.off{opacity:.45}.count{color:var(--mute)}
main{display:grid;grid-template-columns:minmax(0,1fr);gap:0}main.open{grid-template-columns:minmax(0,1fr) 680px}
#wrap{height:calc(100vh - 96px);overflow:auto;padding:0 16px}
table#grid{border-collapse:separate;border-spacing:0;width:100%}#grid th,#grid td{border-bottom:1px solid var(--line);padding:2px 8px;text-align:left;white-space:nowrap}
#grid thead th{height:24px;position:sticky;top:0;background:var(--card);z-index:3;font-weight:600}#grid thead tr.cnt th{top:24px;font-weight:400;color:var(--mute);font-size:11px}
#grid td.n{max-width:520px;overflow:hidden;text-overflow:ellipsis}tr.t{cursor:pointer}tr.t:hover{background:var(--card)}tr.t.sel{background:var(--sel)}tr.t.diff td.n{box-shadow:inset 3px 0 var(--chg)}
tr.g td{background:var(--card);font-weight:600;cursor:pointer}tr.a td{background:var(--card);font-weight:700;font-size:14px;cursor:pointer}
td.c{text-align:center;width:90px}.s{display:inline-block;min-width:54px;border-radius:9px;padding:0 8px;font-size:12px;text-align:center}
.s.ok{color:var(--ok);background:var(--okbg)}.s.bad{color:var(--bad);background:var(--badbg);font-weight:600}.s.skip{color:var(--skip);background:var(--skipbg)}.s.none{color:var(--mute)}
.s.chg{outline:2px solid var(--chg)}.reason{color:var(--mute);font-size:11px;margin-left:8px}
aside#panel{height:calc(100vh - 96px);overflow:auto;border-left:1px solid var(--line);padding:10px 14px;background:var(--bg)}
aside h2{font-size:14px;margin:0 0 6px;word-break:break-all}.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:6px 0}
#view{overflow:auto;max-height:46vh;border:1px solid var(--line);background:repeating-conic-gradient(var(--card) 0 25%,var(--bg) 0 50%) 0 0/16px 16px}
#view canvas{display:block;image-rendering:pixelated;cursor:crosshair}#readout{font:12px ui-monospace,monospace;min-height:34px;color:var(--mute);white-space:pre}
.strip{display:flex;gap:6px;overflow-x:auto;padding:4px 0}.strip figure{margin:0;text-align:center;cursor:pointer;border:2px solid transparent}.strip figure.on{border-color:var(--chg)}
.strip img{width:64px;height:64px;object-fit:contain;image-rendering:pixelated;display:block;background:var(--card)}.strip figcaption{font-size:10px;color:var(--mute)}
table.kv{border-collapse:collapse;font-size:12px}table.kv td{padding:1px 8px 1px 0;vertical-align:top}table.kv td:first-child{color:var(--mute)}
.note{color:var(--mute);font-size:12px}.id{font-family:ui-monospace,monospace;font-size:12px}.ok{color:var(--ok)}.bad{color:var(--bad)}.skip{color:var(--skip)}
section.extras{padding:8px 16px 40px}details{margin:6px 0}summary{cursor:pointer;font-weight:600}.scroll{overflow-x:auto}table.plain{border-collapse:collapse}table.plain th,table.plain td{border:1px solid var(--line);padding:2px 8px;text-align:left}
.mute{color:var(--mute)}.hidden{display:none!important}
@media (max-width:900px){main.open{grid-template-columns:minmax(0,1fr)}aside#panel{position:fixed;inset:60px 0 0 0;z-index:9}}
"""

TEMPLATE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>@@TITLE@@</title><style>@@CSS@@</style></head><body>
<header><h1>@@TITLE@@</h1><div class="meta" id="meta"></div></header>
<div class="bar" id="bar"></div>
<main id="main"><div id="wrap"><table id="grid"><thead></thead><tbody></tbody></table></div><aside id="panel" hidden></aside></main>
<section class="extras"><details id="runs"><summary>Runs</summary><div class="scroll" id="runtable"></div></details>@@STATIC@@
<p class="note">Keys: j and k move between tests, 1 to 4 pick the comparison mode, left and right step frames, Escape closes the panel.</p></section>
<script type="application/json" id="metadata">@@META@@</script>
<script type="application/json" id="tests">@@TESTS@@</script>
<script type="application/json" id="images">@@IMAGES@@</script>
<script>@@SCRIPT@@</script></body></html>
"""

SCRIPT = r"""
(function(){
'use strict';
var $=function(id){return document.getElementById(id)};
var META=JSON.parse($('metadata').textContent),TESTS=JSON.parse($('tests').textContent),IMG=JSON.parse($('images').textContent);
var RUNS=META.runs,S={q:'',api:'',grp:'',st:'',bits:'',differ:false,changed:false,on:RUNS.map(function(){return true}),sel:-1,run:0,cmp:'ref',label:'',fi:0,mode:1,amp:8,thr:0,op:50,split:50,zoom:0};
var rows=[],visible=[],collapsed={};
function el(tag,cls,text){var e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e}
function stClass(s){return s==='ok'?'ok':s==='skip'?'skip':'bad'}
function stText(s){return s==='ok'?'pass':s}
$('meta').textContent=META.date+', revision '+META.revision+', built on '+META.machine+', '+TESTS.length+' tests, '+RUNS.length+' runs';
(function(){var t=el('table','plain'),h=el('tr');['host','GPU','adapter under test','bits','executable','started','complete'].forEach(function(c){h.appendChild(el('th',null,c))});t.appendChild(h);
RUNS.forEach(function(r){var tr=el('tr'),a=r.adapter||{};[r.host,r.gpu,(a.name||a.device_under_test||'')+(a.luid?' LUID '+a.luid:'')+(a.vulkan_uuid?' uuid '+a.vulkan_uuid:''),r.bits,r.exe,r.started,r.complete?'yes':'no'].forEach(function(c){tr.appendChild(el('td',null,c))});t.appendChild(tr)});$('runtable').appendChild(t)})();
var apis=[],groups={};TESTS.forEach(function(t){if(apis.indexOf(t.a)<0)apis.push(t.a);(groups[t.a]=groups[t.a]||{})[t.g]=1});
var bar=$('bar');
var q=el('input');q.type='search';q.placeholder='filter: text, id, reason';bar.appendChild(q);
function mkSel(opts,label){var s=el('select');s.title=label;opts.forEach(function(o){var op=el('option',null,o[1]);op.value=o[0];s.appendChild(op)});bar.appendChild(s);return s}
var selApi=mkSel([['','all APIs']].concat(apis.map(function(a){return [a,a]})),'API');
var selGrp=mkSel([['','all groups']],'group');
var bitsList=[];RUNS.forEach(function(r){if(bitsList.indexOf(r.bits)<0)bitsList.push(r.bits)});
var selBits=mkSel([['','all bitness']].concat(bitsList.map(function(b){return [b,'x'+b]})),'bitness');
var selSt=mkSel([['','any status'],['ok','pass'],['fail','fail'],['skip','skip']],'status');
function mkCheck(text){var l=el('label','chip'),c=el('input');c.type='checkbox';l.appendChild(c);l.appendChild(document.createTextNode(text));bar.appendChild(l);return c}
var cDiff=mkCheck('differs between runs'),cChg=META.baseline?mkCheck('changed vs baseline'):null;
var chips=RUNS.map(function(r,i){var l=el('label','chip'),c=el('input');c.type='checkbox';c.checked=true;c.addEventListener('change',function(){S.on[i]=c.checked;l.classList.toggle('off',!c.checked);apply()});l.appendChild(c);l.appendChild(document.createTextNode(r.label));bar.appendChild(l);return l});
var countEl=el('span','count');bar.appendChild(countEl);
function fillGroups(){selGrp.innerHTML='';var o=el('option','', 'all groups');o.value='';selGrp.appendChild(o);var g=S.api?Object.keys(groups[S.api]||{}):[];g.sort().forEach(function(x){var op=el('option',null,x);op.value=x;selGrp.appendChild(op)});S.grp=''}
var thead=document.querySelector('#grid thead'),tbody=document.querySelector('#grid tbody');
var h1=el('tr'),h2=el('tr','cnt');h1.appendChild(el('th',null,'test'));h2.appendChild(el('th',null,''));
var cntCells=[],colHeads=[];RUNS.forEach(function(r,i){var th=el('th','c',r.label);th.style.textAlign='center';h1.appendChild(th);colHeads.push(th);var c=el('th');c.style.textAlign='center';h2.appendChild(c);cntCells.push(c)});
thead.appendChild(h1);thead.appendChild(h2);
var curApi=null,curGrp=null,apiRows={},grpRows={};
TESTS.forEach(function(t,ti){
 if(t.a!==curApi){curApi=t.a;curGrp=null;var ar=el('tr','a');var at=el('td',null,t.a);at.colSpan=RUNS.length+1;ar.appendChild(at);ar.dataset.api=t.a;ar.addEventListener('click',function(){collapsed['a'+t.a]=!collapsed['a'+t.a];apply()});tbody.appendChild(ar);apiRows[t.a]=ar}
 if(t.g!==curGrp){curGrp=t.g;var gr=el('tr','g');var gt=el('td',null,t.g);gt.colSpan=RUNS.length+1;gr.appendChild(gt);var key=t.a+'.'+t.g;gr.addEventListener('click',function(){collapsed['g'+key]=!collapsed['g'+key];apply()});tbody.appendChild(gr);grpRows[key]=gr}
 var tr=el('tr','t');var nt=el('td','n',t.n);nt.title=t.id;var firstBad=null;
 tr.appendChild(nt);t._cells=[];
 t.c.forEach(function(c,ri){var td=el('td','c');if(c===null){td.appendChild(el('span','s none','-'))}else{var s=el('span','s '+stClass(c.s),stText(c.s));if(c.b&&c.b!==c.s){s.classList.add('chg');s.title='baseline: '+stText(c.b)}if(c.r&&c.s!=='ok')s.title=(s.title?s.title+'\n':'')+c.r;td.appendChild(s);if(!firstBad&&c.s!=='ok'&&c.s!=='skip'&&c.r)firstBad=c.r}td.addEventListener('click',function(ev){ev.stopPropagation();openTest(ti,ri)});tr.appendChild(td);t._cells.push(td)});
 if(firstBad){nt.appendChild(el('span','reason',firstBad.length>110?firstBad.slice(0,110)+'...':firstBad))}
 tr.addEventListener('click',function(){openTest(ti,firstRun(t))});t._tr=tr;t._gk=t.a+'.'+t.g;t._search=(t.id+' '+t.c.map(function(c){return c?c.r:''}).join(' ')).toLowerCase();
 var sts={};t.c.forEach(function(c){if(c)sts[c.s]=1});t._differ=Object.keys(sts).length>1;t._chg=t.c.some(function(c){return c&&c.b&&c.b!==c.s});
 if(t._differ)tr.classList.add('diff');tbody.appendChild(tr);rows.push(t)});
function firstRun(t){for(var i=0;i<t.c.length;i++)if(S.on[i]&&t.c[i])return i;for(i=0;i<t.c.length;i++)if(t.c[i])return i;return 0}
function matchStatus(c){if(!S.st)return true;if(S.st==='fail')return c.s!=='ok'&&c.s!=='skip';return c.s===S.st}
function apply(){
 var terms=S.q.toLowerCase().split(/\s+/).filter(Boolean);visible=[];var per=RUNS.map(function(){return {ok:0,bad:0,skip:0}});var gcount={},acount={};
 var bitsOn=RUNS.map(function(r,i){return S.on[i]&&(!S.bits||r.bits===S.bits)});
 rows.forEach(function(t,ti){
  var show=true;
  if(S.api&&t.a!==S.api)show=false;if(S.grp&&t.g!==S.grp)show=false;
  if(show&&terms.length)show=terms.every(function(w){return t._search.indexOf(w)>=0});
  if(show&&(S.st)){show=t.c.some(function(c,i){return c&&bitsOn[i]&&matchStatus(c)})}
  if(show&&S.differ){var ss={};t.c.forEach(function(c,i){if(c&&bitsOn[i])ss[c.s]=1});show=Object.keys(ss).length>1}
  if(show&&S.changed)show=t._chg;
  if(show&&!bitsOn.some(function(b,i){return b&&t.c[i]}))show=false;
  t._show=show;
  var hide=!show||collapsed['a'+t.a]||collapsed['g'+t._gk];
  t._tr.classList.toggle('hidden',!!hide);
  if(show){visible.push(ti);gcount[t._gk]=(gcount[t._gk]||0)+1;acount[t.a]=(acount[t.a]||0)+1;t.c.forEach(function(c,i){if(c&&bitsOn[i]){var k=c.s==='ok'?'ok':c.s==='skip'?'skip':'bad';per[i][k]++}})}
  t._cells.forEach(function(td,i){td.classList.toggle('hidden',!bitsOn[i])})});
 Object.keys(grpRows).forEach(function(k){var n=gcount[k]||0,r=grpRows[k];r.classList.toggle('hidden',!n||!!collapsed['a'+k.split('.')[0]]);r.firstChild.textContent=(collapsed['g'+k]?'+ ':'- ')+k.split('.').slice(1).join('.')+'  ('+n+')'});
 Object.keys(apiRows).forEach(function(k){var n=acount[k]||0,r=apiRows[k];r.classList.toggle('hidden',!n);r.firstChild.textContent=(collapsed['a'+k]?'+ ':'- ')+k+'  ('+n+')'});
 RUNS.forEach(function(r,i){var on=bitsOn[i];colHeads[i].classList.toggle('hidden',!on);cntCells[i].classList.toggle('hidden',!on);cntCells[i].textContent=per[i].ok+' / '+per[i].bad+' / '+per[i].skip;chips[i].classList.toggle('off',!on)});
 countEl.textContent=visible.length+' of '+rows.length+' tests shown. Columns show pass / fail / skip of the shown tests.';
 var span=1+bitsOn.filter(Boolean).length;Array.prototype.forEach.call(document.querySelectorAll('tr.a td,tr.g td'),function(td){td.colSpan=span});
 if(S.sel>=0&&rows[S.sel])rows[S.sel]._tr.classList.add('sel');
}
function onFilter(){S.q=q.value;apply()}
q.addEventListener('input',onFilter);
selApi.addEventListener('change',function(){S.api=selApi.value;fillGroups();apply()});selGrp.addEventListener('change',function(){S.grp=selGrp.value;apply()});
selBits.addEventListener('change',function(){S.bits=selBits.value;apply()});selSt.addEventListener('change',function(){S.st=selSt.value;apply()});
cDiff.addEventListener('change',function(){S.differ=cDiff.checked;apply()});if(cChg)cChg.addEventListener('change',function(){S.changed=cChg.checked;apply()});
var cache={};
function getImage(key){if(!key||!IMG[key])return Promise.resolve(null);if(cache[key])return cache[key];var e=IMG[key];cache[key]=new Promise(function(res){var im=new Image();im.onload=function(){var c=document.createElement('canvas');c.width=e[0];c.height=e[1];var x=c.getContext('2d');x.drawImage(im,0,0);res({w:e[0],h:e[1],canvas:c,data:x.getImageData(0,0,e[0],e[1]).data})};im.onerror=function(){res(null)};im.src='data:image/png;base64,'+e[2]});return cache[key]}
var panel=$('panel'),main=$('main'),P={};
function caps(){var t=rows[S.sel],c=t&&t.c[S.run];return c&&c.c?c.c:[]}
function labels(){var l=[];caps().forEach(function(c){if(l.indexOf(c.l)<0)l.push(c.l)});return l}
function frames(){return caps().filter(function(c){return c.l===S.label})}
function curCap(){var f=frames();return f[Math.min(S.fi,f.length-1)]||null}
function openTest(ti,ri){if(S.sel>=0&&rows[S.sel])rows[S.sel]._tr.classList.remove('sel');S.sel=ti;S.run=ri;var t=rows[ti];t._tr.classList.add('sel');if(S.cmp!=='ref'&&(!t.c[S.cmp]||S.cmp===S.run))S.cmp='ref';var ls=labels();if(ls.indexOf(S.label)<0)S.label=ls[0]||'';S.fi=Math.min(S.fi,Math.max(0,frames().length-1));main.classList.add('open');panel.hidden=false;showRow(t);render()}
function showRow(t){var w=$('wrap'),top=t._tr.offsetTop;if(top<w.scrollTop+60||top>w.scrollTop+w.clientHeight-40)w.scrollTop=Math.max(0,top-w.clientHeight/2)}
function writeHash(){if(S.sel<0||!rows[S.sel]){history.replaceState(null,'',location.pathname+location.search);return}var h='test='+encodeURIComponent(rows[S.sel].id)+'&run='+S.run+'&cmp='+S.cmp+'&mode='+S.mode+'&frame='+S.fi+'&label='+encodeURIComponent(S.label)+'&zoom='+S.zoom+'&split='+S.split+'&op='+S.op+'&amp='+S.amp+'&thr='+S.thr;history.replaceState(null,'','#'+h)}
function readHash(){var h=location.hash.replace(/^#/,'');if(!h)return false;var m={};h.split('&').forEach(function(p){var i=p.indexOf('=');if(i>0)m[p.slice(0,i)]=decodeURIComponent(p.slice(i+1))});if(!m.test)return false;var ti=-1;for(var i=0;i<rows.length;i++)if(rows[i].id===m.test){ti=i;break}if(ti<0)return false;
 ['mode','fi','zoom','split','op','amp','thr'].forEach(function(k){var src=k==='fi'?m.frame:m[k];if(src!=null&&src!==''&&!isNaN(+src))S[k]=+src});S.label=m.label||'';S.cmp=m.cmp==null||m.cmp==='ref'?'ref':+m.cmp;var run=m.run!=null?+m.run:0;
 if(S.sel>=0&&rows[S.sel])rows[S.sel]._tr.classList.remove('sel');S.sel=ti;S.run=rows[ti].c[run]?run:firstRun(rows[ti]);var t=rows[ti];t._tr.classList.add('sel');if(S.cmp!=='ref'&&(!t.c[S.cmp]||S.cmp===S.run))S.cmp='ref';var ls=labels();if(ls.indexOf(S.label)<0)S.label=ls[0]||'';S.fi=Math.min(S.fi,Math.max(0,frames().length-1));main.classList.add('open');panel.hidden=false;showRow(t);renderPanel();return true}
function closePanel(){panel.hidden=true;main.classList.remove('open');if(S.sel>=0&&rows[S.sel])rows[S.sel]._tr.classList.remove('sel');S.sel=-1;writeHash()}
function step(d){if(!visible.length)return;var p=visible.indexOf(S.sel);var n=p<0?(d>0?0:visible.length-1):Math.max(0,Math.min(visible.length-1,p+d));var ti=visible[n];var t=rows[ti];var r=t.c[S.run]?S.run:firstRun(t);openTest(ti,r)}
function modeBtn(n,text,bx){var b=el('button',S.mode===n?'on':'',n+' '+text);b.addEventListener('click',function(){S.mode=n;render()});bx.appendChild(b)}
function slider(bx,text,min,max,key,onc){var l=el('label',null,text+' ');var i=el('input');i.type='range';i.min=min;i.max=max;i.value=S[key];i.addEventListener('input',function(){S[key]=+i.value;draw();writeHash()});l.appendChild(i);bx.appendChild(l)}
function render(){writeHash();renderPanel()}
function renderPanel(){
 var t=rows[S.sel];if(!t)return;panel.innerHTML='';
 var top=el('div','row');var cl=el('button',null,'close');cl.addEventListener('click',closePanel);var pv=el('button',null,'prev (k)');pv.addEventListener('click',function(){step(-1)});var nx=el('button',null,'next (j)');nx.addEventListener('click',function(){step(1)});top.appendChild(cl);top.appendChild(pv);top.appendChild(nx);panel.appendChild(top);
 panel.appendChild(el('h2',null,t.id));
 var cell=t.c[S.run];
 var r1=el('div','row');r1.appendChild(document.createTextNode('device '));var rs=el('select');t.c.forEach(function(c,i){if(c){var o=el('option',null,RUNS[i].label+' ('+stText(c.s)+')');o.value=i;if(i===S.run)o.selected=true;rs.appendChild(o)}});rs.addEventListener('change',function(){S.run=+rs.value;if(S.cmp===S.run)S.cmp='ref';var ls=labels();if(ls.indexOf(S.label)<0)S.label=ls[0]||'';render()});r1.appendChild(rs);
 r1.appendChild(document.createTextNode(' against '));var cs=el('select');var o0=el('option',null,'reference (oracle or host)');o0.value='ref';cs.appendChild(o0);t.c.forEach(function(c,i){if(c&&i!==S.run){var o=el('option',null,RUNS[i].label+' device image');o.value=i;if(String(S.cmp)===String(i))o.selected=true;cs.appendChild(o)}});cs.addEventListener('change',function(){S.cmp=cs.value==='ref'?'ref':+cs.value;render()});r1.appendChild(cs);panel.appendChild(r1);
 var ls=labels();var cap=curCap();
 var r2=el('div','row');if(ls.length>1){r2.appendChild(document.createTextNode('capture '));var lsel=el('select');ls.forEach(function(l){var o=el('option',null,l||'(main)');o.value=l;if(l===S.label)o.selected=true;lsel.appendChild(o)});lsel.addEventListener('change',function(){S.label=lsel.value;S.fi=0;render()});r2.appendChild(lsel)}
 var fr=frames();if(fr.length>1){var pb=el('button',null,'<');pb.addEventListener('click',function(){stepFrame(-1)});var nb=el('button',null,'>');nb.addEventListener('click',function(){stepFrame(1)});r2.appendChild(pb);r2.appendChild(el('span',null,'frame '+(cap?cap.f:'')+' ('+(S.fi+1)+' of '+fr.length+')'));r2.appendChild(nb)}panel.appendChild(r2);
 var r3=el('div','row');modeBtn(1,'side by side',r3);modeBtn(2,'swipe',r3);modeBtn(3,'onion',r3);modeBtn(4,'difference',r3);panel.appendChild(r3);
 var r4=el('div','row');if(S.mode===2)slider(r4,'swipe',0,100,'split');if(S.mode===3)slider(r4,'opacity',0,100,'op');if(S.mode===4){slider(r4,'amplify',1,64,'amp');slider(r4,'threshold',0,64,'thr')}
 var zl=el('label',null,'zoom ');var zi=el('input');zi.type='range';zi.min=1;zi.max=12;zi.value=S.zoom||autoZoom(cap);zi.addEventListener('input',function(){S.zoom=+zi.value;draw();writeHash()});zl.appendChild(zi);r4.appendChild(zl);panel.appendChild(r4);
 var view=el('div');view.id='view';panel.appendChild(view);var ro=el('div');ro.id='readout';ro.textContent='hover the image for pixel values';panel.appendChild(ro);P={view:view,ro:ro};
 if(fr.length>1){var strip=el('div','strip');fr.forEach(function(c,i){var f=el('figure',i===S.fi?'on':'');var im=el('img');im.dataset.key=c.dev||'';f.appendChild(im);f.appendChild(el('figcaption',null,'f'+c.f));f.addEventListener('click',function(){S.fi=i;render()});strip.appendChild(f)});panel.appendChild(strip);lazy(strip)}
 var info=el('table','kv');function kv(a,b){var tr=el('tr');tr.appendChild(el('td',null,a));tr.appendChild(el('td',null,b));info.appendChild(tr)}
 kv('status',stText(cell.s)+(cell.b&&cell.b!==cell.s?' (baseline '+stText(cell.b)+')':''));if(cell.r)kv('reason',cell.r);if(cell.h)kv('host crash evidence',cell.h);kv('time',cell.ms+' ms');
 if(cap){kv('capture',(cap.l||'main')+' frame '+cap.f+', '+cap.w+'x'+cap.h+' '+cap.k);if(cap.t&&cap.t.name){var tt=cap.t;kv('tolerance',tt.name+' (byte delta '+tt.byte_delta+', abs '+tt.absolute+', rel '+tt.relative+', outliers '+tt.outlier_fraction+', edge '+tt.edge_radius+', forgive '+tt.forgive_fraction+', min psnr '+tt.min_psnr+')')}
  if(cap.p!=null)kv('measures','psnr '+(cap.p>900?'inf':cap.p.toFixed(2))+' dB, bad '+cap.b+' of '+cap.n+', max delta '+cap.m+', forgiven '+cap.g+(cap.ok===false?' (failed)':''))}
 if(cell.m){Object.keys(cell.m).forEach(function(k){kv(k,String(cell.m[k]))})}
 var all=el('div');t.c.forEach(function(c,i){if(c){var s=el('span','s '+stClass(c.s),stText(c.s));var ln=el('div','row');ln.appendChild(s);ln.appendChild(document.createTextNode(RUNS[i].label+(c.r&&c.s!=='ok'?': '+c.r:'')));all.appendChild(ln)}});
 panel.insertBefore(info,view);panel.appendChild(el('h2',null,'every run'));panel.appendChild(all);
 draw()}
function autoZoom(cap){var w=cap&&cap.w?cap.w:128;return Math.max(1,Math.min(8,Math.floor(560/(S.mode===1?w*2:w))))}
function stepFrame(d){var f=frames();S.fi=Math.max(0,Math.min(f.length-1,S.fi+d));render()}
function lazy(root){var io=new IntersectionObserver(function(es){es.forEach(function(e){if(e.isIntersecting){var im=e.target;var k=im.dataset.key;if(k&&IMG[k]&&!im.src)im.src='data:image/png;base64,'+IMG[k][2];io.unobserve(im)}})},{root:root});Array.prototype.forEach.call(root.querySelectorAll('img'),function(i){io.observe(i)})}
var token=0;
function draw(){
 var cap=curCap(),view=P.view;if(!view)return;var my=++token;view.innerHTML='';
 if(!cap){view.appendChild(el('p','note','no captured image for this run (skipped, no result, or the report was built with fewer images)'));return}
 var devKey=cap.dev,refKey=null,refName='reference';
 if(S.cmp==='ref'){refKey=cap.ref}else{var oc=rows[S.sel].c[S.cmp];var m=oc&&oc.c?oc.c.filter(function(c){return c.f===cap.f&&c.l===cap.l})[0]:null;refKey=m?m.dev:null;refName=RUNS[S.cmp].label}
 Promise.all([getImage(devKey),getImage(refKey)]).then(function(im){
  if(my!==token)return;var A=im[0],B=im[1];
  if(!A){view.appendChild(el('p','note','no device image stored for this capture'));return}
  if(!B){B=A;view.appendChild(el('p','note','no '+refName+' image is stored for this capture; comparing the device image with itself'))}
  var w=Math.min(A.w,B.w),h=Math.min(A.h,B.h),z=S.zoom||autoZoom(cap),mode=S.mode;
  var cv=document.createElement('canvas');cv.width=mode===1?w*2:w;cv.height=h;var x=cv.getContext('2d');
  if(mode===1){x.drawImage(A.canvas,0,0);x.drawImage(B.canvas,w,0)}
  else if(mode===2){x.drawImage(B.canvas,0,0,w,h,0,0,w,h);var sx=Math.round(w*S.split/100);if(sx>0)x.drawImage(A.canvas,0,0,sx,h,0,0,sx,h);x.fillStyle='#f0f';x.fillRect(sx,0,1,h)}
  else if(mode===3){x.drawImage(A.canvas,0,0);x.globalAlpha=S.op/100;x.drawImage(B.canvas,0,0);x.globalAlpha=1}
  else{var out=x.createImageData(w,h),bad=0;for(var yy=0;yy<h;yy++)for(var xx=0;xx<w;xx++){var ia=(yy*A.w+xx)*4,ib=(yy*B.w+xx)*4,io=(yy*w+xx)*4;var d=0;for(var c=0;c<4;c++){var dd=Math.abs(A.data[ia+c]-B.data[ib+c]);if(dd>d)d=dd}
    var v=Math.min(255,d*S.amp);if(S.thr>0&&d>S.thr){out.data[io]=255;out.data[io+1]=0;out.data[io+2]=0;bad++}else{out.data[io]=v;out.data[io+1]=v;out.data[io+2]=v}out.data[io+3]=255}
   x.putImageData(out,0,0);if(S.thr>0)view.appendChild(el('p','note',bad+' pixels differ by more than '+S.thr))}
  cv.style.width=cv.width*z+'px';cv.style.height=cv.height*z+'px';view.appendChild(cv);
  var names=mode===1?'left: device, right: '+refName:'device: '+(RUNS[S.run].label)+', other: '+refName;view.insertBefore(el('p','note',names),cv);
  cv.addEventListener('mousemove',function(ev){var r=cv.getBoundingClientRect(),px=Math.floor((ev.clientX-r.left)/r.width*cv.width),py=Math.floor((ev.clientY-r.top)/r.height*cv.height);var bx=mode===1?px%w:px;if(px<0||py<0||py>=h||bx>=w)return;
   var ia=(py*A.w+bx)*4,ib=(py*B.w+bx)*4,a=[],b=[],dl=[];for(var c=0;c<4;c++){a.push(A.data[ia+c]);b.push(B.data[ib+c]);dl.push(A.data[ia+c]-B.data[ib+c])}
   P.ro.textContent='x '+bx+' y '+py+'\ndevice '+a.join(',')+'   '+refName+' '+b.join(',')+'   delta '+dl.join(',')});
 })}
document.addEventListener('keydown',function(ev){if(ev.target&&(ev.target.tagName==='INPUT'&&ev.target.type==='search'||ev.target.tagName==='SELECT'))return;if(ev.ctrlKey||ev.metaKey||ev.altKey)return;
 var k=ev.key;if(k==='j'){step(1)}else if(k==='k'){step(-1)}else if(k>='1'&&k<='4'&&S.sel>=0){S.mode=+k;render()}else if(k==='ArrowRight'&&S.sel>=0){stepFrame(1);ev.preventDefault()}else if(k==='ArrowLeft'&&S.sel>=0){stepFrame(-1);ev.preventDefault()}else if(k==='Escape'){closePanel()}});
fillGroups();apply();readHash();window.addEventListener('hashchange',readHash);
})();
"""

if __name__ == "__main__":
    sys.exit(main())
