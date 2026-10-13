#!/usr/bin/env python3
"""Check that the checks still catch what they are for.

Each check runs on small inputs in test/checks/ whose outcome is known, with
temporary lists of expected results: it must fail where the input differs
from what is expected, with the message that says so, and pass where it
doesn't. A check that silently stopped detecting anything, after a change to
its script or to the tools it runs, fails here.

Usage: script/test-checks.py
"""

import importlib.util
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from texinfo_language import ROOT, load_language

from tree_sitter import Parser

CHECKS = Path("test/checks")
TAB = CHECKS / "tab-outside-multitable.texi"
SAME = CHECKS / "same.texi"
PARSE_ERROR = CHECKS / "parse-error.texi"
COMPARED = [
    TAB,
    SAME,
    CHECKS / "expanded.texi",
    CHECKS / "as-written.texi",
    CHECKS / "both-expanded.texi",
]
TAB_DIFFERENCE = "line 3: ours '@tab', texi2any '(nothing)'"
# A line command in the middle of a line, which is one only for texi2any: the
# same command on both sides, with different arguments.
ARGUMENTS = CHECKS / "line-command-in-text.texi"
ARGUMENTS_DIFFERENCE = "line 1: @sp arguments: ours '(none)', texi2any '2'"


class Checker:
    def __init__(self, directory: Path):
        self.directory = directory
        self.failures = 0

    def file(self, name: str, text: str) -> Path:
        path = self.directory / name
        path.write_text(text)
        return path

    def expect(
        self,
        title: str,
        command: list,
        succeeds: bool,
        *messages: str,
        count: tuple[str, int] | None = None,
    ):
        """Runs `command`, which must succeed or fail as `succeeds` says and
        print each of `messages`; `count` is a text it must print exactly so
        many times."""
        result = subprocess.run(
            [str(c) for c in command], cwd=ROOT, capture_output=True, text=True, check=False
        )
        output = result.stdout + result.stderr
        problems = []
        if (result.returncode == 0) != succeeds:
            problems.append(f"exited with {result.returncode}")
        problems.extend(f"didn't print {m!r}" for m in messages if m not in output)
        if count and output.count(count[0]) != count[1]:
            problems.append(f"printed {count[0]!r} {output.count(count[0])} times, not {count[1]}")
        self.report(title, problems, output)

    def report(self, title: str, problems: list[str], output: str = ""):
        if problems:
            self.failures += 1
            print(f"FAILED: {title}: {'; '.join(problems)}")
            print("".join(f"  | {line}\n" for line in output.splitlines()[-20:]), end="")
        else:
            print(f"ok: {title}")


def check_comparison(checker: Checker):
    compare = ["python3", "script/compare-texi2any.py", "--hunks", "0", "--expected"]
    none = checker.file("none.txt", "")
    checker.expect(
        "comparison: a difference not listed fails",
        [*compare, none, *COMPARED],
        False,
        f"Unexpected: {TAB}: {TAB_DIFFERENCE}",
        # Each of the ways to compare a file is used, and finds no difference
        # where the files hold no deliberate one.
        "Compared: 2 as texi2any expanded them, 2 as written, 1 as texi2any expanded them "
        "on both sides",
        count=("Unexpected:", 1),
    )
    checker.expect(
        "comparison: a difference in a command's arguments not listed fails",
        [*compare, none, ARGUMENTS],
        False,
        f"Unexpected: {ARGUMENTS}: {ARGUMENTS_DIFFERENCE}",
        count=("Unexpected:", 1),
    )
    unexplained = checker.file("unexplained.txt", f"{TAB}\n  {TAB_DIFFERENCE}\n")
    checker.expect(
        "comparison: a difference listed without a reason fails",
        [*compare, unexplained, TAB],
        False,
        f"No reason given: {TAB}",
    )
    listed = (
        f"# A reason\n{TAB}\n  {TAB_DIFFERENCE}\n\n"
        f"# A reason\n{ARGUMENTS}\n  {ARGUMENTS_DIFFERENCE}\n"
    )
    gone = checker.file(
        "gone.txt", f"{listed}\n# A reason\n{SAME}\n  line 4: ours '@x', texi2any '(nothing)'\n"
    )
    checker.expect(
        "comparison: a listed difference no longer found fails",
        [*compare, gone, TAB, SAME],
        False,
        f"No longer found: {SAME}: line 4",
    )
    checker.expect(
        "comparison: listed differences pass",
        [*compare, checker.file("listed.txt", listed), *COMPARED, ARGUMENTS],
        True,
    )


