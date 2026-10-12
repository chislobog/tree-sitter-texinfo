#!/usr/bin/env python3
"""Parse files, and check which fail to parse and how fast each parses.

- Parse errors: the files that fail to parse must be exactly those listed in
  test/parse-errors.txt (or the --expected FILE), each after a `#` comment with the reason (an error
  in the source that texi2any reports too, or one a tree-sitter tree can't
  represent).
- Speed: a file of at least 20 KB that parses more than --slowdown times
  (default 10) slower, in bytes per second, than the median of those files
  fails, so a grammar change that makes some input pathologically slow
  shows. Relative to the median, so it holds on any machine. The time is
  the CPU time of the parse in this process, which other processes running
  at once (as `nix flake check`'s other checks do) hardly affect. A file
  that looks slow is timed again: it only fails if it is still slow every
  time.

Usage: script/parse-examples.py [--manuals] [--test-inputs] [--compared-manuals]
       [--edited-manuals] [--slowdown N] [--expected FILE] [FILE...]
"""

import argparse
import statistics
import sys
import time
from pathlib import Path

from texinfo_language import ROOT, add_files_argument, files_argument, load_language

from tree_sitter import Parser

EXPECTED = ROOT / "test" / "parse-errors.txt"
MIN_BYTES = 20_000
# Times a file that looks slow is timed again.
RETRIES = 3


def read_expected(path: Path) -> dict[str, list[str]]:
    """The files expected to fail, with their comment lines."""
    expected: dict[str, list[str]] = {}
    comment: list[str] = []
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            comment.append(line)
        elif line.strip():
            expected[line] = comment
            comment = []
    return expected


def parse(parser: Parser, files: list) -> list[dict]:
    """Each file's name, size, whether it parses without errors (no ERROR or
    MISSING nodes), and the CPU time of its parse."""
    summaries = []
    for name in files:
        source = Path(name).read_bytes()
        start = time.process_time()
        tree = parser.parse(source)
        seconds = time.process_time() - start
        summaries.append(
            {
                "file": str(name),
                "successful": not tree.root_node.has_error,
                "bytes": len(source),
                "seconds": seconds,
            }
        )
    return summaries


def speeds(summaries: list[dict]) -> dict[str, float]:
    """Bytes per second of the files of at least MIN_BYTES."""
    return {
        summary["file"]: summary["bytes"] / summary["seconds"]
        for summary in summaries
        if summary["bytes"] >= MIN_BYTES and summary["seconds"] > 0
    }


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    arguments.add_argument(
        "--slowdown",
        type=float,
        default=10.0,
        help="slowdown factor before a file fails (default 10)",
    )
    arguments.add_argument(
        "--expected",
        type=Path,
        default=EXPECTED,
        metavar="FILE",
        help="file listing expected parse errors (default %(default)s)",
    )
    add_files_argument(arguments)
    options = arguments.parse_args()
    options.files = files_argument(options)

    parser = Parser(load_language())
    summaries = parse(parser, options.files)
    checked = {s["file"] for s in summaries}

    failures = 0
    expected = read_expected(options.expected)
    failed = {s["file"] for s in summaries if not s["successful"]}
    for name in sorted(failed - expected.keys()):
        print(f"Parse errors: {name}", file=sys.stderr)
    for name in sorted(expected.keys() - failed):
        if name in checked:
            print(f"No longer fails to parse: {name}", file=sys.stderr)
    for name in sorted(failed & expected.keys()):
        if not expected[name]:
            print(f"No reason given: {name}", file=sys.stderr)
    if failed != expected.keys() & checked or any(
        not expected[n] for n in failed & expected.keys()
    ):
        print(
            f"The files that fail to parse don't match {options.expected}.",
            file=sys.stderr,
        )
        failures += 1

    rates = speeds(summaries)
    if rates:
        median = statistics.median(rates.values())
        slow = {name: rate for name, rate in rates.items() if rate * options.slowdown < median}
        for _ in range(RETRIES):
            if not slow:
                break
            again = speeds(parse(parser, list(slow)))
            for name, rate in again.items():
                rates[name] = max(rates[name], rate)
            slow = {name: rates[name] for name in slow if rates[name] * options.slowdown < median}
        for name, rate in sorted(slow.items()):
            print(
                f"Slow: {name} parses at {rate / 1e6:.2f} MB/s, the median is {median / 1e6:.2f}",
                file=sys.stderr,
            )
        if slow:
            failures += 1
        print(
            f"{len(summaries)} files, {len(failed)} with parse errors; median speed "
            f"{median / 1e6:.2f} MB/s, slowest {min(rates.values()) / 1e6:.2f} MB/s"
        )
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
