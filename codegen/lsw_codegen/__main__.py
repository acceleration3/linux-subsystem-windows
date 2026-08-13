"""CLI entry point for ``lsw-codegen``.

Each emit backend is registered in ``emit.ALL_BACKENDS`` and run in
order. ``--check`` re-emits to a temp directory and diffs against the
committed copy; this is what CI runs.
"""

from __future__ import annotations

import argparse
import filecmp
import sys
import tempfile
from pathlib import Path

from . import lower, parse, profile
from .emit import ALL_BACKENDS, Backend
from .ir import Registry


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="lsw-codegen",
        description="Generate LSW serialization code from Khronos vk.xml.",
    )
    p.add_argument("--xml", type=Path, required=True, help="Path to Vulkan-Headers/registry/vk.xml")
    p.add_argument(
        "--profile",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "profile.toml",
        help="Path to a profile.toml selecting commands/extensions.",
    )
    p.add_argument(
        "--ids",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "protocol_ids.toml",
        help="Path to a protocol_ids.toml mapping commands to wire IDs.",
    )
    p.add_argument("--out", type=Path, required=True, help="Output directory; created if missing.")
    p.add_argument(
        "--check",
        action="store_true",
        help="Exit non-zero if --out does not match what would be generated.",
    )
    p.add_argument("--verbose", "-v", action="store_true", help="Verbose progress output.")
    return p


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    if not args.xml.is_file():
        print(f"error: --xml does not exist: {args.xml}", file=sys.stderr)
        return 2
    if not args.profile.is_file():
        print(f"error: --profile does not exist: {args.profile}", file=sys.stderr)
        return 2
    if not args.ids.is_file():
        print(f"error: --ids does not exist: {args.ids}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)

    if args.verbose:
        print("lsw-codegen v0.0.1")
        print(f"  xml:     {args.xml}")
        print(f"  profile: {args.profile}")
        print(f"  ids:     {args.ids}")
        print(f"  out:     {args.out}")

    prof = profile.load(args.profile, args.ids)
    if args.verbose:
        print(f"  api_version: {prof.api_version[0]}.{prof.api_version[1]}")
        print(f"  extensions:  {len(prof.extensions)} ({', '.join(prof.extensions)})")

    root = parse.load_xml(args.xml)
    reg = lower.lower(root, prof)

    if args.verbose:
        print(f"  commands lowered: {len(reg.commands)}")

    if args.check:
        return _check(reg, args.out, ALL_BACKENDS)

    # Persist any newly-assigned wire IDs back to protocol_ids.toml so
    # the developer doesn't have to hand-edit it.
    profile.write_ids(prof)

    for backend in ALL_BACKENDS:
        if args.verbose:
            print(f"  emit {backend.name} -> {', '.join(backend.outputs)}")
        backend.emit(reg, args.out)
    return 0


def _check(reg: Registry, out: Path, backends: tuple[Backend, ...]) -> int:
    """Generate to a temp dir and diff each backend's outputs against ``out``."""
    with tempfile.TemporaryDirectory() as td:
        tmp = Path(td)
        for backend in backends:
            backend.emit(reg, tmp)

        differ = False
        expected = sorted(p.name for p in tmp.iterdir())
        # Dotfiles are configuration that lives alongside the output, not
        # output: generated/.clang-format is what stops an editor
        # reformatting these files and breaking this very check.
        actual = sorted(p.name for p in out.iterdir() if not p.name.startswith("."))
        if expected != actual:
            print(
                f"error: file set differs.\n  expected: {expected}\n  actual:   {actual}",
                file=sys.stderr,
            )
            differ = True
        for name in expected:
            if not filecmp.cmp(tmp / name, out / name, shallow=False):
                print(f"error: {name} differs from committed copy", file=sys.stderr)
                differ = True
        return 1 if differ else 0


if __name__ == "__main__":
    raise SystemExit(main())
