import pathlib
import re

ARG_INFO = "-cl-kernel-arg-info"


def option_words(text):
    return tuple(word for word in text.split() if word != ARG_INFO)


def program_builds(programs):
    builds = []
    for options in sorted(pathlib.Path(programs).glob("CLI_*_options.txt")):
        program = options.name.split("_")[1]
        source = options.parent / f"CLI_{program}_source.cl"
        if source.is_file():
            builds.append((source, option_words(options.read_text(errors="replace"))))
    return builds


def defines_kernel(source, name):
    return re.search(r"(?<!\w)" + re.escape(name) + r"(?!\w)", source) is not None


def source_binaries(replay, programs):
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
    notes = []
    if sourced:
        notes.append(f"{len(sourced)} kernel(s) the application made from a device binary given their program's "
                     f"recorded source ({', '.join(f'{enqueue} <- {source}' for enqueue, source in sourced)})")
    if left:
        notes.append(f"{len(left)} kernel(s) left as device binaries ({'; '.join(f'{e}: {why}' for e, why in left)})")
    return "; ".join(notes)
