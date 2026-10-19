#!/usr/bin/env python3
"""Check that this grammar's trees for the given files haven't changed.

test/tree-snapshots.txt holds a SHA-256 digest of each file's full tree:
every node with its field and byte range (see `dump` in texinfo_language.py).
`--check` fails on a tree that differs from its digest, on a file without
one, and on a digest of a file in a checked directory that no longer exists;
`--write` records the trees of the given files; `--snapshots FILE` uses
another file of digests. After a deliberate change,
`--diff REV` shows how the trees differ from those of the parser at git
revision REV, to review before writing the new digests.

Usage: script/tree-snapshots.py [--snapshots FILE] (--check | --write | --diff REV)
       [--manuals] [--test-inputs] [--compared-manuals] [--edited-manuals] [FILE...]
"""

import argparse
import difflib
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path

from tree_sitter import Parser

from texinfo_language import ROOT, add_files_argument, dump, files_argument, load_language

SNAPSHOTS = ROOT / "test" / "tree-snapshots.txt"


def digest(parser: Parser, path: Path) -> str:
    tree = parser.parse(path.read_bytes())
    lines = hashlib.sha256()
    for line in dump(tree.root_node):
        lines.update(line.encode() + b"\n")
    return lines.hexdigest()


def read_snapshots(path: Path) -> dict[str, str]:
    snapshots = {}
    if path.exists():
        for line in path.read_text().splitlines():
            value, name = line.split("  ", 1)
            snapshots[name] = value
    return snapshots


def write_snapshots(path: Path, snapshots: dict[str, str]):
    path.write_text("".join(f"{snapshots[name]}  {name}\n" for name in sorted(snapshots)))


def show_differences(revision: str, files: list[Path], parser: Parser):
    """Prints the tree diff of each file whose tree differs at `revision`."""
    with tempfile.TemporaryDirectory() as directory:
        checkout = Path(directory) / "checkout"
        subprocess.run(
            ["git", "worktree", "add", "--detach", str(checkout), revision],
            cwd=ROOT,
            check=True,
            capture_output=True,
        )
        try:
            old = Parser(load_language(checkout))
            for path in files:
                source = path.read_bytes()
                before = list(dump(old.parse(source).root_node))
                after = list(dump(parser.parse(source).root_node))
                if before != after:
                    sys.stdout.writelines(
                        line + "\n"
                        for line in difflib.unified_diff(
                            before, after, f"{path} ({revision})", f"{path} (now)", lineterm=""
                        )
                    )
        finally:
            subprocess.run(
                ["git", "worktree", "remove", "--force", str(checkout)],
                cwd=ROOT,
                check=True,
                capture_output=True,
            )


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = arguments.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help="fail on any changed tree")
    mode.add_argument("--write", action="store_true", help="record the trees")
    mode.add_argument("--diff", metavar="REV", help="show tree diffs against git revision REV")
    arguments.add_argument(
        "--snapshots", type=Path, default=SNAPSHOTS, help="the digests (default %(default)s)"
    )
    add_files_argument(arguments)
    options = arguments.parse_args()
    options.files = files_argument(options)

    parser = Parser(load_language())
    if options.diff:
        show_differences(options.diff, options.files, parser)
        return 0

    snapshots = read_snapshots(options.snapshots)
    current = {str(path): digest(parser, path) for path in options.files}
    # A file gone from a directory that was checked, such as a manual removed
    # from the examples, leaves a stale digest, which --write drops.
    directories = {str(Path(name).parts[0]) for name in current}
    gone = [
        name for name in snapshots if Path(name).parts[0] in directories and name not in current
    ]
    if options.write:
        for name in gone:
            del snapshots[name]
        snapshots.update(current)
        write_snapshots(options.snapshots, snapshots)
        return 0

    changed = [name for name, value in current.items() if snapshots.get(name) != value]
    for name in changed:
        state = "no digest" if name not in snapshots else "tree changed"
        print(f"{state}: {name}", file=sys.stderr)
    for name in gone:
        print(f"no longer checked: {name}", file=sys.stderr)
    if changed or gone:
        print(
            f"{len(changed)} trees differ from {options.snapshots}. If the change "
            "is deliberate, review it with `just snapshots-diff HEAD FILE...` "
            "and record it with `just snapshots-write`.",
            file=sys.stderr,
        )
        return 1
    print(f"{len(current)} trees match their snapshots")
    return 0


if __name__ == "__main__":
    sys.exit(main())
