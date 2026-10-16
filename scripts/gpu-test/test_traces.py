"""The traces suite's judging, store and guest-side comparison, without a VM:
python3 -m unittest test_traces (from scripts/gpu-test)."""
import argparse
import hashlib
import json
import math
import pathlib
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE / "guest"))
import gpu_matrix  # noqa: E402,F401 - puts accel-virt's tools on the path, as a run does
import clreplay  # noqa: E402
import trace_judge as judge  # noqa: E402
import trace_mend  # noqa: E402
import trace_store  # noqa: E402
import trace_suite  # noqa: E402
import traces  # noqa: E402

LIMITS = dict(trace_store.DEFAULT_JUDGE)


def solid(width, height, rgb):
    return judge.image(width, height, bytes(rgb) * (width * height))


def with_pixels(picture, changes):
    width, height, data = picture
    data = bytearray(data)
    for (x, y), rgb in changes.items():
        data[(y * width + x) * 3:(y * width + x) * 3 + 3] = bytes(rgb)
    return judge.image(width, height, bytes(data))


def pnm(picture, comment):
    width, height, rgb = picture
    return f"P6\n#{comment}\n{width} {height}\n255\n".encode() + rgb


def png_with_filters(picture, filters):
    """A PNG whose rows use `filters` in turn, encoded the way libpng would."""
    width, height, rgb = picture
    stride = width * 3
    raw, previous = bytearray(), bytes(stride)
    for y in range(height):
        row = rgb[y * stride:(y + 1) * stride]
        kind = filters[y % len(filters)]
        encoded = bytearray(stride)
        for x in range(stride):
            left = row[x - 3] if x >= 3 else 0
            up = previous[x]
            corner = previous[x - 3] if x >= 3 else 0
            if kind == 0:
                predictor = 0
            elif kind == 1:
                predictor = left
            elif kind == 2:
                predictor = up
            elif kind == 3:
                predictor = (left + up) >> 1
            else:
                estimate = left + up - corner
                pa, pb, pc = abs(estimate - left), abs(estimate - up), abs(estimate - corner)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else corner)
            encoded[x] = (row[x] - predictor) & 255
        raw += bytes([kind]) + encoded
        previous = row

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


class Images(unittest.TestCase):
    def test_pnm_stream_keeps_each_snapshots_call_number(self):
        red, blue = solid(4, 2, (255, 0, 0)), solid(3, 3, (0, 0, 255))
        images = judge.parse_pnm_stream(pnm(red, 1200) + pnm(blue, 4400))
        self.assertEqual([comment for comment, _ in images], ["1200", "4400"])
        self.assertEqual(images[0][1], red)
        self.assertEqual(images[1][1], blue)

    def test_pnm_stream_cut_short_is_an_error(self):
        with self.assertRaises(ValueError):
            judge.parse_pnm_stream(pnm(solid(4, 4, (1, 2, 3)), 7)[:-5])

    def test_float_pnm_is_clamped_to_eight_bits(self):
        body = struct.pack("<6f", 1.0, 0.5, 0.0, 2.0, -1.0, float("nan"))
        (comment, picture), = judge.parse_pnm_stream(b"PF\n#9\n2 1\n-1\n" + body)
        self.assertEqual(comment, "9")
        self.assertEqual(picture[2], bytes([255, 128, 0, 255, 0, 0]))

    def test_png_round_trip(self):
        picture = with_pixels(solid(5, 4, (10, 20, 30)), {(1, 1): (200, 100, 50), (4, 3): (0, 255, 0)})
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "frame.png"
            judge.write_png(path, picture)
            self.assertEqual(judge.read_png(path), picture)

    def test_png_every_filter_decodes(self):
        picture = judge.image(7, 5, bytes((x * 37 + y * 11) & 255 for y in range(5) for x in range(21)))
        decoded = judge.decode_png(png_with_filters(picture, [0, 1, 2, 3, 4]))
        self.assertEqual(decoded, picture)

    def test_bmp_bottom_up_is_read_top_row_first(self):
        width, height = 3, 2
        rows = [bytes([0, 0, 255] * width) + b"\x00" * 3, bytes([255, 0, 0] * width) + b"\x00" * 3]  # BGR, padded
        pixels = rows[1] + rows[0]  # bottom-up: the last row first
        header = b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
        info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "shot.bmp"
            path.write_bytes(header + info + pixels)
            picture = judge.read_bmp(path)
        self.assertEqual(picture[:2], (3, 2))
        self.assertEqual(picture[2][:3], bytes([255, 0, 0]))   # top row red
        self.assertEqual(picture[2][-3:], bytes([0, 0, 255]))  # bottom row blue


class Pictures(unittest.TestCase):
    def test_identical_frames(self):
        picture = solid(8, 8, (1, 2, 3))
        stats = judge.compare_images(picture, picture)
        self.assertEqual(stats, {"max": 0, "changed_pct": 0.0, "psnr": math.inf})
        self.assertEqual(judge.picture_verdict(stats, LIMITS), "identical")

    def test_rounding_differences_match(self):
        reference = solid(20, 20, (100, 100, 100))
        got = with_pixels(reference, {(x, 0): (101, 99, 100) for x in range(20)})
        stats = judge.compare_images(reference, got)
        self.assertEqual(stats["changed_pct"], 0.0)  # within the 2-step pixel tolerance
        self.assertEqual(judge.picture_verdict(stats, LIMITS), "matches")

    def test_a_wrong_region_differs(self):
        reference = solid(20, 20, (0, 0, 0))
        got = with_pixels(reference, {(x, y): (255, 255, 255) for x in range(10) for y in range(10)})
        stats = judge.compare_images(reference, got)
        self.assertEqual(stats["changed_pct"], 25.0)
        self.assertEqual(stats["max"], 255)
        self.assertEqual(judge.picture_verdict(stats, LIMITS), "differs")

    def test_size_mismatch_differs(self):
        stats = judge.compare_images(solid(4, 4, (0, 0, 0)), solid(4, 5, (0, 0, 0)))
        self.assertEqual(judge.picture_verdict(stats, LIMITS), "differs")
        self.assertIn("size", judge.describe(stats))


