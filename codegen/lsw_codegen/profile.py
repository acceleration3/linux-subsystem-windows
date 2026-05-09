"""Load profile.toml and protocol_ids.toml.

The profile is now Vulkan-version + extension driven; ``lower.py`` walks
``vk.xml`` to compute the actual command set. ``protocol_ids.toml``
remains the source of truth for stable wire IDs but is auto-extended on
each regeneration so the developer never hand-edits it.
"""

from __future__ import annotations

import sys
import tomllib
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class Profile:
    api_version: tuple[int, int] = (1, 0)
    extensions: list[str] = field(default_factory=list)
    excluded_commands: set[str] = field(default_factory=set)
    platforms: list[str] = field(default_factory=lambda: ["common", "linux"])
    sync_overrides: dict[str, str] = field(default_factory=dict)
    command_ids: dict[str, int] = field(default_factory=dict)
    # Path back to protocol_ids.toml so the lowering stage can append
    # newly-discovered commands without losing the existing IDs.
    ids_path: Path | None = None


def load(profile_path: Path, ids_path: Path) -> Profile:
    with profile_path.open("rb") as f:
        prof = tomllib.load(f)
    with ids_path.open("rb") as f:
        ids = tomllib.load(f)

    p = Profile()
    p.api_version = _parse_api_version(prof.get("api_version", "1.0"))
    p.extensions = list(prof.get("extensions", {}).get("include", []))
    p.excluded_commands = set(prof.get("commands", {}).get("exclude", []))
    p.platforms = list(prof.get("platforms", {}).get("include", ["common", "linux"]))
    p.sync_overrides = dict(prof.get("sync_overrides", {}))
    p.command_ids = {k: int(v) for k, v in ids.get("commands", {}).items()}
    p.ids_path = ids_path
    return p


def _parse_api_version(raw: str | int | float) -> tuple[int, int]:
    if isinstance(raw, (int, float)):
        major, minor = str(raw).split(".") if "." in str(raw) else (str(raw), "0")
        return int(major), int(minor)
    parts = str(raw).split(".")
    if len(parts) < 2 or not all(p.isdigit() for p in parts[:2]):
        print(f"error: api_version must look like '1.3', got {raw!r}", file=sys.stderr)
        raise SystemExit(2)
    return int(parts[0]), int(parts[1])


def assign_missing_ids(profile: Profile, command_names: list[str]) -> list[str]:
    """Make sure every command in ``command_names`` has a stable wire ID.

    Existing IDs in ``profile.command_ids`` are never moved. New commands
    are appended at the highest-id+1. Returns the list of newly-assigned
    command names so the caller can persist them via ``write_ids``.
    """
    new: list[str] = []
    next_id = max(profile.command_ids.values(), default=0x10000) + 1
    for name in command_names:
        if name not in profile.command_ids:
            profile.command_ids[name] = next_id
            new.append(name)
            next_id += 1
    return new


def write_ids(profile: Profile) -> None:
    """Re-serialize protocol_ids.toml with all current IDs.

    Sorted by ID so the file stays human-readable and merge conflicts are
    rare. Existing entries keep their numeric value.
    """
    if profile.ids_path is None:
        return
    rows = sorted(profile.command_ids.items(), key=lambda kv: kv[1])
    lines = [
        "# Stable command IDs for the LSW wire protocol.",
        "# Auto-maintained by codegen — do not hand-edit unless you're",
        "# bumping the protocol major version. New commands get appended",
        "# at the next free ID; existing IDs never move.",
        "",
        "[commands]",
    ]
    for name, cid in rows:
        lines.append(f'"{name}" = {cid}')
    lines.append("")
    profile.ids_path.write_text("\n".join(lines), encoding="utf-8")
