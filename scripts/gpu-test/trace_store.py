import fnmatch
import hashlib
import json
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile

HERE = pathlib.Path(__file__).resolve().parent
PINS = HERE / "trace_tools.json"
STATE = pathlib.Path(os.environ.get("XDG_DATA_HOME", pathlib.Path.home() / ".local" / "share")) / "lsw-gpu-test"
STORE = pathlib.Path(os.environ.get("LSW_GPU_TEST_TRACE_STORE", STATE / "traces"))
DOWNLOADS = pathlib.Path(os.environ.get("XDG_CACHE_HOME", pathlib.Path.home() / ".cache")) / "lsw-gpu-test" / \
    "trace-tools"

SCHEMA = 1
NAME = re.compile(r"^[a-z0-9][a-z0-9_.-]*$")
APITRACE_APIS = ("d3d8", "d3d9", "dxgi", "gl")
GFXR_APIS = ("vulkan", "d3d12")
TOOL_OF_API = {**{api: "apitrace" for api in APITRACE_APIS}, **{api: "gfxreconstruct" for api in GFXR_APIS},
               "opencl": "clintercept"}
REPLAYER_OF_TOOL = {"apitrace": "apitrace", "gfxreconstruct": "gfxreconstruct", "clintercept": "python"}
REPLAYER_OF_API = {"d3d12": "gfxreconstruct_d3d12"}

DEFAULT_JUDGE = {
    "reference": "promoted",
    "pixel_tolerance": 2,
    "psnr_min": 40.0,
    "changed_max_pct": 1.0,
    "psnr_warn": 30.0,
    "memory_growth_mib_max": 256,
    "min_fps": None,
    "rtol": 1e-3,
    "atol": 1e-5,
    "itol": 0,
    "max_bad_fraction": 0.0,
}
DEFAULT_REPLAY = {"timeout_s": 900, "benchmark": False, "env": {}, "extra_args": [], "cl_platform": "avgpu",
                  "warp_gpu": 1}


class StoreError(Exception):
    pass


class Manifest:
    def __init__(self, data, directory):
        self.data = data
        self.directory = pathlib.Path(directory)

    @classmethod
    def load(cls, directory):
        path = pathlib.Path(directory) / "manifest.json"
        try:
            data = json.loads(path.read_text())
        except (OSError, ValueError) as error:
            raise StoreError(f"{path}: {error}") from error
        manifest = cls(data, directory)
        problems = manifest.problems()
        if problems:
            raise StoreError(f"{path}: {'; '.join(problems)}")
        return manifest

    @property
    def id(self):
        return f"{self.data['app']}/{self.data['functionality']}"

    @property
    def api(self):
        return self.data["api"]

    @property
    def tool(self):
        return TOOL_OF_API[self.api]

    @property
    def replayer(self):
        return REPLAYER_OF_API.get(self.api, REPLAYER_OF_TOOL[self.tool])

    @property
    def arch(self):
        return self.data.get("arch", "x64")

    @property
    def capture(self):
        return self.data["capture"]

    @property
    def check_frames(self):
        return list(self.data.get("check", {}).get("frames", []))

    @property
    def judge(self):
        return {**DEFAULT_JUDGE, **self.data.get("judge", {})}

    @property
    def replay(self):
        return {**DEFAULT_REPLAY, **self.data.get("replay", {})}

    def problems(self):
        data = self.data
        found = []
        if data.get("schema") != SCHEMA:
            found.append(f"schema is {data.get('schema')!r}, not {SCHEMA}")
        for key in ("app", "functionality"):
            if not isinstance(data.get(key), str) or not NAME.match(data[key]):
                found.append(f"{key} {data.get(key)!r} is not a lower-case name")
        if data.get("api") not in TOOL_OF_API:
            found.append(f"api {data.get('api')!r} is none of {', '.join(TOOL_OF_API)}")
        if data.get("arch", "x64") not in ("x64", "x86"):
            found.append(f"arch {data.get('arch')!r} is neither x64 nor x86")
        capture = data.get("capture")
        if not isinstance(capture, str) or not capture or pathlib.PurePosixPath(capture).is_absolute() \
                or ".." in pathlib.PurePosixPath(capture).parts:
            found.append(f"capture {capture!r} is not a path inside the functionality's directory")
        files = data.get("files")
        if not isinstance(files, dict) or not files:
            found.append("files lists nothing")
        frames = data.get("check", {}).get("frames", [])
        if not isinstance(frames, list) or not all(isinstance(frame, int) for frame in frames):
            found.append(f"check.frames {frames!r} is not a list of frame numbers")
        unknown = set(data.get("judge", {})) - set(DEFAULT_JUDGE)
        if unknown:
            found.append(f"judge has unknown keys {sorted(unknown)}")
        unknown = set(data.get("replay", {})) - set(DEFAULT_REPLAY)
        if unknown:
            found.append(f"replay has unknown keys {sorted(unknown)}")
        if data.get("judge", {}).get("reference", "promoted") not in ("promoted", "warp"):
            found.append("judge.reference is neither promoted nor warp")
        if data.get("judge", {}).get("reference") == "warp" and data.get("api") not in ("dxgi", "d3d12"):
            found.append("judge.reference warp needs a dxgi (D3D10/11) or d3d12 trace: D3D9, GL and Vulkan "
                         "have no WARP")
        return found

    def missing_files(self):
        missing = []
        for relative, entry in self.data["files"].items():
            path = self.directory / relative
            if not path.is_file() or path.stat().st_size != entry.get("size"):
                missing.append(relative)
        return missing

    def frame_calls(self):
        path = self.directory / "frames.txt"
        if not path.exists():
            return None
        return parse_frames(path.read_text())

    def references(self):
        found = {}
        for path in sorted((self.directory / "reference").glob("frame-*.png")):
            number = re.match(r"frame-(-?\d+)\.png$", path.name)
            if number:
                found[int(number.group(1))] = path
        return found

    def digest(self):
        return hashlib.sha256(json.dumps(self.data["files"], sort_keys=True).encode()).hexdigest()


