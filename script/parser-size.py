#!/usr/bin/env python3
"""Check the parser's state count against its budget, BUDGET below.

Each grammar feature adds a few percent of states, too little to notice one
at a time: the parser went from 5,623 states to 12,387 in steps of 1-8%,
none of which a check against the previous commit would flag. So the budget
is fixed, and raised only on purpose, in the commit that needs it: the check
fails when the parser has more states. It also fails when the parser has
more than 10% fewer, so that the budget follows a reduction down rather than
leaving room to grow back unnoticed.

Usage: script/parser-size.py [--parser FILE] [--budget STATES]
"""

import argparse
import math
import re
import sys
from pathlib import Path

# About 5% over the state count. Raise it with the grammar change that needs
# it, after measuring what makes the change cost what it does (CLAUDE.md).
BUDGET = 13_300

STATE_COUNT = re.compile(r"^#define STATE_COUNT (\d+)", re.M)


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    arguments.add_argument(
        "--parser", type=Path, default=Path(__file__).resolve().parent.parent / "src" / "parser.c"
    )
    arguments.add_argument("--budget", type=int, default=BUDGET)
    options = arguments.parse_args()
    text = options.parser.read_text()
    match = STATE_COUNT.search(text)
    if not match:
        print(f"{options.parser} has no STATE_COUNT")
        return 1
    states = int(match.group(1))
    budget = options.budget
    size = f"{options.parser.name} is {len(text.encode()) / 1048576:.1f} MB"
    if states > budget:
        print(
            f"The parser has {states} states, over its budget of {budget} ({size}). If the "
            "grammar change is worth it, raise BUDGET in script/parser-size.py with it."
        )
        return 1
    if states < budget * 0.9:
        lower = math.ceil(states * 1.05 / 100) * 100
        print(
            f"The parser has {states} states, more than 10% under its budget of {budget}: lower "
            f"BUDGET in script/parser-size.py to {lower}."
        )
        return 1
    print(f"The parser has {states} states, within its budget of {budget} ({size}).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
