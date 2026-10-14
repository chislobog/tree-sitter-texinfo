#!/usr/bin/env python3
"""Write files as corpus tests, so `tree-sitter fuzz` fuzzes them along with
ours: texi2any's test inputs and the example manuals.

The tests go to test/corpus/fuzz-inputs.txt, which git ignores: the files
come from other projects, under other licences, and are extracted by Nix,
linked by the dev shell. Delete it after fuzzing, or `tree-sitter test` runs
it too. Each test is named by its file's path, for `tree-sitter fuzz --include`.
Its expected tree is this parser's own (checking the trees is what
test/tree-snapshots.txt does): written here rather than by `tree-sitter test
--update`, which rewrites the delimiters without their suffix, so that a line
of dashes in a manual ends its input.

Usage: script/fuzz-corpus.py [--manuals] [--test-inputs] [--compared-manuals]
       [--edited-manuals] [FILE...]
"""

import argparse
import sys

from texinfo_language import ROOT, add_files_argument, files_argument, load_language

from tree_sitter import Parser

OUTPUT = ROOT / "test" / "corpus" / "fuzz-inputs.txt"
# A delimiter suffix, so lines of `=` or `-` in an input end nothing.
SUFFIX = "|fuzz"


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    add_files_argument(arguments)
    options = arguments.parse_args()
    parser = Parser(load_language())
    tests = []
    skipped = []
    for path in files_argument(options):
        try:
            text = path.read_bytes().decode("utf-8")
        except UnicodeDecodeError:
            # Corpus files are UTF-8; the inputs in other encodings are left
            # out.
            skipped.append(path)
            continue
        # The input starts right after the header; `tree-sitter test` keeps
        # one of its final newlines, and the blank line before the `---`
        # ends it.
        text = text.rstrip("\n")
        tree = parser.parse(text.encode() + b"\n").root_node
        tests.append(
            f"{'=' * 80}{SUFFIX}\n{path}\n{'=' * 80}{SUFFIX}\n"
            f"{text}\n\n{'-' * 3}{SUFFIX}\n\n{tree}\n"
        )
    OUTPUT.write_text("\n".join(tests))
    print(f"{len(tests)} tests written to {OUTPUT.relative_to(ROOT)}")
    if skipped:
        names = ", ".join(str(path) for path in skipped)
        print(f"{len(skipped)} left out, not being UTF-8: {names}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