def check_snapshots(checker: Checker):
    snapshots = checker.directory / "snapshots.txt"
    command = ["python3", "script/tree-snapshots.py", "--snapshots", snapshots]
    checker.expect(
        "snapshots: a missing digest fails",
        [*command, "--check", SAME],
        False,
        f"no digest: {SAME}",
    )
    checker.expect("snapshots: recording", [*command, "--write", SAME], True)
    checker.expect("snapshots: an unchanged tree passes", [*command, "--check", SAME], True)
    digest, name = snapshots.read_text().split("  ", 1)
    snapshots.write_text(f"{'0' * len(digest)}  {name}")
    checker.expect(
        "snapshots: a changed tree fails",
        [*command, "--check", SAME],
        False,
        f"tree changed: {SAME}",
    )
    # A digest of a file no longer checked (not in the input).
    stale_snapshots = checker.directory / "stale.txt"
    stale_file = checker.file("stale.texi", "Text in stale file.\n")
    checker.expect(
        "snapshots: recording stale file",
        [*command, "--snapshots", stale_snapshots, "--write", stale_file],
        True,
    )
    # Checking another file reports the stale file's digest.
    other_file = checker.file("other.texi", "Other text.\n")
    checker.expect(
        "snapshots: a stale digest fails",
        [*command, "--snapshots", stale_snapshots, "--check", other_file],
        False,
        f"no longer checked: {stale_file}",
    )


def check_parse_errors(checker: Checker):
    command = ["python3", "script/parse-examples.py", "--expected"]
    checker.expect(
        "parse errors: a file not listed fails",
        [*command, checker.file("no-errors.txt", ""), PARSE_ERROR, SAME],
        False,
        f"Parse errors: {PARSE_ERROR}",
    )
    checker.expect(
        "parse errors: a file listed without a reason fails",
        [*command, checker.file("unexplained-errors.txt", f"{PARSE_ERROR}\n"), PARSE_ERROR],
        False,
        f"No reason given: {PARSE_ERROR}",
    )
    checker.expect(
        "parse errors: a listed file that parses fails",
        [*command, checker.file("parses.txt", f"# A reason\n{SAME}\n"), SAME],
        False,
        f"No longer fails to parse: {SAME}",
    )
    checker.expect(
        "parse errors: listed files pass",
        [*command, checker.file("errors.txt", f"# A reason\n{PARSE_ERROR}\n"), PARSE_ERROR, SAME],
        True,
    )
    # Of two files, the slower is slower than the median by any factor above 1.
    plain = checker.file("plain.texi", "Plain text in a paragraph.\n\n" * 1000)
    nested = checker.file("nested.texi", "@code{@emph{@var{x}} @strong{y}}\n" * 1000)
    checker.expect(
        "parse speed: a slow file fails",
        [*command, checker.file("none-slow.txt", ""), "--slowdown", "1.0001", plain, nested],
        False,
        "Slow: ",
    )


