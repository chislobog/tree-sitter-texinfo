#!/usr/bin/env python3
"""Compare the parser's state count with an earlier commit's: warn when it
grows by 10% or more, fail at 50% or more. Each feature so far added 20-35%,
while splitting arguments into fields multiplied it by 8.6, so large jumps
deserve a look before they land.

Only the standard library: CI runs this before the dev shell is set up. It
can't be a flake check, as the source Nix checks has no git history. The
`::warning::` and `::error::` lines are GitHub Actions annotations.

Usage: script/parser-size.py BASE_COMMIT
"""

import re
import subprocess
import sys
from pathlib import Path

STATE_COUNT = re.compile(r"^#define STATE_COUNT (\d+)", re.M)


def state_count(text: str) -> int | None:
    match = STATE_COUNT.search(text)
    return int(match.group(1)) if match else None


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    base = sys.argv[1]
    old_parser = subprocess.run(
        ["git", "show", f"{base}:src/parser.c"], capture_output=True, text=True, check=False
    )
    old = state_count(old_parser.stdout) if old_parser.returncode == 0 else None
    if not old:
        print(f"No parser at {base} to compare with.")
        return 0
    parser = Path(__file__).resolve().parent.parent / "src" / "parser.c"
    text = parser.read_text()
    new = state_count(text) or 0
    change = int((new - old) * 100 / old)
    size = len(text.encode()) // 1048576
    print(f"Parser states: {old} -> {new} ({change}%), parser.c {size} MB")
    if change >= 50:
        print(
            f"::error::The parser's state count grew by {change}%. Check the grammar "
            "change for a combinatorial blowup."
        )
        return 1
    if change >= 10:
        print(f"::warning::The parser's state count grew by {change}% (parser.c is {size} MB).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
