#!/usr/bin/env python3
"""Write the input of each corpus test in test/corpus/ to its own file.

Used as the seed corpus for fuzzing the scanner, so the fuzzer starts from
inputs that reach blocks, raw text, menus and the other scanner paths.
"""

import argparse
import sys
from pathlib import Path

from texinfo_language import corpus_tests


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.strip())
    arguments.add_argument(
        "output", type=Path, metavar="OUTPUT_DIR", help="directory to write the seed files to"
    )
    options = arguments.parse_args()
    options.output.mkdir(parents=True, exist_ok=True)
    count = 0
    numbers: dict[str, int] = {}
    for corpus, _, text in corpus_tests():
        numbers[corpus.stem] = numbers.get(corpus.stem, 0) + 1
        path = options.output / f"{corpus.stem}-{numbers[corpus.stem]}.texi"
        # With its carriage returns, which the corpus keeps as written.
        with path.open("w", newline="") as file:
            file.write(text)
        count += 1
    print(f"{count} inputs written to {options.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