class Process(unittest.TestCase):
    def test_retrace_rate(self):
        self.assertEqual(judge.retrace_fps("Rendered 600 frames in 4.5 secs, average of 133.333 fps\n"),
                         (600, 4.5, 133.333))
        self.assertIsNone(judge.retrace_fps("nothing"))

    def test_device_removed_is_found_once(self):
        text = ("1234: warning: failed with 0x887a0005 (DXGI_ERROR_DEVICE_REMOVED): gone\n"
                "1240: warning: failed with 0x887a0005 (DXGI_ERROR_DEVICE_REMOVED): gone\n")
        self.assertEqual(judge.device_lost(text), ["DXGI_ERROR_DEVICE_REMOVED"])
        self.assertEqual(judge.device_lost("1: warning: failed with 0x80070057 (E_INVALIDARG)"), [])

    def test_warnings_are_retrace_lines_only(self):
        text = "12: warning: mismatch in x\nRendered 1 frames\n  99: error: no such call\n"
        self.assertEqual(len(judge.retrace_warnings(text)), 2)

    def test_crash_codes_are_named(self):
        self.assertEqual(judge.exit_description(-1073741819), "exit 0xC0000005 (access violation)")
        self.assertEqual(judge.exit_description(3), "exit 3")

    def test_flat_memory_does_not_grow(self):
        samples = [[t * 0.5, 800 << 20, 0] for t in range(100)]
        self.assertEqual(judge.memory_growth(samples), (0.0, 800.0))

    def test_a_leak_grows(self):
        # Loading to 500 MiB, then a leak of 8 MiB a sample, as Dark Souls
        # Remastered's 1 MiB a frame looked.
        samples = [[t, min(t, 10) * (50 << 20) + max(0, t - 10) * (8 << 20), 0] for t in range(100)]
        growth, peak = judge.memory_growth(samples)
        self.assertGreater(growth, 200)
        result = {"exit_code": 0, "seconds": 50.0, "samples": samples, "output": ""}
        failures, metrics = judge.judge_process("benchmark", result, LIMITS)
        self.assertEqual([f["case"] for f in failures], ["benchmark"])
        self.assertIn("grew", failures[0]["detail"])
        self.assertEqual(metrics["peak_private_mib"], peak)

    def test_a_timeout_and_a_lost_device_both_fail(self):
        result = {"exit_code": None, "timed_out": True, "seconds": 900.0, "samples": [],
                  "output": "77: warning: failed with 0x88760870 (D3DERR_DEVICEREMOVED)"}
        failures, _ = judge.judge_process("check", result, LIMITS)
        self.assertEqual(len(failures), 2)

    def test_no_result_fails(self):
        failures, _ = judge.judge_process("check", None, LIMITS)
        self.assertEqual(failures[0]["detail"], "the replay left no result")

    def test_speed_floor(self):
        self.assertEqual(judge.judge_fps(30.0, dict(LIMITS, min_fps=None)), [])
        self.assertEqual(len(judge.judge_fps(30.0, dict(LIMITS, min_fps=60))), 1)


class Driver(unittest.TestCase):
    def test_avgpu_must_be_loaded(self):
        failures, notes, seen = judge.judge_driver("check", {"modules": ["avumd32.dll", "d3d9.dll"]}, "d3d9")
        self.assertEqual((failures, notes, seen), ([], [], ["avumd32.dll"]))
        failures, _, _ = judge.judge_driver("check", {"modules": ["d3d11.dll", "d3d10warp.dll"]}, "dxgi")
        self.assertEqual(len(failures), 2)
        failures, _, _ = judge.judge_driver("check", {"modules": ["opengl32.dll"]}, "gl")
        self.assertIn("avgl", failures[0]["detail"])

    def test_the_warp_pass_must_load_warp(self):
        self.assertEqual(judge.judge_driver("warp", {"modules": ["d3d10warp.dll"]}, "dxgi", warp=True)[0], [])
        self.assertEqual(len(judge.judge_driver("warp", {"modules": ["avumd.dll"]}, "dxgi", warp=True)[0]), 1)

    def test_unseen_modules_are_a_note(self):
        failures, notes, _ = judge.judge_driver("check", {"modules": []}, "d3d9")
        self.assertEqual((failures, len(notes)), ([], 1))

    def test_opencl_must_run_on_avgpu(self):
        result = {"enqueues": [{"name": "Enqueue_1_k", "exit_code": 0, "ran_on": "Intel(R) OpenCL", "output": "Running on platform: Intel(R) OpenCL\n",
                                "arguments": []},
                               {"name": "Enqueue_2_k", "exit_code": 0,
                                "ran_on": "avgpu OpenCL on Intel(R) Arc", "output": "Running on platform: avgpu OpenCL on Intel(R) Arc\n", "arguments": []}]}
        failures, _, _ = judge.judge_opencl(result, LIMITS, "avgpu")
        self.assertEqual([f["case"] for f in failures], ["Enqueue_1_k"])


