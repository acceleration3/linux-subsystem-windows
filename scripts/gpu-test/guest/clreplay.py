"""clreplay.py: CLIntercept capture-replay kernels replayed and compared.

  python clreplay.py --capture DIR --out RESULT.json [--programs DIR] [--platform avgpu]
                     [--rtol 1e-3] [--atol 1e-5] [--itol 0] [--timeout 300]

DIR holds the Enqueue_<n>_<kernel> directories CLIntercept's CaptureReplay
wrote (its Replay directory). Each is replayed by the run.py CLIntercept
wrote into it, unchanged, on the OpenCL platform whose name contains
--platform (PYOPENCL_CTX); then every buffer and image it wrote to Test/ is
compared with the one the application's own run left in Post/, element by
element as ArgumentDataTypes.txt types it: floats within atol + rtol * |ref|,
integers within itol. Test/ is removed again, so the capture stays as it was.

--programs names CLIntercept's DumpProgramSource output (CLI_<hash>_source.cl
with its CLI_<hash>_*_options.txt): each program is built again, in a process
of its own, with each set of options the application built it with - a
program the driver cannot build never reached an enqueue to be captured.

Runs in the guest under the pinned embeddable Python with numpy and
pyopencl; the comparison also runs without numpy, which is how it is tested
on the hosts (test_traces.py).
"""
import argparse
import hashlib
import json
import math
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time

try:
    import numpy
except ImportError:  # the hosts' tests
    numpy = None

FLOATS = {"float": ("f4", "f"), "double": ("f8", "d"), "half": ("f2", "e")}
IMAGE_FLOAT_TYPES = {0x10DE: "float", 0x10DD: "half"}
"""CL_FLOAT and CL_HALF_FLOAT: an image of either is compared as floats."""


def argument_kinds(directory):
    """{arg index: "float" | "double" | "half" | "bytes"} from
    ArgumentDataTypes.txt (one type name per argument, in order), and from
    each image's metadata."""
    kinds = {}
    path = directory / "ArgumentDataTypes.txt"
    if path.exists():
        for index, line in enumerate(path.read_text(errors="replace").splitlines()):
            base = re.sub(r"\b(const|global|__global|restrict|volatile|unsigned)\b|\*", " ", line).split()
            name = re.sub(r"\d+$", "", base[-1]) if base else ""
            kinds[index] = name if name in FLOATS else "bytes"
    for meta in directory.glob("Image_MetaData_*.txt"):
        index = int(re.findall(r"\d+", meta.name)[0])
        lines = meta.read_text().splitlines()
        try:
            kinds[index] = IMAGE_FLOAT_TYPES.get(int(lines[6]), "bytes")
        except (IndexError, ValueError):
            kinds[index] = "bytes"
    return kinds


