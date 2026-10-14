#!/usr/bin/env python3
"""Extract the example manuals' Texinfo files, as listed in a JSON manifest
that flake.nix writes for the `examples` package.

Each entry of the manifest is an object:

- `source`: a source archive (tar, compressed or not), or a directory.
- `into`: the directory under OUT to write its files to.
- `strip`: how many leading components of a file's path to drop, as tar's
  `--strip-components` does.
- `members`: for an archive, the members to extract, as tar's `--wildcards`
  patterns, where `*` also matches `/`; for a directory, shell globs of its
  files, where `*` doesn't.
- `exclude` (optional): archive members to leave out, as tar's `--exclude`.

Only Texinfo files (`.texi`, `.texinfo`, `.txi`) are written.

Usage: script/extract-examples.py MANIFEST OUT
"""

import argparse
import fnmatch
import json
import shutil
import sys
import tarfile
from pathlib import Path, PurePosixPath

TEXINFO_SUFFIXES = (".texi", ".texinfo", ".txi")


def stripped(name: str, strip: int) -> PurePosixPath | None:
    """`name` without its first `strip` components, or None if nothing is
    left, as tar skips such a member."""
    parts = PurePosixPath(name).parts[strip:]
    return PurePosixPath(*parts) if parts else None


def extract_archive(entry: dict, into: Path) -> int:
    count = 0
    with tarfile.open(entry["source"]) as archive:
        for member in archive:
            # Hard links are files too; tarfile reads the file they name.
            if not (member.isfile() or member.islnk()):
                continue
            if not member.name.endswith(TEXINFO_SUFFIXES):
                continue
            if not any(fnmatch.fnmatchcase(member.name, p) for p in entry["members"]):
                continue
            if any(fnmatch.fnmatchcase(member.name, p) for p in entry.get("exclude", [])):
                continue
            path = stripped(member.name, entry["strip"])
            if path is None:
                continue
            target = into / path
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, target.open("wb") as file:
                shutil.copyfileobj(source, file)
            count += 1
    return count


def extract_directory(entry: dict, into: Path) -> int:
    root = Path(entry["source"])
    count = 0
    for pattern in entry["members"]:
        for source in sorted(root.glob(pattern)):
            if not source.is_file() or not source.name.endswith(TEXINFO_SUFFIXES):
                continue
            path = stripped(source.relative_to(root).as_posix(), entry["strip"])
            if path is None:
                continue
            target = into / path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            count += 1
    return count


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    arguments.add_argument("manifest", type=Path)
    arguments.add_argument("out", type=Path)
    options = arguments.parse_args()
    for entry in json.loads(options.manifest.read_text()):
        into = options.out / entry["into"]
        source = Path(entry["source"])
        extract = extract_directory if source.is_dir() else extract_archive
        count = extract(entry, into)
        # A pattern that matches nothing after an update of the sources
        # would silently drop a manual.
        if count == 0:
            raise SystemExit(f"no Texinfo files in {source} for {entry['members']}")
        print(f"{count} files from {source.name} into {entry['into']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
