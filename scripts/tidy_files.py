#!/usr/bin/env python3

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
        if not path.startswith(root + os.sep):
            continue
        if any(path == w or path.startswith(w + os.sep) for w in wanted):
            print(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
