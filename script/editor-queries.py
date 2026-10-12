#!/usr/bin/env python3
"""Generate each editor's queries from those in queries/.

queries/ follows no single editor's capture names: where editors
disagree, it takes the plainer name (`.tsqueryrc.json` lists them). Each
editor has a table: the files it uses, the capture names to rename, and the
captures it doesn't have, whose patterns are left out (with the comment
before them). Only captures are renamed, not text in string
literals or comments. Each file starts with a line saying where it comes
from; the files are committed, checked against the editor's own capture
names (editors/NAME/tsqueryrc.json), and `--check` fails if one isn't what
this script would write.

- Helix (editors/helix/queries/): `markup.bold`,
  `constant.character.escape` and `keyword.control.conditional`. Helix
  doesn't fold.
- Neovim (editors/neovim/queries/): nvim-treesitter-textobjects' `outer`/
  `inner` instead of `around`/`inside`, without `entry`. Neovim doesn't use
  tags.

Usage: script/editor-queries.py [--check]
"""

import argparse
import re
import sys

from texinfo_language import ROOT

QUERIES = ROOT / "queries"

EDITORS = {
    "helix": {
        "files": ["highlights.scm", "injections.scm", "textobjects.scm", "tags.scm"],
        "rename": {
            "markup.strong": "markup.bold",
            "string.escape": "constant.character.escape",
            "keyword.conditional": "keyword.control.conditional",
        },
        "drop": [],
    },
    "neovim": {
        "files": ["highlights.scm", "injections.scm", "folds.scm", "textobjects.scm"],
        "rename": {
            r"(\w+)\.around": r"\1.outer",
            r"(\w+)\.inside": r"\1.inner",
        },
        "drop": [r"entry\.\w+"],
    },
}

# A string literal, a comment, or a capture.
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|;[^\n]*|@([\w.]+)')


def rename(text: str, table: dict[str, str]) -> str:
    def capture(match: re.Match) -> str:
        name = match.group(1)
        if name is None:
            return match.group(0)
        for pattern, replacement in table.items():
            if re.fullmatch(pattern, name):
                return "@" + re.sub(pattern, replacement, name)
        return match.group(0)

    return TOKEN.sub(capture, text)


def patterns(text: str) -> list[str]:
    """The top-level patterns, each with the comment lines and blank line
    before it."""
    result: list[str] = []
    start = depth = 0
    i = 0
    while i < len(text):
        match = TOKEN.match(text, i)
        if match and match.group(1) is None:
            i = match.end()
            continue
        char = text[i]
        if char in "([":
            depth += 1
        elif char in ")]":
            depth -= 1
        elif char == "\n" and depth == 0:
            # A pattern ends at a newline outside brackets, unless the next
            # line continues it (a capture or quantifier after `)` or `]`).
            rest = text[i + 1 :].lstrip(" ")
            if rest.startswith(("@", "+", "*", "?")):
                i += 1
                continue
            chunk = text[start : i + 1]
            if chunk.strip() and not all(
                line.strip().startswith(";") or not line.strip() for line in chunk.splitlines()
            ):
                result.append(chunk)
                start = i + 1
        i += 1
    if text[start:].strip():
        result.append(text[start:])
    return result


def generate(source: str, editor: dict) -> str:
    kept = []
    for pattern in patterns(source):
        names = [m.group(1) for m in TOKEN.finditer(pattern) if m.group(1)]
        if any(re.fullmatch(drop, name) for drop in editor["drop"] for name in names):
            continue
        kept.append(rename(pattern, editor["rename"]))
    return "".join(kept).strip("\n") + "\n"


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    arguments.add_argument("--check", action="store_true", help="fail if a file is out of date")
    options = arguments.parse_args()
    stale = []
    for name, editor in EDITORS.items():
        directory = ROOT / "editors" / name / "queries"
        directory.mkdir(parents=True, exist_ok=True)
        for file in editor["files"]:
            header = f"; Generated from queries/{file} by script/editor-queries.py.\n"
            text = header + generate((QUERIES / file).read_text(), editor)
            target = directory / file
            if options.check:
                if not target.exists() or target.read_text() != text:
                    stale.append(str(target.relative_to(ROOT)))
            else:
                target.write_text(text)
    for path in stale:
        print(f"Out of date: {path}; run script/editor-queries.py", file=sys.stderr)
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
