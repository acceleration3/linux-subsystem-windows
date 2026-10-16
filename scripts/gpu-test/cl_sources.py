"""CLIntercept kernels captured with only their program's device binary,
given that program's source back.

An application can make a program from a binary it got from the driver
earlier in the same run (Resolve does, for some variants of its DVIP
programs, even with its on-disk kernel cache cleared). CLIntercept then
dumps the enqueue with `DeviceBinary0.bin` and no `kernel.cl`, and its
run.py loads the binary - which is avgpu's, sealed for the host GPU and
driver that built it, so the replay on any other GPU (or driver build)
fails. The capture's `Programs/` holds every source the application built,
with each set of options it built it with: the source that defines the
kernel, built with the enqueue's own options, is the program the binary came
from, so it is written as the enqueue's `kernel.cl` and the binary dropped.
"""
import pathlib
import re

ARG_INFO = "-cl-kernel-arg-info"
"""What CLIntercept appends to every build (AppendBuildOptions)."""


def option_words(text):
    """Build options as compared: their words, without CLIntercept's own."""
    return tuple(word for word in text.split() if word != ARG_INFO)


def program_builds(programs):
    """[(source path, option words)] for every build Programs/ records:
    CLI_<program>_source.cl with each CLI_<program>_<n>_<hash>_options.txt."""
    builds = []
    for options in sorted(pathlib.Path(programs).glob("CLI_*_options.txt")):
        program = options.name.split("_")[1]
        source = options.parent / f"CLI_{program}_source.cl"
        if source.is_file():
            builds.append((source, option_words(options.read_text(errors="replace"))))
    return builds


def defines_kernel(source, name):
    """Whether `source` names `name` at all - not whether a `__kernel`
    function of that name is declared: Resolve's sources spell even that
    through obfuscated #defines (`#define z4fe1... __kernel`, names behind
    macros of their own), and its kernel names are unique hashes."""
    return re.search(r"(?<!\w)" + re.escape(name) + r"(?!\w)", source) is not None


def source_binaries(replay, programs):
    """Every enqueue under `replay` that has a device binary and no source
    given its program's source, when exactly one recorded build defines its
    kernel with its options: [(enqueue directory name, source name)], and
    [(enqueue directory name, why not)] for the ones left as they were."""
    builds = program_builds(programs) if pathlib.Path(programs).is_dir() else []
    texts = {}
    sourced, left = [], []
    for enqueue in sorted(pathlib.Path(replay).glob("Enqueue_*")):
        binaries = sorted(enqueue.glob("DeviceBinary*.bin"))
        if not binaries or (enqueue / "kernel.cl").exists():
            continue
        name = (enqueue / "kernelName.txt").read_text(errors="replace").strip()
        wanted = option_words((enqueue / "buildOptions.txt").read_text(errors="replace"))
        matches = []
        for source, words in builds:
            if words != wanted:
                continue
            if source not in texts:
                texts[source] = source.read_text(errors="replace")
            if defines_kernel(texts[source], name):
                matches.append(source)
        if len(matches) != 1:
            left.append((enqueue.name, f"{len(matches)} recorded builds define {name} with its options"))
            continue
        (enqueue / "kernel.cl").write_text(texts[matches[0]])
        for binary in binaries:
            binary.unlink()
        sourced.append((enqueue.name, matches[0].name))
    return sourced, left


def note(sourced, left):
    """What source_binaries did, for the manifest."""
    notes = []
    if sourced:
        notes.append(f"{len(sourced)} kernel(s) the application made from a device binary given their program's "
                     f"recorded source ({', '.join(f'{enqueue} <- {source}' for enqueue, source in sourced)})")
    if left:
        notes.append(f"{len(left)} kernel(s) left as device binaries ({'; '.join(f'{e}: {why}' for e, why in left)})")
    return "; ".join(notes)