def check_edits(checker: Checker):
    """A mistake that changes the tree past its paragraph must be found."""
    spec = importlib.util.spec_from_file_location("check_edits", ROOT / "script" / "check-edits.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    parser = Parser(load_language())
    path = ROOT / CHECKS / "edits.texi"
    source = path.read_bytes()
    tree = parser.parse(source)
    first = next(p for p in module.paragraphs(tree.root_node) if p.text.startswith(b"First"))
    local = module.check_edit(parser, path, source, tree, first, first.start_byte + 2, 0, b"@code{")
    checker.report("edits: a local mistake passes", [f"found {p}" for p in local])
    # Ending the list early changes the items after the paragraph.
    spreading = module.check_edit(
        parser, path, source, tree, first, first.start_byte, 0, b"@end itemize\n"
    )
    found = any("nodes changed outside" in p for p in spreading)
    checker.report("edits: a mistake that spreads fails", [] if found else ["found no problem"])


def check_scanner(checker: Checker):
    """Each way the scanner can disagree with the grammar is found, in a copy
    of the scanner changed in that way."""
    scanner = (ROOT / "src" / "scanner.c").read_text()
    command = ["python3", "script/check-scanner.py", "--scanner"]
    checker.expect("scanner: the scanner agrees", [*command, ROOT / "src" / "scanner.c"], True)
    changes = [
        (
            "two tokens swapped",
            ("  BLOCK_START,\n  LIST_START,", "  LIST_START,\n  BLOCK_START,"),
            "LIST_START = 0, but the grammar's external 0 is _block_start",
        ),
        (
            "a family's gap too small",
            ("NODE_BEFORE = DEFINITION_START + 8", "NODE_BEFORE = DEFINITION_START + 7"),
            "NODE_BEFORE = 21, but the grammar's external 21 is _definition_start_7",
        ),
        (
            "a definition block's layout",
            ('{"deffn", KIND_DEFINITION}', '{"deffn", KIND_DEFINITION + 1}'),
            "@deffn has layout 1, but @deffnx is in layout [0]",
        ),
        (
            "a sectioning command's level",
            ('{"chapter", 2}', '{"chapter", 3}'),
            "@chapter has level 3, which isn't its section's",
        ),
        (
            "a name in two tables",
            ('"include",', '"include", "code",'),
            "@code is in both LINE_COMMANDS and BRACE_COMMANDS",
        ),
        (
            "a name ending in index",
            ('{"AA",', '{"AA", "fooindex",'),
            "@fooindex in BRACE_COMMANDS ends in `index`",
        ),
        (
            "an accent that isn't a brace command",
            (re.compile(r'\n *"ogonek",'), ""),
            "the accent @ogonek isn't in BRACE_COMMANDS",
        ),
    ]
    for title, (old, new), message in changes:
        changed = (
            old.sub(new, scanner, 1)
            if isinstance(old, re.Pattern)
            else scanner.replace(old, new, 1)
        )
        if changed == scanner:
            checker.report(f"scanner: {title}", ["the change doesn't apply to src/scanner.c"])
            continue
        path = checker.file("scanner.c", changed)
        checker.expect(f"scanner: {title} fails", [*command, path], False, message)


def check_parser_size(checker: Checker):
    """A parser over its budget fails, and so does one well under it."""
    command = ["python3", "script/parser-size.py", "--budget", "10000", "--parser"]
    for states, succeeds, message in (
        (10000, True, "within its budget of 10000"),
        (10001, False, "The parser has 10001 states, over its budget of 10000"),
        (9000, True, "within its budget of 10000"),
        (8999, False, "lower BUDGET in script/parser-size.py to 9500"),
    ):
        path = checker.file("parser.c", f"#define STATE_COUNT {states}\n")
        outcome = "passes" if succeeds else "fails"
        checker.expect(
            f"parser size: {states} states of 10000 {outcome}", [*command, path], succeeds, message
        )


def main() -> int:
    with tempfile.TemporaryDirectory() as directory:
        checker = Checker(Path(directory))
        check_comparison(checker)
        check_snapshots(checker)
        check_parse_errors(checker)
        check_edits(checker)
        check_scanner(checker)
        check_parser_size(checker)
    if checker.failures:
        print(f"{checker.failures} checks don't work as they should")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