def compare(reference, got, kind, rtol, atol, itol):
    """How `got` differs from `reference`, both raw bytes of one buffer."""
    result = {"kind": kind, "bytes": len(reference), "sha256": hashlib.sha256(got).hexdigest()}
    if len(reference) != len(got):
        result.update(identical=False, size_mismatch=f"{len(got)} bytes, not {len(reference)}", elements=0, bad=0)
        return result
    if reference == got:
        size = struct.calcsize(FLOATS[kind][1]) if kind in FLOATS else 1
        result.update(identical=True, elements=len(reference) // size, bad=0, max_abs=0.0, max_rel=0.0)
        return result
    result["identical"] = False
    if kind in FLOATS:
        size = struct.calcsize(FLOATS[kind][1])
        usable = len(reference) - len(reference) % size
        if numpy is not None:
            a = numpy.frombuffer(reference[:usable], dtype="<" + FLOATS[kind][0]).astype(numpy.float64)
            b = numpy.frombuffer(got[:usable], dtype="<" + FLOATS[kind][0]).astype(numpy.float64)
            both_nan = numpy.isnan(a) & numpy.isnan(b)
            with numpy.errstate(invalid="ignore", over="ignore"):
                difference = numpy.abs(a - b)
                bad = ~both_nan & ~(difference <= atol + rtol * numpy.abs(a))
                finite = ~both_nan & numpy.isfinite(difference)
                max_abs = float(difference[finite].max()) if finite.any() else 0.0
                relative = difference[finite] / numpy.maximum(numpy.abs(a[finite]), 1e-30)
                max_rel = float(relative.max()) if relative.size else 0.0
            result.update(elements=int(a.size), bad=int(bad.sum()), max_abs=max_abs, max_rel=max_rel)
        else:
            count = usable // size
            a = struct.unpack(f"<{count}{FLOATS[kind][1]}", reference[:usable])
            b = struct.unpack(f"<{count}{FLOATS[kind][1]}", got[:usable])
            bad, max_abs, max_rel = 0, 0.0, 0.0
            for x, y in zip(a, b):
                if math.isnan(x) and math.isnan(y):
                    continue
                difference = abs(x - y)
                if not difference <= atol + rtol * abs(x):
                    bad += 1
                if math.isfinite(difference):
                    max_abs = max(max_abs, difference)
                    max_rel = max(max_rel, difference / max(abs(x), 1e-30))
            result.update(elements=count, bad=bad, max_abs=max_abs, max_rel=max_rel)
    else:
        if numpy is not None:
            a = numpy.frombuffer(reference, dtype=numpy.uint8).astype(numpy.int16)
            b = numpy.frombuffer(got, dtype=numpy.uint8).astype(numpy.int16)
            difference = numpy.abs(a - b)
            result.update(elements=int(a.size), bad=int((difference > itol).sum()), max_abs=float(difference.max()),
                          max_rel=None)
        else:
            differences = [abs(x - y) for x, y in zip(reference, got)]
            result.update(elements=len(reference), bad=sum(1 for d in differences if d > itol),
                          max_abs=float(max(differences)), max_rel=None)
    return result


def enqueue_dirs(capture):
    def number(path):
        found = re.match(r"Enqueue_(\d+)_", path.name)
        return int(found.group(1)) if found else -1
    return sorted((path for path in capture.iterdir() if path.is_dir() and path.name.startswith("Enqueue_")),
                  key=number)


READ_BACK = "cl.enqueue_copy(queue, output_images[pos], gpu_images[pos], region=shape, origin=(0,0,0))"
"""CLIntercept 3.0.6's run.py reads every image back over `shape`, the
extent of the last image it made: a kernel with images of two sizes (Resolve's
745x72 and 1280x720) has its smaller one read past its end, which OpenCL
refuses (CL_INVALID_VALUE). Each is read over its own extent instead."""
OWN_EXTENT = ("cl.enqueue_copy(queue, output_images[pos], gpu_images[pos], "
              "region=get_image_metadata(pos)[1], origin=(0,0,0))")


def runnable(text):
    """run.py's text as it is replayed: each image read back over its own extent."""
    return text.replace(READ_BACK, OWN_EXTENT)


def replay(directory, platform, timeout):
    environment = dict(os.environ, PYOPENCL_CTX=platform, PYOPENCL_NO_CACHE="1", PYOPENCL_COMPILER_OUTPUT="1")
    shutil.rmtree(directory / "Test", ignore_errors=True)
    script = directory / "run.py"
    text = script.read_text(errors="replace")
    if runnable(text) != text:
        script = directory / "run-lsw.py"
        script.write_text(runnable(text))
    started = time.monotonic()
    try:
        done = subprocess.run([sys.executable, script.name], cwd=directory, capture_output=True, text=True,
                              env=environment, timeout=timeout)
        code, output = done.returncode, done.stdout + done.stderr
    except subprocess.TimeoutExpired as expired:
        code, output = None, f"ran past {timeout} s\n{expired.stdout or ''}"
    return code, output, time.monotonic() - started


def replayed_output(directory, post):
    """The file run.py wrote for `post`. CLIntercept names what it dumps
    ..._Arg_4_Buffer_0000.bin or ..._Arg_7_Image_0006_1x1x1_32bpp.raw - the
    dump's number, and an image's extent and texel size - while its run.py
    writes ..._Arg_4_Buffer.bin and ..._Arg_7_Image.raw; either name is
    taken."""
    test = directory / "Test" / post.name
    if test.exists():
        return test
    return directory / "Test" / re.sub(r"(_Arg_\d+_(?:Buffer|Image))_[^.]*(\.\w+)$", r"\1\2", post.name)


def judge_enqueue(directory, args):
    kinds = argument_kinds(directory)
    arguments = []
    for post in sorted((directory / "Post").glob("*")):
        found = re.search(r"_Arg_(\d+)_", post.name)
        if not found or post.suffix not in (".bin", ".raw"):
            continue
        index = int(found.group(1))
        test = replayed_output(directory, post)
        if not test.exists():
            arguments.append({"arg": index, "missing": True})
            continue
        entry = compare(post.read_bytes(), test.read_bytes(), kinds.get(index, "bytes"), args.rtol, args.atol,
                        args.itol)
        entry["arg"] = index
        arguments.append(entry)
    return arguments


def program_builds(directory):
    """(source, options text, label) for every dumped program and build."""
    builds = []
    for source in sorted(directory.glob("CLI_*_source.cl")):
        prefix = source.name[:-len("_source.cl")]
        options = [path for path in sorted(directory.glob(prefix + "_*_options.txt"))
                   if not path.name.endswith(("_compile_options.txt", "_link_options.txt"))]
        for path in options or [None]:
            text = path.read_text(errors="replace").strip() if path else ""
            builds.append((source, text, path.name[:-len("_options.txt")] if path else prefix))
    return builds


def build_one(source, options):
    """In a process of its own: one program built on the chosen platform."""
    import pyopencl as cl
    context = cl.create_some_context(interactive=False)
    device = context.devices[0]
    print(f"Running on platform: {device.platform.name}", flush=True)
    program = cl.Program(context, pathlib.Path(source).read_text(errors="replace"))
    try:
        program.build(options=options)
    except cl.Error as error:
        print(f"BUILD FAILED: {type(error).__name__}: {str(error)[:3000]}", flush=True)
        return 1
    print(f"built {len(program.kernel_names.split(';')) if program.kernel_names else 0} kernel(s)", flush=True)
    return 0


RAN_ON = re.compile(r"Running on platform: (.*)")


def ran_on(output):
    """The platform a build or a run.py named, found before its output is cut
    to its end: a compiler's warnings can push the line out of what is kept."""
    found = RAN_ON.search(output)
    return found.group(1).strip() if found else None


def rebuild_programs(directory, platform, timeout):
    environment = dict(os.environ, PYOPENCL_CTX=platform, PYOPENCL_NO_CACHE="1")
    results = []
    for source, options, label in program_builds(directory):
        started = time.monotonic()
        try:
            done = subprocess.run([sys.executable, str(pathlib.Path(__file__).resolve()), "--build-one", str(source),
                                   "--build-options", options], capture_output=True, text=True, env=environment,
                                  timeout=timeout)
            code, output = done.returncode, done.stdout + done.stderr
        except subprocess.TimeoutExpired:
            code, output = None, f"ran past {timeout} s"
        results.append({"name": label, "source": source.name, "options": options, "exit_code": code,
                        "seconds": round(time.monotonic() - started, 3), "ran_on": ran_on(output),
                        "output": output[-3000:]})
        print(f"{label}: exit {code}", flush=True)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-one", help=argparse.SUPPRESS)
    parser.add_argument("--build-options", default="", help=argparse.SUPPRESS)
    parser.add_argument("--programs", type=pathlib.Path)
    parser.add_argument("--capture", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path)
    parser.add_argument("--platform", default="avgpu")
    parser.add_argument("--rtol", type=float, default=1e-3)
    parser.add_argument("--atol", type=float, default=1e-5)
    parser.add_argument("--itol", type=int, default=0)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if args.build_one:
        return build_one(args.build_one, args.build_options)
    if args.out is None:
        parser.error("--out is required")
    result = {"platform": args.platform, "enqueues": [], "programs": []}
    try:
        import pyopencl  # noqa: F401 - what run.py needs, checked once up front
        result["platforms"] = [platform.name for platform in pyopencl.get_platforms()]
    except Exception as error:  # the replay below then fails with the same error
        result["error"] = f"pyopencl: {type(error).__name__}: {error}"
    if args.programs and args.programs.is_dir():
        result["programs"] = rebuild_programs(args.programs, args.platform, args.timeout)
    for directory in enqueue_dirs(args.capture) if args.capture and args.capture.is_dir() else []:
        code, output, seconds = replay(directory, args.platform, args.timeout)
        entry = {"name": directory.name, "exit_code": code, "seconds": round(seconds, 3),
                 "ran_on": ran_on(output), "output": output[-4000:]}
        if code == 0:
            entry["arguments"] = judge_enqueue(directory, args)
        shutil.rmtree(directory / "Test", ignore_errors=True)
        result["enqueues"].append(entry)
        print(f"{directory.name}: exit {code} in {seconds:.1f} s", flush=True)
    args.out.write_text(json.dumps(result, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
