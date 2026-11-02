import hashlib
import json
import math
import pathlib
import re
import struct
import zlib

MiB = 1 << 20
DEVICE_LOST = {
    "887a0005": "DXGI_ERROR_DEVICE_REMOVED",
    "887a0006": "DXGI_ERROR_DEVICE_HUNG",
    "887a0007": "DXGI_ERROR_DEVICE_RESET",
    "887a0020": "DXGI_ERROR_DRIVER_INTERNAL_ERROR",
    "88760868": "D3DERR_DEVICELOST",
    "88760870": "D3DERR_DEVICEREMOVED",
    "88760874": "D3DERR_DEVICEHUNG",
}
CRASHES = {
    0xC0000005: "access violation",
    0xC0000374: "heap corruption",
    0xC0000409: "stack buffer overrun (fail-fast)",
    0xC000001D: "illegal instruction",
    0xC00000FD: "stack overflow",
    0x80000003: "breakpoint",
}


def image(width, height, rgb):
    if len(rgb) != width * height * 3:
        raise ValueError(f"{width}x{height} needs {width * height * 3} bytes, not {len(rgb)}")
    return (width, height, bytes(rgb))


def parse_pnm_stream(data):
    images = []
    offset = 0
    while offset < len(data):
        while offset < len(data) and data[offset:offset + 1].isspace():
            offset += 1
        if offset >= len(data):
            break
        fields, comment = [], None
        while len(fields) < 4:
            end = data.index(b"\n", offset)
            line = data[offset:end].strip()
            offset = end + 1
            if line.startswith(b"#"):
                comment = line[1:].decode(errors="replace").strip()
                continue
            fields += line.split()
        magic, width, height, scale = fields[0].decode(), int(fields[1]), int(fields[2]), fields[3]
        if magic in ("P6", "P5"):
            channels = 3 if magic == "P6" else 1
            size = width * height * channels
            body = data[offset:offset + size]
            if len(body) != size:
                raise ValueError(f"image {len(images)} is cut short")
            offset += size
            rgb = body if channels == 3 else bytes(value for value in body for _ in range(3))
        elif magic in ("PF", "Pf", "PX"):
            channels = {"PF": 3, "Pf": 1, "PX": 4}[magic]
            size = width * height * channels * 4
            body = data[offset:offset + size]
            if len(body) != size:
                raise ValueError(f"image {len(images)} is cut short")
            offset += size
            endian = "<" if float(scale) < 0 or magic == "PX" else ">"
            values = struct.unpack(f"{endian}{width * height * channels}f", body)
            rgb = bytearray(width * height * 3)
            for pixel in range(width * height):
                for channel in range(3):
                    value = values[pixel * channels + min(channel, channels - 1)]
                    rgb[pixel * 3 + channel] = 0 if value != value else max(0, min(255, round(value * 255)))
            rgb = bytes(rgb)
        else:
            raise ValueError(f"image {len(images)}: unknown PNM type {magic}")
        images.append((comment, image(width, height, rgb)))
    return images


def write_png(path, picture):
    width, height, rgb = picture
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    with open(path, "wb") as written:
        written.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 6))
                      + chunk(b"IEND", b""))


def read_png(path):
    with open(path, "rb") as source:
        data = source.read()
    return decode_png(data)


