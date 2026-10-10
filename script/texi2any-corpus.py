#!/usr/bin/env python3
"""Write texi2any's test inputs as corpus tests, so `tree-sitter fuzz` fuzzes
them along with ours.

The tests go to test/corpus/texi2any-tests.txt, which git ignores: the inputs
come from Texinfo's sources (GPL-3), under a different licence than this
repository, and are extracted by the dev shell. Their expected trees are
this parser's own, written by `tree-sitter test --update` (inputs with parse
errors are marked `:error`): the point is
fuzzing incremental parsing on many small, often invalid inputs, not
checking the trees, which test/tree-snapshots.txt does.

Usage: script/texi2any-corpus.py [FILE...]
"""

import argparse
import sys

from texinfo_language import ROOT, add_files_argument, files_argument, load_language

from tree_sitter import Parser

OUTPUT = ROOT / "test" / "corpus" / "texi2any-tests.txt"
# A delimiter suffix, so lines of `=` or `-` in an input end nothing.
SUFFIX = "|texi2any"


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    add_files_argument(arguments)
    options = arguments.parse_args()
    parser = Parser(load_language())
    tests = []
    for name in files_argument(options):
        source = name.read_bytes()
        try:
            text = source.decode("utf-8")
        except UnicodeDecodeError:
            # Corpus files are UTF-8; the inputs in other encodings are left
            # out.
            continue
        # An input with parse errors is marked `:error`, which tree-sitter
        # tests only for an error; the others get their tree from --update.
        error = ":error\n" if parser.parse(source).root_node.has_error else ""
        tests.append(
            f"{'=' * 80}{SUFFIX}\n{name}\n{error}{'=' * 80}{SUFFIX}\n\n"
            f"{text.rstrip(chr(10))}\n\n{'-' * 3}{SUFFIX}\n\n(source_file)\n"
        )
    OUTPUT.write_text("\n".join(tests))
    print(f"{len(tests)} tests written to {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