def parse_frames(text):
    return [int(found.group(1)) for found in re.finditer(r"^\s*(\d+)\b", text, re.M)]


def resolve_frames(wanted, count):
    chosen, outside = [], []
    for frame in wanted:
        index = frame + count if frame < 0 else frame
        if 0 <= index < count:
            if index not in chosen:
                chosen.append(index)
        else:
            outside.append(frame)
    return chosen, outside


def default_frames(count, picks=5):
    if count <= 0:
        return []
    start = count // 10
    if count - start <= picks:
        return list(range(start, count))
    step = (count - 1 - start) / (picks - 1)
    return sorted({start + round(step * index) for index in range(picks)})


def file_entry(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1 << 22), b""):
            digest.update(block)
    return {"size": path.stat().st_size, "sha256": digest.hexdigest()}


def describe_files(directory, relative_paths):
    directory = pathlib.Path(directory)
    files = {}
    for relative in relative_paths:
        path = directory / relative
        if path.is_dir():
            for inner in sorted(p for p in path.rglob("*") if p.is_file()):
                files[inner.relative_to(directory).as_posix()] = file_entry(inner)
        elif path.is_file():
            files[pathlib.PurePath(relative).as_posix()] = file_entry(path)
    return files


class Store:
    def __init__(self, root=None):
        self.root = pathlib.Path(root or STORE)

    @property
    def tools_root(self):
        return self.root / "tools"

    def functionality_dirs(self):
        if not self.root.is_dir():
            return []
        return sorted(path.parent for path in self.root.glob("*/*/manifest.json")
                      if path.parent.parent.name != "tools")

    def manifests(self):
        loaded, problems = [], []
        for directory in self.functionality_dirs():
            try:
                loaded.append(Manifest.load(directory))
            except StoreError as error:
                problems.append(str(error))
        return loaded, problems

    def select(self, selection):
        loaded, problems = self.manifests()
        wanted = [part.strip() for part in (selection or "").split(",") if part.strip()]
        if not wanted:
            return loaded, problems

        def names(part, manifest):
            if any(wildcard in part for wildcard in "*?["):
                return fnmatch.fnmatchcase(manifest.id, part)
            return part == manifest.id or part == manifest.data["app"]

        chosen = [manifest for manifest in loaded if any(names(part, manifest) for part in wanted)]
        unknown = [part for part in wanted if not any(names(part, manifest) for manifest in loaded)]
        problems += [f"no trace {part} in {self.root}" for part in unknown]
        return chosen, problems

    def tool(self, name):
        found = sorted(self.tools_root.glob(f"{name}-*/tool.json"))
        if not found:
            raise StoreError(f"{name} is not in {self.tools_root}: run traces.py fetch-tools on the builder "
                             "and gpu-test.sh traces-sync")
        path = found[-1]
        return path.parent, json.loads(path.read_text())


def load_pins(path=PINS):
    return json.loads(pathlib.Path(path).read_text())


def sha256_of(path):
    return file_entry(pathlib.Path(path))["sha256"]


