#!/usr/bin/env python3
"""Write the input of each corpus test in test/corpus/ to its own file.

Used as the seed corpus for fuzzing the scanner, so the fuzzer starts from
inputs that reach blocks, raw text, menus and the other scanner paths.
"""

import argparse
import re
import sys
from pathlib import Path

from texinfo_language import ROOT

# A test is a header (a line of `=`, the name and attributes, a line of `=`),
# the input, a line of `-`, and the expected tree.
TEST = re.compile(r"^=+\n(.*?)\n=+\n(.*?)\n-{3,}\n", re.S | re.M)


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.strip())
    arguments.add_argument(
        "output", type=Path, metavar="OUTPUT_DIR", help="directory to write the seed files to"
    )
    options = arguments.parse_args()
    options.output.mkdir(parents=True, exist_ok=True)
    count = 0
    for corpus in sorted((ROOT / "test" / "corpus").glob("*.txt")):
        for number, match in enumerate(TEST.finditer(corpus.read_text()), 1):
            (options.output / f"{corpus.stem}-{number}.texi").write_text(match.group(2))
            count += 1
    print(f"{count} inputs written to {options.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