def decode_png(data):
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    offset, idat, header = 8, [], None
    while offset < len(data):
        length, kind = struct.unpack_from(">I4s", data, offset)
        body = data[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError("a PNG with no IHDR")
    width, height, depth, colour, _, _, interlace = header
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colour)
    if depth != 8 or channels is None or interlace:
        raise ValueError(f"unsupported PNG: depth {depth}, colour type {colour}, interlace {interlace}")
    raw = zlib.decompress(b"".join(idat))
    stride = width * channels
    rows, previous = [], bytes(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, row = raw[start], raw[start + 1:start + 1 + stride]
        if kind == 0:
            current = row
        elif kind == 2:
            current = bytes((a + b) & 255 for a, b in zip(row, previous))
        else:
            current = bytearray(row)
            for x in range(stride):
                left = current[x - channels] if x >= channels else 0
                up = previous[x]
                if kind == 1:
                    current[x] = (current[x] + left) & 255
                elif kind == 3:
                    current[x] = (current[x] + ((left + up) >> 1)) & 255
                elif kind == 4:
                    corner = previous[x - channels] if x >= channels else 0
                    estimate = left + up - corner
                    pa, pb, pc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                    predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else corner)
                    current[x] = (current[x] + predictor) & 255
                else:
                    raise ValueError(f"row {y}: unknown filter {kind}")
            current = bytes(current)
        rows.append(current)
        previous = current
    pixels = b"".join(rows)
    if channels == 3:
        rgb = pixels
    elif channels == 4:
        rgb = bytearray(width * height * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = pixels[0::4], pixels[1::4], pixels[2::4]
    elif channels == 2:
        rgb = bytes(value for value in pixels[0::2] for _ in range(3))
    else:
        rgb = bytes(value for value in pixels for _ in range(3))
    return image(width, height, rgb)


def read_bmp(path):
    with open(path, "rb") as source:
        data = source.read()
    if data[:2] != b"BM":
        raise ValueError("not a BMP")
    pixels_at = struct.unpack_from("<I", data, 10)[0]
    width, height, _, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if bits not in (24, 32) or compression not in (0, 3):
        raise ValueError(f"unsupported BMP: {bits} bits, compression {compression}")
    bottom_up = height > 0
    height = abs(height)
    step = bits // 8
    stride = (width * step + 3) & ~3
    rgb = bytearray(width * height * 3)
    for y in range(height):
        source_row = (height - 1 - y) if bottom_up else y
        row = data[pixels_at + source_row * stride:pixels_at + source_row * stride + width * step]
        out = y * width * 3
        rgb[out + 0:out + width * 3:3] = row[2::step]
        rgb[out + 1:out + width * 3:3] = row[1::step]
        rgb[out + 2:out + width * 3:3] = row[0::step]
    return image(width, height, bytes(rgb))


def compare_images(reference, got, pixel_tolerance=2):
    width, height, a = reference
    other_width, other_height, b = got
    if (width, height) != (other_width, other_height):
        return {"size": f"{other_width}x{other_height}, not {width}x{height}"}
    if a == b:
        return {"max": 0, "changed_pct": 0.0, "psnr": math.inf}
    stride = width * 3
    worst, changed, squared = 0, 0, 0
    for y in range(height):
        row_a, row_b = a[y * stride:(y + 1) * stride], b[y * stride:(y + 1) * stride]
        if row_a == row_b:
            continue
        diffs = [abs(p - q) for p, q in zip(row_a, row_b)]
        squared += sum(d * d for d in diffs)
        for x in range(0, stride, 3):
            pixel = max(diffs[x], diffs[x + 1], diffs[x + 2])
            if pixel > pixel_tolerance:
                changed += 1
            if pixel > worst:
                worst = pixel
    mse = squared / (width * height * 3)
    return {"max": worst, "changed_pct": 100.0 * changed / (width * height),
            "psnr": math.inf if mse == 0 else 10 * math.log10(255 * 255 / mse)}


def difference_image(reference, got):
    width, height, a = reference
    _, _, b = got
    heat = bytearray(width * height * 3)
    for pixel in range(width * height):
        value = min(255, 8 * max(abs(a[pixel * 3 + c] - b[pixel * 3 + c]) for c in range(3)))
        heat[pixel * 3:pixel * 3 + 3] = bytes((value, value // 2, 0 if value else 40))
    return image(width, height, bytes(heat))


def picture_verdict(stats, limits):
    if "size" in stats:
        return "differs"
    if stats["max"] == 0:
        return "identical"
    if stats["psnr"] >= limits["psnr_min"] and stats["changed_pct"] <= limits["changed_max_pct"]:
        return "matches"
    if stats["psnr"] >= limits["psnr_warn"]:
        return "close"
    return "differs"


def describe(stats):
    if "size" in stats:
        return f"size {stats['size']}"
    psnr = "inf" if stats["psnr"] == math.inf else f"{stats['psnr']:.1f} dB"
    return f"PSNR {psnr}, {stats['changed_pct']:.2f}% of pixels differ, worst channel {stats['max']}"


def retrace_fps(text):
    found = re.search(r"Rendered (\d+) frames in ([\d.]+) secs?, average of ([\d.]+) fps", text)
    return (int(found.group(1)), float(found.group(2)), float(found.group(3))) if found else None


def device_lost(text):
    seen = []
    for code in re.findall(r"0x([0-9a-fA-F]{8})", text):
        name = DEVICE_LOST.get(code.lower())
        if name and name not in seen:
            seen.append(name)
    for name in DEVICE_LOST.values():
        if name in text and name not in seen:
            seen.append(name)
    return seen


def retrace_warnings(text):
    return [line.strip() for line in text.splitlines() if re.match(r"^\s*\d+: (warning|error):", line)]


def exit_description(code):
    if code is None:
        return "no exit code"
    unsigned = code & 0xFFFFFFFF
    name = CRASHES.get(unsigned)
    if name:
        return f"exit 0x{unsigned:08X} ({name})"
    return f"exit {code}" if unsigned < 0x80000000 else f"exit 0x{unsigned:08X}"


def memory_growth(samples):
    if not samples:
        return None, None
    private = [sample[1] for sample in samples if sample[1]]
    if not private:
        return None, None
    peak = max(private) / MiB
    if len(private) < 10:
        return None, round(peak, 1)

    def median(values):
        ordered = sorted(values)
        return ordered[len(ordered) // 2]

    third = len(private) // 3
    early = median(private[third:third + max(1, len(private) // 10)])
    late = median(private[-max(1, len(private) // 10):])
    return round((late - early) / MiB, 1), round(peak, 1)


def judge_process(label, result, judge):
    failures, metrics = [], {}
    if result is None:
        return [{"case": label, "detail": "the replay left no result"}], metrics
    if result.get("timed_out"):
        failures.append({"case": label, "detail": f"still running after {result.get('seconds', 0):.0f} s"})
    elif result.get("exit_code") != 0:
        failures.append({"case": label, "detail": exit_description(result.get("exit_code"))
                                                  + (f": {result['error']}" if result.get("error") else "")})
    lost = device_lost(result.get("output", ""))
    if lost:
        failures.append({"case": label, "detail": f"the device was lost: {', '.join(lost)}"})
    growth, peak = memory_growth(result.get("samples", []))
    if growth is not None:
        metrics["memory_growth_mib"] = growth
        if judge.get("memory_growth_mib_max") is not None and growth > judge["memory_growth_mib_max"]:
            failures.append({"case": label, "detail": f"private memory grew {growth:.0f} MiB during the replay "
                                                      f"(limit {judge['memory_growth_mib_max']})"})
    if peak is not None:
        metrics["peak_private_mib"] = peak
    if result.get("seconds") is not None:
        metrics["seconds"] = round(result["seconds"], 1)
    return failures, metrics


DRIVER_OF_API = {"d3d8": "avumd", "d3d9": "avumd", "dxgi": "avumd", "d3d12": "avumd", "gl": "avgl", "vulkan": "avvk"}


def judge_driver(label, result, api, warp=False):
    if not result or api not in DRIVER_OF_API:
        return [], [], []
    modules = [name.lower() for name in result.get("modules") or []]
    if not modules:
        return [], [{"case": label, "detail": "the replay's DLLs could not be seen: which driver it used is unknown"}], []
    ours = [name for name in modules if name.startswith(DRIVER_OF_API[api])]
    warped = [name for name in modules if name.startswith("d3d10warp")]
    if warp:
        return ([] if warped else [{"case": label, "detail": f"the WARP pass did not load WARP: {', '.join(modules)}"}],
                [], warped)
    failures = []
    if not ours:
        failures.append({"case": label, "detail": f"rendered without avgpu ({DRIVER_OF_API[api]}*.dll not loaded); "
                                                  f"loaded {', '.join(modules)}"})
    if warped:
        failures.append({"case": label, "detail": "rendered with WARP (d3d10warp.dll), not avgpu"})
    return failures, [], ours


def judge_frames(frames, references, judge):
    failures, notes, worst_psnr, worst_changed = [], [], math.inf, 0.0
    for frame, got in sorted(frames.items()):
        if got is None:
            failures.append({"case": f"frame {frame}", "detail": "the replay produced no picture for it"})
            continue
        reference = references.get(frame)
        if reference is None:
            notes.append({"case": f"frame {frame}", "detail": "no reference picture: promote one (traces.py promote)"})
            continue
        stats = compare_images(reference, got, judge["pixel_tolerance"])
        label = picture_verdict(stats, judge)
        if label == "differs":
            failures.append({"case": f"frame {frame}", "detail": describe(stats)})
        elif label == "close":
            notes.append({"case": f"frame {frame}", "detail": describe(stats)})
        if "size" not in stats:
            worst_psnr = min(worst_psnr, stats["psnr"])
            worst_changed = max(worst_changed, stats["changed_pct"])
    metrics = {}
    if any(frame in references for frame in frames):
        metrics["min_psnr"] = "inf" if worst_psnr == math.inf else round(worst_psnr, 1)
        metrics["max_changed_pct"] = round(worst_changed, 3)
    return failures, notes, metrics


def judge_fps(fps, judge):
    if fps is None or judge.get("min_fps") is None or fps >= judge["min_fps"]:
        return []
    return [{"case": "speed", "detail": f"{fps:.1f} fps, below the {judge['min_fps']} fps floor"}]


def judge_opencl(result, judge, platform=None):
    failures, notes = [], []
    enqueues = result.get("enqueues", [])
    programs = result.get("programs", [])
    if not enqueues and not programs:
        failures.append({"case": "opencl", "detail": result.get("error") or "the capture has no kernel to replay"})
    built = 0
    for program in programs:
        name = f"program {program.get('name', '?')}"
        ran_on = program.get("ran_on")
        if platform and (ran_on is None or platform.lower() not in ran_on.lower()):
            failures.append({"case": name, "detail": f"built on {ran_on or 'an unknown platform'}"
                                                     f", not {platform}: " + " | ".join(
                                                         (program.get("output") or "").strip().splitlines()[-2:])})
        elif program.get("exit_code") != 0:
            failed = re.search(r"BUILD FAILED: (.*)", program.get("output") or "")
            detail = failed.group(1)[:300] if failed else exit_description(program.get("exit_code")) + ": " + \
                " | ".join((program.get("output") or "").strip().splitlines()[-3:])
            failures.append({"case": name, "detail": f"does not build: {detail}"})
        else:
            built += 1
    identical, seconds, worst = 0, 0.0, 0.0
    for enqueue in enqueues:
        name = enqueue.get("name", "?")
        seconds += enqueue.get("seconds") or 0.0
        if enqueue.get("exit_code") != 0:
            tail = " | ".join((enqueue.get("output") or "").strip().splitlines()[-3:])
            failures.append({"case": name, "detail": f"{exit_description(enqueue.get('exit_code'))}: {tail}"})
            continue
        ran_on = enqueue.get("ran_on")
        if platform and (ran_on is None or platform.lower() not in ran_on.lower()):
            failures.append({"case": name, "detail": f"ran on {ran_on or 'an unknown platform'}, "
                                                     f"not {platform}"})
            continue
        all_equal = True
        for argument in enqueue.get("arguments", []):
            if argument.get("missing"):
                failures.append({"case": f"{name} arg {argument.get('arg')}", "detail": "run.py wrote no output"})
                all_equal = False
                continue
            if argument.get("identical"):
                continue
            all_equal = False
            elements = argument.get("elements") or 1
            fraction = (argument.get("bad") or 0) / elements
            worst = max(worst, fraction)
            detail = (f"{argument.get('bad')} of {elements} {argument.get('kind')} elements beyond tolerance"
                      f" (max abs {argument.get('max_abs')}, max rel {argument.get('max_rel')})")
            if argument.get("size_mismatch"):
                failures.append({"case": f"{name} arg {argument.get('arg')}",
                                 "detail": f"{argument.get('size_mismatch')}"})
            elif fraction > judge["max_bad_fraction"]:
                failures.append({"case": f"{name} arg {argument.get('arg')}", "detail": detail})
            elif argument.get("bad"):
                notes.append({"case": f"{name} arg {argument.get('arg')}", "detail": detail})
        identical += all_equal
    metrics = {"kernels": len(enqueues), "kernels_bit_identical": identical, "seconds": round(seconds, 1),
               "worst_bad_fraction": round(worst, 6)}
    if programs:
        metrics.update(programs=len(programs), programs_built=built)
    return failures, notes, metrics


def output_hashes(result):
    return {f"{enqueue.get('name')} arg {argument.get('arg')}": argument["sha256"]
            for enqueue in result.get("enqueues", []) for argument in enqueue.get("arguments", [])
            if argument.get("sha256")}


def across_gpus(passes, judge_of):
    rows = []
    trace_ids = sorted({trace_id for _, traces in passes for trace_id in traces})
    for trace_id in trace_ids:
        limits = judge_of(trace_id)
        frame_numbers = sorted({int(frame) for _, traces in passes
                                for frame in traces.get(trace_id, {}).get("frames", {})})
        for frame in frame_numbers:
            base_label, base = None, None
            cells, bad = {}, False
            for label, traces in passes:
                path = traces.get(trace_id, {}).get("frames", {}).get(str(frame)) \
                    or traces.get(trace_id, {}).get("frames", {}).get(frame)
                if path is None:
                    cells[label] = "-"
                    continue
                try:
                    picture = read_png(path)
                except (OSError, ValueError) as error:
                    cells[label] = f"unreadable ({error})"
                    bad = True
                    continue
                if base is None:
                    base_label, base = label, picture
                    cells[label] = "base"
                    continue
                stats = compare_images(base, picture, limits["pixel_tolerance"])
                verdict = picture_verdict(stats, limits)
                cells[label] = verdict if verdict == "identical" else f"{verdict}: {describe(stats)}"
                bad |= verdict == "differs"
            rows.append((trace_id, f"frame {frame} (vs {base_label})", cells, bad))
        hashed = [(label, traces[trace_id]["hashes"]) for label, traces in passes
                  if traces.get(trace_id, {}).get("hashes")]
        if hashed:
            base_label, base = hashed[0]
            cells = {label: ("base" if label == base_label else
                             "identical" if hashes == base else
                             f"{sum(1 for key in base if hashes.get(key) != base[key])} of {len(base)} outputs differ")
                     for label, hashes in hashed}
            rows.append((trace_id, f"OpenCL outputs (vs {base_label})", cells,
                         any(text not in ("base", "identical") for text in cells.values())))
    return rows


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def cross_gpu_section(run):
    passes, limits = [], {}
    for results_path in sorted(pathlib.Path(run).glob("*/*/results.json")):
        try:
            result = json.loads(results_path.read_text())
        except (OSError, ValueError):
            continue
        label = f"{results_path.parent.parent.name} / {result['gpu']['name']}"
        traces = {}
        for summary_path in sorted((results_path.parent / "traces").glob("*/*/trace.json")):
            trace_id = f"{summary_path.parent.parent.name}/{summary_path.parent.name}"
            summary = json.loads(summary_path.read_text())
            traces[trace_id] = {"frames": {frame: summary_path.parent / name
                                           for frame, name in summary.get("frames", {}).items()},
                                "hashes": summary.get("hashes", {})}
            limits.setdefault(trace_id, summary.get("judge") or {})
        if traces:
            passes.append((label, traces))
    if not passes:
        return []
    from trace_store import DEFAULT_JUDGE
    rows = across_gpus(passes, lambda trace_id: {**DEFAULT_JUDGE, **limits.get(trace_id, {})})
    labels = [label for label, _ in passes]
    lines = ["## Trace frames across GPUs", "", "| trace | what | " + " | ".join(labels) + " |",
             "|---|---|" + "---|" * len(labels)]
    for trace_id, item, cells, bad in rows:
        marked = f"**{item}**" if bad else item
        lines.append(f"| {trace_id} | {marked} | " + " | ".join(cells.get(label, "-") for label in labels) + " |")
    lines.append("")
    return lines