def archive_name(entry):
    return entry.get("file") or entry["url"].rsplit("/", 1)[-1]


def download(entry, cache):
    url, sha256 = entry["url"], entry["sha256"]
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / archive_name(entry)
    if target.exists() and sha256_of(target) == sha256:
        return target
    partial = target.with_name(target.name + ".part")
    if entry.get("gh_artifact"):
        endpoint = url.removeprefix("https://api.github.com/")
        with open(partial, "wb") as written:
            done = subprocess.run(["gh", "api", endpoint], stdout=written, stderr=subprocess.PIPE, text=False)
        if done.returncode != 0:
            partial.unlink(missing_ok=True)
            raise StoreError(f"{url}: gh api exited {done.returncode} ({done.stderr.decode(errors='replace').strip()}); "
                             "GitHub serves Actions artifacts only to a signed-in gh, and only for 90 days: "
                             "fetch-tools --from a directory holding " + target.name)
    else:
        with urllib.request.urlopen(url, timeout=120) as response, open(partial, "wb") as written:
            shutil.copyfileobj(response, written, 1 << 22)
    got = sha256_of(partial)
    if got != sha256:
        partial.unlink()
        raise StoreError(f"{url}: sha256 {got}, pinned {sha256}")
    partial.rename(target)
    return target


def safe_members(names):
    kept = []
    for name in names:
        posix = pathlib.PurePosixPath(name.replace("\\", "/"))
        if posix.is_absolute() or ".." in posix.parts or re.match(r"^[A-Za-z]:", name):
            raise StoreError(f"archive member {name!r} would land outside its directory")
        kept.append(name)
    return kept


def unpack(archive, kind, scratch):
    if kind in ("zip", "wheel"):
        with zipfile.ZipFile(archive) as opened:
            safe_members(opened.namelist())
            opened.extractall(scratch)
    elif kind == "7z":
        listing = subprocess.run(["7z", "l", "-slt", str(archive)], capture_output=True, text=True, check=True).stdout
        safe_members(re.findall(r"^Path = (.+)$", listing, re.M)[1:])
        subprocess.run(["7z", "x", "-y", f"-o{scratch}", str(archive)], capture_output=True, check=True)
    else:
        raise StoreError(f"unknown archive kind {kind}")


def install_download(entry, archive, target):
    with tempfile.TemporaryDirectory(dir=target.parent) as scratch:
        scratch = pathlib.Path(scratch)
        unpack(archive, entry["unpack"], scratch)
        base = scratch / entry.get("strip", "")
        if not base.is_dir():
            raise StoreError(f"{archive.name} has no {entry.get('strip')}")
        into = target / entry.get("into", "")
        kept = 0
        for path in sorted(p for p in base.rglob("*") if p.is_file()):
            relative = path.relative_to(base).as_posix()
            patterns = entry.get("keep", ["*"])
            if not any(fnmatch.fnmatch(relative, pattern) for pattern in patterns):
                continue
            destination = into / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
            kept += 1
        if kept == 0:
            raise StoreError(f"{archive.name}: nothing matched {entry.get('keep')}")
        return kept


def fetch_tool(name, pin, store, cache=DOWNLOADS, sources=()):
    target = store.tools_root / f"{name}-{pin['version']}"
    stamp = target / "tool.json"
    if stamp.exists() and json.loads(stamp.read_text()) == pin:
        return target, False
    staging = store.tools_root / f".{name}-{pin['version']}.staging"
    shutil.rmtree(staging, ignore_errors=True)
    staging.mkdir(parents=True)
    try:
        for entry in pin["downloads"]:
            filename = archive_name(entry)
            archive = None
            for source in sources:
                candidates = list(pathlib.Path(source).rglob(filename))
                verified = [path for path in candidates if sha256_of(path) == entry["sha256"]]
                if verified:
                    archive = verified[0]
                    break
            if archive is None:
                archive = download(entry, cache / name)
            install_download(entry, archive, staging)
        for relative, lines in pin.get("write", {}).items():
            path = staging / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\r\n".join(lines) + "\r\n")
        (staging / "tool.json").write_text(json.dumps(pin, indent=1))
        shutil.rmtree(target, ignore_errors=True)
        staging.rename(target)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    return target, True


def tree_digest(directory):
    directory = pathlib.Path(directory)
    listing = [(path.relative_to(directory).as_posix(), path.stat().st_size)
               for path in sorted(directory.rglob("*")) if path.is_file()]
    return hashlib.sha256(json.dumps(listing).encode()).hexdigest()
