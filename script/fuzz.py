#!/usr/bin/env python3
"""Run `tree-sitter fuzz` with the given arguments, and fail if it reports a
failure: it exits with 0 whatever it finds (tree-sitter 0.26.11), so that a
check running it alone passes without checking. Fail too if it fuzzed no
test, as when `--include` matches none, which it doesn't report either.

Usage: script/fuzz.py [tree-sitter fuzz arguments]
"""

import re
import subprocess
import sys

# A test that fuzzing broke, the count of them, or a crash.
FAILURE = re.compile(r"^(?:Incorrect|.*failed fuzzing|thread .* panicked).*$", re.M)
# A test fuzzed, each of which has a numbered line.
TEST = re.compile(r"^\s+\d+\. ", re.M)


def problems(output: str) -> list[str]:
    """The lines of `tree-sitter fuzz`'s output that report a failure, and
    whether it fuzzed no test."""
    found = FAILURE.findall(output)
    if not TEST.search(output):
        found.append("no test fuzzed")
    return found


def main() -> int:
    result = subprocess.run(
        ["tree-sitter", "fuzz", *sys.argv[1:]],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        errors="replace",
        check=False,
    )
    print(result.stdout, end="")
    found = problems(result.stdout)
    for failure in found:
        print(f"FAILED: {failure}", file=sys.stderr)
    return 1 if result.returncode or found else 0


if __name__ == "__main__":
    sys.exit(main())
