#!/usr/bin/env python3
"""Write the input of each corpus test in test/corpus/ to its own file.

Used as the seed corpus for fuzzing the scanner, so the fuzzer starts from
inputs that reach blocks, raw text, menus and the other scanner paths.

Usage: script/corpus-inputs.py OUTPUT_DIR
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# A test is a header (a line of `=`, the name and attributes, a line of `=`),
# the input, a line of `-`, and the expected tree.
TEST = re.compile(r"^=+\n(.*?)\n=+\n(.*?)\n-{3,}\n", re.S | re.M)


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    output = Path(sys.argv[1])
    output.mkdir(parents=True, exist_ok=True)
    count = 0
    for corpus in sorted((ROOT / "test" / "corpus").glob("*.txt")):
        for number, match in enumerate(TEST.finditer(corpus.read_text()), 1):
            (output / f"{corpus.stem}-{number}.texi").write_text(match.group(2))
            count += 1
    print(f"{count} inputs written to {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
