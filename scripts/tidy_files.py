#!/usr/bin/env python3
"""Which translation units scripts/tidy.sh should hand to clang-tidy.

The set comes from the compile database rather than from a directory walk.
Anything else picks up the guest agent, which is Windows-only and is
compiled by the mingw cross-build: clang-tidy would fall back to default
flags, fail to find windows.h, and report a file-not-found error per
translation unit that says nothing about the code.

argv[1] is the database; the rest are paths to restrict to, defaulting to
$SELECT_ROOT/src.
"""

import json
import os
import sys


def main() -> int:
    database = sys.argv[1]
    root = os.path.abspath(os.environ.get("SELECT_ROOT", "."))
    wanted = [os.path.abspath(p) for p in sys.argv[2:]] or [os.path.join(root, "src")]

    with open(database, encoding="utf-8") as handle:
        entries = json.load(handle)

    for entry in entries:
        path = os.path.abspath(os.path.join(entry["directory"], entry["file"]))
        # Dependencies fetched and built in-tree are not ours to lint.
        if not path.startswith(root + os.sep):
            continue
        if any(path == w or path.startswith(w + os.sep) for w in wanted):
            print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