class Frames(unittest.TestCase):
    def test_missing_differing_and_unreferenced_frames(self):
        reference = solid(10, 10, (0, 0, 0))
        wrong = solid(10, 10, (255, 0, 0))
        failures, notes, metrics = judge.judge_frames({10: None, 20: wrong, 30: reference, 40: reference},
                                                      {20: reference, 30: reference}, LIMITS)
        self.assertEqual([f["case"] for f in failures], ["frame 10", "frame 20"])
        self.assertEqual([n["case"] for n in notes], ["frame 40"])
        self.assertEqual(metrics["max_changed_pct"], 100.0)

    def test_stream_frames_map_back_from_call_numbers(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "check.out"
            path.write_bytes(pnm(solid(2, 2, (1, 1, 1)), 500) + pnm(solid(2, 2, (2, 2, 2)), 900))
            pictures = trace_suite.frames_of_stream(path, {500: 3, 900: 7})
        self.assertEqual(sorted(pictures), [3, 7])
        self.assertEqual(pictures[7][2][:3], bytes([2, 2, 2]))


class OpenCl(unittest.TestCase):
    def test_floats_within_tolerance_are_good(self):
        reference = struct.pack("<4f", 1.0, 2.0, 1000.0, float("nan"))
        got = struct.pack("<4f", 1.0000001, 2.0, 1000.5, float("nan"))
        result = clreplay.compare(reference, got, "float", 1e-3, 1e-5, 0)
        self.assertEqual((result["identical"], result["elements"], result["bad"]), (False, 4, 0))

    def test_floats_beyond_tolerance_are_counted(self):
        reference = struct.pack("<3f", 1.0, 2.0, 3.0)
        got = struct.pack("<3f", 1.5, 2.0, float("nan"))
        result = clreplay.compare(reference, got, "float", 1e-3, 1e-5, 0)
        self.assertEqual(result["bad"], 2)
        self.assertAlmostEqual(result["max_abs"], 0.5)

    def test_bytes_compare_with_itol(self):
        result = clreplay.compare(bytes([10, 20, 30]), bytes([11, 20, 33]), "bytes", 0, 0, 1)
        self.assertEqual((result["bad"], result["max_abs"]), (1, 3.0))

    def test_size_mismatch(self):
        result = clreplay.compare(b"\0" * 8, b"\0" * 4, "float", 0, 0, 0)
        self.assertIn("size_mismatch", result)

    def test_the_platform_is_read_before_the_output_is_cut(self):
        output = "Running on platform: avgpu OpenCL on NVIDIA\n" + "Warning: a kernel is inlined\n" * 500
        self.assertEqual(clreplay.ran_on(output), "avgpu OpenCL on NVIDIA")
        self.assertIsNone(clreplay.ran_on(output[-4000:]))

    def test_argument_kinds_from_types_and_image_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            (directory / "ArgumentDataTypes.txt").write_text("float*\nconst uchar4*\nint\nhalf8*\nimage2d_t\n")
            (directory / "Image_MetaData_4.txt").write_text("\n".join(["64", "64", "1", "0", "0", "0",
                                                                      str(0x10DE), str(0x10B5), str(0x10F1)]))
            kinds = clreplay.argument_kinds(directory)
        self.assertEqual(kinds, {0: "float", 1: "bytes", 2: "bytes", 3: "half", 4: "float"})

    def test_a_numbered_dump_pairs_with_what_run_py_wrote(self):
        # CLIntercept 3.0.6 dumps ..._Arg_4_Buffer_0000.bin and ..._Arg_7_Image_0006_1x1x1_32bpp.raw;
        # its run.py writes ..._Arg_4_Buffer.bin and ..._Arg_7_Image.raw.
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            (directory / "Post").mkdir()
            (directory / "Test").mkdir()
            (directory / "ArgumentDataTypes.txt").write_text("int\nchar*\n")
            (directory / "Post" / "Enqueue_0000_Kernel_k_Arg_1_Buffer_0000.bin").write_bytes(b"\1\2\3")
            (directory / "Test" / "Enqueue_0000_Kernel_k_Arg_1_Buffer.bin").write_bytes(b"\1\2\3")
            (directory / "Post" / "Enqueue_0000_Kernel_k_Arg_2_Image_0006_1x1x1_32bpp.raw").write_bytes(b"\0")
            (directory / "Test" / "Enqueue_0000_Kernel_k_Arg_2_Image.raw").write_bytes(b"\1")
            (directory / "Post" / "Enqueue_0000_Kernel_k_Arg_3_Buffer_0002.bin").write_bytes(b"\0")
            arguments = clreplay.judge_enqueue(directory, argparse.Namespace(rtol=0, atol=0, itol=0))
        self.assertEqual([(a["arg"], a.get("identical"), a.get("missing")) for a in arguments],
                         [(1, True, None), (2, False, None), (3, None, True)])

    def test_judging_a_replay(self):
        result = {"enqueues": [
            {"name": "Enqueue_1_blur", "exit_code": 0, "seconds": 1.0,
             "arguments": [{"arg": 0, "identical": True, "sha256": "a"},
                           {"arg": 1, "identical": False, "kind": "float", "elements": 1000, "bad": 3,
                            "max_abs": 0.1, "max_rel": 0.2, "sha256": "b"}]},
            {"name": "Enqueue_2_sharpen", "exit_code": 1, "seconds": 0.5, "output": "clBuildProgram failed"},
            {"name": "Enqueue_3_copy", "exit_code": 0, "seconds": 0.1, "arguments": [{"arg": 2, "missing": True}]},
        ]}
        failures, notes, metrics = judge.judge_opencl(result, LIMITS)
        self.assertEqual([f["case"] for f in failures],
                         ["Enqueue_1_blur arg 1", "Enqueue_2_sharpen", "Enqueue_3_copy arg 2"])
        self.assertEqual(metrics["kernels"], 3)
        failures, notes, _ = judge.judge_opencl(result, dict(LIMITS, max_bad_fraction=0.01))
        self.assertEqual([n["case"] for n in notes], ["Enqueue_1_blur arg 1"])
        self.assertEqual(judge.output_hashes(result), {"Enqueue_1_blur arg 0": "a", "Enqueue_1_blur arg 1": "b"})

    def test_programs_pair_with_their_build_options(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = pathlib.Path(directory)
            (directory / "CLI_0a1b_source.cl").write_text("kernel void k() {}")
            (directory / "CLI_0a1b_0000_77aa_options.txt").write_text("-cl-fast-relaxed-math\n")
            (directory / "CLI_0a1b_0001_88bb_compile_options.txt").write_text("-x")
            (directory / "CLI_ffff_source.cl").write_text("kernel void j() {}")
            builds = clreplay.program_builds(directory)
        self.assertEqual([(source.name, options, label) for source, options, label in builds],
                         [("CLI_0a1b_source.cl", "-cl-fast-relaxed-math", "CLI_0a1b_0000_77aa"),
                          ("CLI_ffff_source.cl", "", "CLI_ffff")])

    def test_a_program_that_does_not_build_fails(self):
        result = {"enqueues": [], "programs": [
            {"name": "CLI_0a1b_0000_77aa", "exit_code": 1,
             "ran_on": "avgpu OpenCL on Intel(R) Arc", "output": "Running on platform: avgpu OpenCL on Intel(R) Arc\nBUILD FAILED: RuntimeError: clBuildProgram failed: OUT_OF_RESOURCES"},
            {"name": "CLI_ffff", "exit_code": 0, "ran_on": "avgpu OpenCL on Intel(R) Arc", "output": "Running on platform: avgpu OpenCL on Intel(R) Arc\nbuilt 1 kernel(s)"}]}
        failures, _, metrics = judge.judge_opencl(result, LIMITS, "avgpu")
        self.assertEqual([f["case"] for f in failures], ["program CLI_0a1b_0000_77aa"])
        self.assertIn("OUT_OF_RESOURCES", failures[0]["detail"])
        self.assertEqual((metrics["programs"], metrics["programs_built"]), (2, 1))

    def test_an_empty_capture_fails(self):
        failures, _, _ = judge.judge_opencl({"enqueues": [], "error": "pyopencl: no platform"}, LIMITS)
        self.assertEqual(failures[0]["detail"], "pyopencl: no platform")


def manifest_data(**overrides):
    data = {"schema": 1, "app": "hl2", "functionality": "trainstation", "api": "d3d9", "capture": "capture.trace",
            "files": {"capture.trace": {"size": 4, "sha256": "x"}}, "check": {"frames": [10, -1]}}
    data.update(overrides)
    return data


class Store(unittest.TestCase):
    def test_a_good_manifest_has_no_problem(self):
        self.assertEqual(trace_store.Manifest(manifest_data(), "/x").problems(), [])

    def test_bad_manifests_say_why(self):
        problems = trace_store.Manifest(manifest_data(app="HL2", api="metal", capture="../x", files={},
                                                      judge={"psnr": 3, "reference": "warp"}), "/x").problems()
        text = " ".join(problems)
        for expected in ("lower-case", "api", "capture", "files", "unknown keys"):
            self.assertIn(expected, text)

    def test_warp_needs_dxgi(self):
        problems = trace_store.Manifest(manifest_data(judge={"reference": "warp"}), "/x").problems()
        self.assertTrue(any("WARP" in problem for problem in problems))
        self.assertEqual(trace_store.Manifest(manifest_data(api="dxgi", judge={"reference": "warp"}),
                                              "/x").problems(), [])

    def test_frames(self):
        self.assertEqual(trace_store.parse_frames("12 IDirect3DDevice9::Present()\nnoise\n 40 x\n"), [12, 40])
        self.assertEqual(trace_store.resolve_frames([10, -1, 500, -600], 100), ([10, 99], [500, -600]))
        self.assertEqual(trace_store.default_frames(1000), [100, 325, 550, 774, 999])
        self.assertEqual(trace_store.default_frames(3), [0, 1, 2])
        self.assertEqual(trace_store.default_frames(0), [])

    def test_store_selects_by_app_or_functionality(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            for app, functionality in (("hl2", "trainstation"), ("dsr", "title"), ("dsr", "firelink")):
                directory = root / app / functionality
                directory.mkdir(parents=True)
                (directory / "manifest.json").write_text(json.dumps(manifest_data(app=app, functionality=functionality)))
                (directory / "capture.trace").write_bytes(b"abcd")
            store = trace_store.Store(root)
            self.assertEqual(len(store.select("")[0]), 3)
            chosen, problems = store.select("dsr,hl2/trainstation,nope")
            self.assertEqual(sorted(m.id for m in chosen), ["dsr/firelink", "dsr/title", "hl2/trainstation"])
            self.assertEqual(problems, [f"no trace nope in {root}"])
            chosen, problems = store.select("dsr/f*,*/trainstation,hl2/x*")
            self.assertEqual(sorted(m.id for m in chosen), ["dsr/firelink", "hl2/trainstation"])
            self.assertEqual(problems, [f"no trace hl2/x* in {root}"])
            manifest = chosen[0]
            self.assertEqual(manifest.missing_files(), [])
            (manifest.directory / "capture.trace").write_bytes(b"abc")
            self.assertEqual(manifest.missing_files(), ["capture.trace"])

    def test_fetch_tool_verifies_and_keeps_what_the_pin_says(self):
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            downloads = root / "downloads"
            downloads.mkdir()
            archive = downloads / "tool-1.0.zip"
            with zipfile.ZipFile(archive, "w") as written:
                written.writestr("tool-1.0/bin/tool.exe", b"MZ")
                written.writestr("tool-1.0/bin/tool.pdb", b"debug")
            pin = {"version": "1.0", "downloads": [{"url": "https://example.invalid/tool-1.0.zip",
                                                    "sha256": trace_store.sha256_of(archive), "unpack": "zip",
                                                    "strip": "tool-1.0", "into": "x64", "keep": ["bin/*.exe"]}],
                   "write": {"x64/note.txt": ["a", "b"]}}
            store = trace_store.Store(root / "store")
            target, fetched = trace_store.fetch_tool("tool", pin, store, cache=root / "cache", sources=[downloads])
            self.assertTrue(fetched)
            self.assertEqual(sorted(p.relative_to(target).as_posix() for p in target.rglob("*") if p.is_file()),
                             ["tool.json", "x64/bin/tool.exe", "x64/note.txt"])
            self.assertEqual((target / "x64" / "note.txt").read_bytes(), b"a\r\nb\r\n")
            self.assertFalse(trace_store.fetch_tool("tool", pin, store, sources=[downloads])[1])
            self.assertEqual(store.tool("tool")[0], target)

    def test_archive_members_must_stay_inside(self):
        with self.assertRaises(trace_store.StoreError):
            trace_store.safe_members(["ok/file", "../../etc/passwd"])
        with self.assertRaises(trace_store.StoreError):
            trace_store.safe_members(["C:\\Windows\\evil.dll"])

    def test_pins_are_complete(self):
        for name, pin in trace_store.load_pins().items():
            self.assertTrue(pin["version"] and pin["license"], name)
            for entry in pin["downloads"]:
                self.assertRegex(entry["sha256"], r"^[0-9a-f]{64}$", entry["url"])
                self.assertTrue(entry["url"].startswith("https://"), entry["url"])


class Suite(unittest.TestCase):
    def test_findings_are_judged_and_written_down(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            manifest = trace_store.Manifest(manifest_data(), out)
            reference = solid(6, 6, (9, 9, 9))
            findings = {"notes": [], "passes": {
                "benchmark": {"exit_code": 0, "seconds": 9.0, "samples": [],
                              "output": "Rendered 300 frames in 3 secs, average of 100 fps\n"},
                "check": {"exit_code": 0, "seconds": 12.0, "samples": [], "output": "5: warning: failed with 0x80004005\n"}},
                "frames": {10: reference, 99: solid(6, 6, (200, 9, 9))},
                "references": {10: reference, 99: reference}}
            failures, notes, metrics = trace_suite.judge_findings(manifest, findings, ["LiveKernelEvent 141"], out)
            self.assertEqual([f["case"] for f in failures], ["gpu reset", "frame 99"])
            self.assertEqual(metrics["fps"], 100.0)
            self.assertEqual(metrics["retrace_warnings"], 1)
            self.assertTrue((out / "frame-10.png").exists() and (out / "diff-99.png").exists())
            summary = json.loads((out / "trace.json").read_text())
            self.assertEqual(summary["frames"], {"10": "frame-10.png", "99": "frame-99.png"})

    def test_a_short_trace_is_stretched_and_not_timed(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            (out / "frames.txt").write_text("5\n9\n")
            manifest = trace_store.Manifest(manifest_data(check={"frames": [0, 1]}), out)
            runs = []

            class Replayer:
                def __init__(self):
                    self.manifest, self.out, self.store_dir = manifest, out, "C:\\s"

                def tool_dir(self, name):
                    return "C:\\t"

                def run(self, name, exe, arguments, **options):
                    runs.append(arguments)
                    return {"exit_code": 0, "seconds": 2.0, "samples": [], "output": ""}

            findings = trace_suite.replay_apitrace(Replayer())
        self.assertEqual(runs[0][0], "-w")
        self.assertIn("driver", findings["passes"])
        self.assertIsNone(findings["frame_count"])

    def test_frames_across_gpus(self):
        with tempfile.TemporaryDirectory() as run:
            run = pathlib.Path(run)
            base = solid(8, 8, (50, 50, 50))
            for host, slug, colour in (("local", "0-amd", (50, 50, 50)), ("accel", "1-nvidia", (50, 50, 50)),
                                       ("test", "0-arc", (255, 0, 0))):
                pass_dir = run / host / slug
                trace_dir = pass_dir / "traces" / "hl2" / "trainstation"
                trace_dir.mkdir(parents=True)
                (pass_dir / "results.json").write_text(json.dumps({"gpu": {"name": slug}}))
                judge.write_png(trace_dir / "frame-10.png", solid(8, 8, colour))
                (trace_dir / "trace.json").write_text(json.dumps({"frames": {"10": "frame-10.png"}, "hashes": {},
                                                                  "judge": {}}))
            lines = judge.cross_gpu_section(run)
            text = "\n".join(lines)
            self.assertIn("Trace frames across GPUs", text)
            self.assertIn("**frame 10 (vs accel / 1-nvidia)**", text)
            self.assertIn("identical", text)
            self.assertIn("differs", text)
            self.assertEqual(base[:2], (8, 8))

    def test_no_traces_no_section(self):
        with tempfile.TemporaryDirectory() as run:
            self.assertEqual(judge.cross_gpu_section(run), [])

    def test_capture_command_quotes_for_powershell(self):
        args = type("Args", (), dict(tool="apitrace", trace="hl2/trainstation", api="d3d9",
                                     exe="C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life 2\\hl2.exe",
                                     arguments="-game hl2 +map d1_trainstation_01", package="", app_id="",
                                     process="", mode="", cl_kernel_name="", gfxr_frames="", steam_app=220,
                                     seconds=90, cl_kernels=0, repack=True))()
        command = traces.capture_command(args)
        self.assertIn('-Out:"C:\\av\\traces\\capture\\hl2\\trainstation"', command)
        self.assertIn('-Exe:"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life 2\\hl2.exe"', command)
        self.assertIn("-SteamApp 220 -Seconds 90", command)
        self.assertTrue(command.endswith("-Repack"))
        self.assertEqual(traces.ps_quote("it's"), '"it\'s"')

    def test_capture_command_with_the_gl_of_an_opencl_capture(self):
        args = type("Args", (), dict(tool="clintercept", trace="resolve/edit-cl", api="d3d9",
                                     exe="C:\\Program Files\\Blackmagic Design\\DaVinci Resolve\\Resolve.exe",
                                     arguments="", package="", app_id="", process="", mode="system",
                                     cl_kernel_name="", gfxr_frames="", steam_app=0, seconds=900, cl_kernels=0,
                                     repack=True, gl_trace="resolve/edit-gl",
                                     clear=["%APPDATA%\\a\\OpenCL.cache", "C:\\b"]))()
        command = traces.capture_command(args)
        self.assertIn('-GlOut:"C:\\av\\traces\\capture\\resolve\\edit-gl"', command)
        self.assertIn('-ClearFirst:"%APPDATA%\\a\\OpenCL.cache;C:\\b"', command)
        self.assertNotIn("-Api", command)

    def test_capture_command_with_a_gfxr_hotkey(self):
        args = type("Args", (), dict(tool="gfxr", trace="re4demo/menu", api="d3d12", exe="C:\\g\\re4demo.exe",
                                     arguments="", package="", app_id="", process="", mode="", cl_kernel_name="",
                                     gfxr_frames="", gfxr_trigger="F12", gfxr_trigger_frames=90, steam_app=2231770,
                                     seconds=300, cl_kernels=0, repack=False))()
        command = traces.capture_command(args)
        self.assertIn('-Api d3d12', command)
        self.assertIn('-GfxrTrigger:"F12"', command)
        self.assertIn("-GfxrTriggerFrames 90", command)



class ClSources(unittest.TestCase):
    def test_a_binary_kernel_gets_its_programs_source(self):
        import cl_sources
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            programs = root / "Programs"
            programs.mkdir()
            (programs / "CLI_AAAA_source.cl").write_text("__kernel void zk(__global float *a) { a[0] = 1; }\n")
            (programs / "CLI_AAAA_0000_11111111_options.txt").write_text(" -w -DONE -cl-kernel-arg-info")
            (programs / "CLI_AAAA_0000_22222222_options.txt").write_text(" -w -DONE -DTWO -cl-kernel-arg-info")
            (programs / "CLI_BBBB_source.cl").write_text("kernel void other(void) {}\n")
            (programs / "CLI_BBBB_0000_22222222_options.txt").write_text(" -w -DONE -DTWO -cl-kernel-arg-info")
            for name, kernel, options in (("Enqueue_1_zk", "zk", "-w -DONE -DTWO -cl-kernel-arg-info -cl-kernel-arg-info"),
                                          ("Enqueue_2_zk", "zk", "-w -DTHREE"),
                                          ("Enqueue_3_zk", "zk", "-w -DONE")):
                enqueue = root / "Replay" / name
                enqueue.mkdir(parents=True)
                (enqueue / "kernelName.txt").write_text(kernel)
                (enqueue / "buildOptions.txt").write_text(options)
                (enqueue / "DeviceBinary0.bin").write_bytes(b"sealed")
            (root / "Replay" / "Enqueue_3_zk" / "kernel.cl").write_text("kept")
            sourced, left = cl_sources.source_binaries(root / "Replay", programs)
            self.assertEqual(sourced, [("Enqueue_1_zk", "CLI_AAAA_source.cl")])
            self.assertEqual([name for name, _ in left], ["Enqueue_2_zk"])
            self.assertIn("zk(", (root / "Replay" / "Enqueue_1_zk" / "kernel.cl").read_text())
            self.assertFalse((root / "Replay" / "Enqueue_1_zk" / "DeviceBinary0.bin").exists())
            self.assertTrue((root / "Replay" / "Enqueue_2_zk" / "DeviceBinary0.bin").exists())
            self.assertTrue((root / "Replay" / "Enqueue_3_zk" / "DeviceBinary0.bin").exists())
            self.assertIn("1 kernel(s) left", cl_sources.note(sourced, left))


class ClReplayScript(unittest.TestCase):
    def test_images_are_read_back_over_their_own_extent(self):
        sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "guest"))
        import clreplay
        line = "    cl.enqueue_copy(queue, output_images[pos], gpu_images[pos], region=shape, origin=(0,0,0))\n"
        fixed = clreplay.runnable("for pos in gpu_images.keys():\n" + line)
        self.assertIn("region=get_image_metadata(pos)[1]", fixed)
        self.assertNotIn("region=shape", fixed)
        self.assertEqual(clreplay.runnable("print(1)\n"), "print(1)\n")


class FakeShare:
    """The guest's C: drive as a dict, as GuestShare reaches it."""

    def __init__(self):
        self.files = {}

    @staticmethod
    def key(path):
        return path.lower().rstrip("\\")

    def put(self, local, guest_path, timeout=0):
        self.files[self.key(guest_path)] = pathlib.Path(local).read_bytes()

    def put_bytes(self, data, guest_path):
        self.files[self.key(guest_path)] = bytes(data)

    def get_bytes(self, guest_path):
        from lsw_link import LinkError
        if self.key(guest_path) not in self.files:
            raise LinkError(f"no {guest_path}")
        return self.files[self.key(guest_path)]

    def get(self, guest_path, local, timeout=0):
        pathlib.Path(local).parent.mkdir(parents=True, exist_ok=True)
        pathlib.Path(local).write_bytes(self.get_bytes(guest_path))

    def exists(self, guest_path):
        return self.key(guest_path) in self.files

    def make_directories(self, guest_directory):
        pass


class FakeDesktop:
    """The console session: the few commands the suite runs, and a retracer
    that renders every frame grey, except frame 3, which it renders red."""

    def __init__(self, guest, out, guest_name):
        self.guest = guest
        self.commands = []

    def prepare(self):
        pass

    def clock(self):
        return "2026-10-15T00:00:00Z"

    def resets_since(self, since):
        return []

    def powershell(self, script, timeout=300):
        return 0, "50.5\r\n"

    def run(self, command, timeout=120):
        import io
        import re
        import tarfile
        share = self.guest.share
        self.commands.append(command)
        if " rmdir " in command:
            directory = re.search(r'rmdir /s /q "([^"]+)"', command).group(1).lower()
            for key in [key for key in share.files if key.startswith(directory + "\\")]:
                del share.files[key]
            return 0, ""
        if "tar.exe -xf" in command:
            upload, directory = re.search(r'-xf "([^"]+)" -C "([^"]+)"', command).groups()
            with tarfile.open(fileobj=io.BytesIO(share.get_bytes(upload))) as tar:
                for member in tar.getmembers():
                    share.put_bytes(tar.extractfile(member).read(), directory + "\\" + member.name.replace("/", "\\"))
            del share.files[share.key(upload)]
            return 0, ""
        if "trace-replay.ps1" in command:
            config = json.loads(share.get_bytes(re.search(r'-Config "([^"]+)"', command).group(1)))
            arguments = config["args"].split()
            stdout, stderr = b"", b""
            if "-b" in arguments:
                stdout = b"Rendered 120 frames in 2 secs, average of 60 fps\n"
            if "-S" in arguments:
                calls = [int(call) for call in arguments[arguments.index("-S") + 1].split(",")]
                for call in calls:
                    colour = (255, 0, 0) if call == 40 else (90, 90, 90)
                    stdout += pnm(solid(4, 4, colour), call)
            share.put_bytes(stdout, config["stdout"])
            share.put_bytes(stderr, config["stderr"])
            samples = [[t * 0.5, 300 << 20, 0] for t in range(40)]
            share.put_bytes(json.dumps({"exit_code": 0, "seconds": 2.0, "timed_out": False, "samples": samples,
                                        "modules": ["avumd.dll", "d3d9.dll"]})
                            .encode(), config["result"])
            return 0, ""
        return 0, ""


class SuiteAgainstAFakeGuest(unittest.TestCase):
    def test_a_trace_is_sent_once_replayed_and_judged(self):
        for tar_above in (16, 0):  # file by file, then as one tar
            with self.subTest(tar_above=tar_above):
                self.replay_against_a_fake_guest(tar_above)

    def replay_against_a_fake_guest(self, tar_above):
        from unittest import mock
        with tempfile.TemporaryDirectory() as root, mock.patch.object(trace_suite, "TAR_ABOVE", tar_above):
            root = pathlib.Path(root)
            store = trace_store.Store(root / "store")
            tool = store.tools_root / "apitrace-14.0"
            (tool / "x64" / "bin").mkdir(parents=True)
            (tool / "x64" / "bin" / "d3dretrace.exe").write_bytes(b"MZ")
            (tool / "tool.json").write_text(json.dumps({"version": "14.0"}))
            trace_dir = store.root / "hl2" / "trainstation"
            trace_dir.mkdir(parents=True)
            (trace_dir / "capture.trace").write_bytes(b"trace")
            (trace_dir / "frames.txt").write_text("".join(f"{10 * (n + 1)}\n" for n in range(300)))
            data = manifest_data(files=trace_store.describe_files(trace_dir, ["capture.trace", "frames.txt"]),
                                 check={"frames": [1, 3, -1]})
            (trace_dir / "manifest.json").write_text(json.dumps(data))
            (trace_dir / "reference").mkdir()
            for frame in (1, 3, 299):
                judge.write_png(trace_dir / "reference" / f"frame-{frame}.png", solid(4, 4, (90, 90, 90)))
            guest = type("Guest", (), {"share": FakeShare()})()
            desktops = []

            def desktop(*arguments):
                desktops.append(FakeDesktop(*arguments))
                return desktops[-1]

            with mock.patch.object(trace_suite, "Desktop", desktop), mock.patch.object(trace_suite, "say"):
                result = trace_suite.run_trace_suite(guest, root / "out", None, "", "", store=store)
                again = trace_suite.run_trace_suite(guest, root / "out2", None, "", "hl2", store=store)
            self.assertEqual(result["status"], "fail")
            self.assertEqual([f["case"] for f in result["failures"]], ["hl2/trainstation: frame 3"])
            self.assertEqual(result["metrics"]["hl2/trainstation fps"], 150.0)  # 300 frames over the check pass's 2 s
            self.assertEqual(result["metrics"]["hl2/trainstation memory_growth_mib"], 0.0)
            self.assertEqual(again["failures"], result["failures"])
            stamps = [key for key in guest.share.files if key.endswith(".stamp")]
            self.assertEqual(len(stamps), 2)  # the tool and the trace
            self.assertFalse(any("trace-replay" not in command and "rmdir" in command and "store" in command
                                 for command in desktops[1].commands))  # sent once, kept
            check = next(command for command in desktops[0].commands if "check.config" in command)
            self.assertIn("check.config.json", check)
            sent = json.loads(guest.share.get_bytes("C:\\av\\traces\\runs\\hl2\\trainstation\\check.config.json"))
            self.assertIn("-S 20,40,3000", sent["args"])
            self.assertTrue((root / "out" / "hl2" / "trainstation" / "diff-3.png").exists())
            self.assertTrue((root / "out" / "verdict.json").exists())


class TraceWriter:
    """A version 6 apitrace stream, written the way apitrace's own writer does."""

    def __init__(self):
        self.out = bytearray(b"\x06\x06") + self.string("process.name") + self.string("App.exe") + self.string("")
        self.sigs, self.calls = {}, 0
        self.enum_defined = False

    @staticmethod
    def uint(value):
        return trace_mend.encode_uint(value)

    def string(self, text):
        data = text.encode()
        return self.uint(len(data)) + data

    def bool(self, value):
        """A BOOL as apitrace records it: an enum whose signature its first use defines."""
        if self.enum_defined:
            return b"\x09\x00\x04" + self.uint(int(value))
        self.enum_defined = True
        return (b"\x09\x00" + self.uint(2) + self.string("FALSE") + b"\x04\x00" + self.string("TRUE") + b"\x04\x01"
                + b"\x04" + self.uint(int(value)))

    def enter(self, name, arguments, values):
        if name not in self.sigs:
            self.sigs[name] = len(self.sigs)
            signature = self.uint(self.sigs[name]) + self.string(name) + self.uint(len(arguments)) + \
                b"".join(self.string(argument) for argument in arguments)
        else:
            signature = self.uint(self.sigs[name])
        self.out += b"\x00\x00" + signature
        for index, value in values.items():
            self.out += b"\x01" + self.uint(index) + value
        self.out += b"\x00"
        self.calls += 1
        return self.calls - 1

    def leave(self, number, values, ret=None):
        self.out += b"\x01" + self.uint(number)
        for index, value in values.items():
            self.out += b"\x01" + self.uint(index) + value
        if ret is not None:
            self.out += b"\x02" + ret
        self.out += b"\x00"

    def reclaim(self, resources, discarded):
        arguments = ("this", "NumResources", "ppResources", "pDiscarded")
        number = self.enter("IDXGIDevice4::ReclaimResources", arguments, {
            0: b"\x0d" + self.uint(0x1000), 1: b"\x04" + self.uint(resources),
            2: b"\x0b" + self.uint(resources) + b"".join(b"\x0d" + self.uint(0x2000 + i) for i in range(resources))})
        self.leave(number, {3: b"\x0b\x01" + self.bool(discarded)}, ret=b"\x04\x00")
        return number


class Mending(unittest.TestCase):
    """trace_mend: ReclaimResources' pDiscarded widened to NumResources BOOLs."""

    def trace(self):
        writer = TraceWriter()
        writer.reclaim(1, False)
        writer.enter("ID3D11DeviceContext::Draw", ("this", "VertexCount", "StartVertexLocation"),
                     {0: b"\x0d\x10", 1: b"\x04\x03", 2: b"\x04\x00"})
        writer.leave(1, {})
        six = writer.reclaim(6, True)
        three = writer.reclaim(3, False)
        return writer, six, three

    def test_each_wide_reclaim_gets_room_for_every_resource(self):
        writer, six, three = self.trace()
        mended, mender = trace_mend.mend(memoryview(bytes(writer.out)))
        self.assertEqual(mender.mended, [(six, "IDXGIDevice4::ReclaimResources", 6),
                                   (three, "IDXGIDevice4::ReclaimResources", 3)])
        # The mended stream has nothing left to mend, and its calls are the same calls.
        mender = trace_mend.Mender(memoryview(mended))
        mender.run()
        self.assertEqual((mender.calls, mender.mended), (writer.calls, []))
        # Every copy after the first names the BOOL's enum by its id alone.
        self.assertIn(b"\x0b\x06" + b"\x09\x00\x04\x01" * 6, mended)
        self.assertIn(b"\x0b\x03" + b"\x09\x00\x04\x00" * 3, mended)

    def test_the_first_bool_s_enum_definition_is_not_repeated(self):
        writer = TraceWriter()
        number = writer.reclaim(4, False)
        mended, mender = trace_mend.mend(memoryview(bytes(writer.out)))
        self.assertEqual(mender.mended, [(number, "IDXGIDevice4::ReclaimResources", 4)])
        self.assertEqual(mended.count(b"FALSE"), 1)
        mender = trace_mend.Mender(memoryview(mended))
        mender.run()
        self.assertEqual(mender.mended, [])

    def test_a_trace_without_wide_reclaims_is_left_alone(self):
        writer = TraceWriter()
        writer.reclaim(1, False)
        data = bytes(writer.out)
        mended, mender = trace_mend.mend(memoryview(data))
        self.assertEqual((mender.mended, mender.dropped), ([], []))
        self.assertEqual(bytes(mended), data)

    def test_a_trace_cut_short_ends_before_its_unfinished_calls(self):
        writer, six, three = self.trace()
        whole = len(writer.out)
        # The exit cut off a Release after its enter, then an enter in its signature.
        release = writer.enter("ID3D11Texture2D1::Release", ("this",), {0: b"\x0d\x20"})
        data = bytes(writer.out) + b"\x00\x00\x05"
        mended, mender = trace_mend.mend(memoryview(data))
        self.assertEqual(len(mender.mended), 2)
        self.assertEqual(mender.dropped, [(release, "ID3D11Texture2D1::Release")])
        self.assertEqual(len(mended), whole + (6 - 1) * 4 + (3 - 1) * 4)  # each added BOOL: 09 00 04 xx
        again = trace_mend.Mender(memoryview(mended))
        again.run()
        self.assertEqual((again.calls, again.mended, again.dropped), (writer.calls - 1, [], []))

    def test_a_call_left_open_before_others_finished_is_kept(self):
        writer = TraceWriter()
        first = writer.enter("IDXGISwapChain::Present", ("this",), {0: b"\x0d\x01"})
        second = writer.enter("ID3D11DeviceContext::Flush", ("this",), {0: b"\x0d\x02"})
        writer.leave(second, {})
        mended, mender = trace_mend.mend(memoryview(bytes(writer.out)))
        self.assertEqual(mender.dropped, [])
        self.assertEqual(bytes(mended), bytes(writer.out))
        self.assertEqual(first, 0)

    def test_files_round_trip_through_both_containers(self):
        writer, six, three = self.trace()
        stream = bytes(writer.out)
        with tempfile.TemporaryDirectory() as directory:
            # apitrace's snappy container: 'at', then length-prefixed raw snappy
            # blocks - here one of literals and one with a back-reference.
            first, second = stream[:40], stream[40:]
            literal = trace_mend.encode_uint(len(first)) + bytes([(len(first) - 1) << 2]) + first
            copy = trace_mend.encode_uint(len(second) + 8) + bytes([(len(second) - 1) << 2 if len(second) <= 60
                                                                      else 61 << 2])
            if len(second) > 60:
                copy += (len(second) - 1).to_bytes(2, "little")
            copy += second + bytes([((8 - 4) << 2) | 1, 8])  # copy 8 bytes from 8 back
            path = pathlib.Path(directory) / "capture.trace"
            path.write_bytes(b"at" + struct.pack("<I", len(literal)) + literal + struct.pack("<I", len(copy)) + copy)
            self.assertEqual(trace_mend.read_container(path), stream + stream[-8:])
            path.write_bytes(b"at" + struct.pack("<I", len(literal)) + literal)
            self.assertEqual(trace_mend.read_container(path), first)
            # A whole trace mended in place, written as zstd and read back.
            trace_mend.write_container(path, stream)
            self.assertEqual(trace_mend.mend_file(path).mended, [(six, "IDXGIDevice4::ReclaimResources", 6),
                                                                 (three, "IDXGIDevice4::ReclaimResources", 3)])
            before = path.read_bytes()
            self.assertEqual(trace_mend.mend_file(path).mended, [])
            self.assertEqual(path.read_bytes(), before)
            path.write_bytes(b"PK\x03\x04")
            with self.assertRaises(trace_mend.TraceError):
                trace_mend.read_container(path)


def io_zip(members):
    """A zip archive's bytes holding `members` ({name: bytes})."""
    import io
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as written:
        for name, data in members.items():
            written.writestr(name, data)
    return buffer.getvalue()


class Direct3D12(unittest.TestCase):
    """D3D12 traces: recorded and replayed by GFXReconstruct's D3D12 build."""

    def test_d3d12_replays_with_the_d3d12_build_and_vulkan_with_the_sdk_s(self):
        d3d12 = trace_store.Manifest(manifest_data(api="d3d12", capture="capture.gfxr"), "/x")
        vulkan = trace_store.Manifest(manifest_data(api="vulkan", capture="capture.gfxr"), "/x")
        self.assertEqual((d3d12.tool, d3d12.replayer), ("gfxreconstruct", "gfxreconstruct_d3d12"))
        self.assertEqual((vulkan.tool, vulkan.replayer), ("gfxreconstruct", "gfxreconstruct"))
        self.assertEqual(trace_store.Manifest(manifest_data(), "/x").replayer, "apitrace")
        self.assertIn("gfxreconstruct_d3d12", trace_store.load_pins())

    def test_a_tool_s_name_does_not_find_another_tool_it_prefixes(self):
        with tempfile.TemporaryDirectory() as root:
            store = trace_store.Store(root)
            for name in ("gfxreconstruct-1.4.363.0", "gfxreconstruct_d3d12-dev-280a0ac"):
                (store.tools_root / name).mkdir(parents=True)
                (store.tools_root / name / "tool.json").write_text(json.dumps({"version": name}))
            self.assertEqual(store.tool("gfxreconstruct")[0].name, "gfxreconstruct-1.4.363.0")
            self.assertEqual(store.tool("gfxreconstruct_d3d12")[0].name, "gfxreconstruct_d3d12-dev-280a0ac")

    def test_capture_command_and_tool_for_d3d12(self):
        args = type("Args", (), dict(tool="gfxr", trace="re4demo/village", api="d3d12",
                                     exe="C:\\Games\\re4demo.exe", arguments="", package="", app_id="", process="",
                                     mode="", cl_kernel_name="", gfxr_frames="", steam_app=2231770, seconds=600,
                                     cl_kernels=0, repack=False))()
        command = traces.capture_command(args)
        self.assertIn("-Tool gfxr", command)
        self.assertIn("-Api d3d12", command)
        self.assertIn("-SteamApp 2231770", command)
        self.assertEqual(traces.capture_tool(args), "gfxreconstruct_d3d12")
        args.api = None
        self.assertIn("-Api vulkan", traces.capture_command(args))
        self.assertEqual(traces.capture_tool(args), "gfxreconstruct")
        args.api = "dxgi"
        with self.assertRaises(SystemExit):
            traces.capture_command(args)

    def test_an_actions_artifact_comes_through_gh_or_from_a_named_file(self):
        from unittest import mock
        with tempfile.TemporaryDirectory() as root:
            root = pathlib.Path(root)
            payload = io_zip({"gfxrecon-replay.exe": b"MZ", "D3D12/D3D12Core.dll": b"MZ", "extra.pdb": b"x"})
            entry = {"url": "https://api.github.com/repos/o/r/actions/artifacts/7/zip", "file": "build-abc.zip",
                     "gh_artifact": True, "sha256": hashlib.sha256(payload).hexdigest(), "unpack": "zip",
                     "into": "x64", "keep": ["*.exe", "D3D12/*.dll"]}
            calls = []

            def gh(command, stdout, **options):
                calls.append(command)
                stdout.write(payload)
                return type("Done", (), {"returncode": 0, "stderr": b""})()

            with mock.patch.object(trace_store.subprocess, "run", gh):
                got = trace_store.download(entry, root / "cache")
            self.assertEqual(calls, [["gh", "api", "repos/o/r/actions/artifacts/7/zip"]])
            self.assertEqual(got.name, "build-abc.zip")
            # Already downloaded by hand: found by its pinned file name.
            sources = root / "downloads"
            sources.mkdir()
            (sources / "build-abc.zip").write_bytes(payload)
            pin = {"version": "dev-abc", "downloads": [entry]}
            with mock.patch.object(trace_store, "download", side_effect=AssertionError("no download")):
                target, fetched = trace_store.fetch_tool("t", pin, trace_store.Store(root / "store"), sources=[sources])
            self.assertTrue(fetched)
            self.assertEqual(sorted(p.relative_to(target).as_posix() for p in target.rglob("*") if p.is_file()),
                             ["tool.json", "x64/D3D12/D3D12Core.dll", "x64/gfxrecon-replay.exe"])

            def failing(command, stdout, **options):
                return type("Done", (), {"returncode": 1, "stderr": b"HTTP 410: artifact expired"})()

            with mock.patch.object(trace_store.subprocess, "run", failing), \
                    self.assertRaisesRegex(trace_store.StoreError, "410.*--from"):
                trace_store.download(entry, root / "cache2")

    def test_pull_judges_gfxr_frames_from_gfxrecon_info(self):
        info = "File info:\n\tCompression format: Zstandard\nFrame info:\n\tTotal frames: 901\n"
        self.assertEqual(traces.gfxr_frame_count(info), 901)
        self.assertEqual(traces.gfxr_frame_count("no frames here"), 0)
        self.assertEqual(traces.first_frames("gfxreconstruct", 901), [91, 293, 495, 698, 900])
        self.assertEqual(traces.first_frames("gfxreconstruct", 1), [])
        self.assertEqual(traces.first_frames("apitrace", 1000), [100, 325, 550, 774, 999])
        self.assertEqual(traces.first_frames("clintercept", 7), [])

    def test_replay_runs_the_d3d12_build_s_replayer_and_reads_its_screenshots(self):
        with tempfile.TemporaryDirectory() as directory:
            out = pathlib.Path(directory)
            manifest = trace_store.Manifest(manifest_data(api="d3d12", capture="capture.gfxr",
                                                          check={"frames": [5, 0, 9]}), out)
            runs = []
            bmp = out / "shot.bmp"

            class Files:
                def __init__(self):
                    self.share = self

                def make_directories(self, directory):
                    pass

                def listing(self, directory):
                    return ["shot_frame_5.bmp", "shot_frame_9.bmp", "unrelated.txt"]

                def get(self, guest_path, local):
                    pathlib.Path(local).write_bytes(bmp.read_bytes())

            class Replayer:
                def __init__(self):
                    self.manifest, self.out, self.store_dir, self.run_dir = manifest, out, "C:\\s", "C:\\r"
                    self.files = Files()

                def tool_dir(self, name):
                    return f"C:\\t\\{name}"

                def run(self, name, exe, arguments, **options):
                    runs.append((exe, arguments))
                    return {"exit_code": 0, "seconds": 2.0, "samples": [], "output": "Measured FPS: 42.5 fps"}

            width, height = 2, 2
            row = bytes([0, 0, 255] * width) + b"\0" * ((4 - (3 * width) % 4) % 4)
            pixels = row * height
            bmp.write_bytes(b"BM" + struct.pack("<IHHI", 54 + len(pixels), 0, 0, 54)
                            + struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 0, 0, 0, 0)
                            + pixels)
            findings = trace_suite.replay_gfxreconstruct(Replayer())
        exe, arguments = runs[0]
        self.assertEqual(exe, "C:\\t\\gfxreconstruct_d3d12\\x64\\gfxrecon-replay.exe")
        self.assertEqual(arguments[arguments.index("--screenshots") + 1], "5,9")
        self.assertEqual(arguments[-1], "C:\\s\\capture.gfxr")
        self.assertEqual(sorted(findings["frames"]), [5, 9])
        self.assertEqual(findings["frames"][5][:2], (2, 2))
        self.assertTrue(findings["notes"])  # frame 0 is not a GFXReconstruct frame


if __name__ == "__main__":
    unittest.main()
