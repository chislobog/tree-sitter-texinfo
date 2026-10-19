#!/usr/bin/env python3
"""Run the tests of the queries `tree-sitter test` can't test: folds,
textobjects and injections, on the files in test/queries/QUERY/, and the
tags tests asserting that a tag is absent, which its tags tests read as
present.

`tree-sitter test` would run the tests in test/QUERY/, but tree-sitter
0.26.11 reads each such test file as the query itself, so the tests live
apart until it doesn't.

The assertions are tree-sitter's: in a comment, `@c ^ NAME` checks that a
capture NAME covers that column of the last line above that isn't an
assertion, `@c <- NAME` the column of the comment, and `!NAME` that none
does. `N lines up` after the name checks the line N lines above the
assertion instead, for a raw block's body, where no comment can follow a
line. For injections, `injection.LANGUAGE` also names the content of an
injection in LANGUAGE (set by the query or captured as
`@injection.language`). A line that looks like an assertion but isn't a
comment, as in a raw block, fails, rather than being skipped, and so does a
file without assertions.

Usage: script/test-queries.py
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

from texinfo_language import ROOT, load_language

from tree_sitter import Parser, Query, QueryCursor

TESTS = ROOT / "test" / "queries"
# A `#not-any-of?` predicate: py-tree-sitter 0.25.2 lets through a capture
# that is one of its values but the first, so it runs them as the regex of a
# `#not-match?`, which it gets right.
NOT_ANY_OF = re.compile(r'\(#not-any-of\? (@[\w.]+)((?:\s+"(?:[^"\\]|\\.)*")+)\)')
ASSERTION = re.compile(r"^(\s*)@c(?:omment)?(\s+)(\^+|<-)\s*(!?)(\S+)(?:\s+(\d+) lines? up)?\s*$")


def captures(query: Query, root) -> dict[str, list[tuple]]:
    """The ranges of each capture name, and of `injection.LANGUAGE` for each
    injection's content."""
    found: dict[str, list[tuple]] = defaultdict(list)
    for pattern, match in QueryCursor(query).matches(root):
        for name, nodes in match.items():
            for node in nodes:
                found[name].append((node.start_point, node.end_point))
        language = query.pattern_settings(pattern).get("injection.language")
        if language is None and "injection.language" in match:
            language = match["injection.language"][0].text.decode()
        for node in match.get("injection.content", []):
            found[f"injection.{language}"].append((node.start_point, node.end_point))
    return found


def comment_rows(root) -> set[int]:
    """The rows on which a comment starts."""
    rows = set()
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type == "comment":
            rows.add(node.start_point.row)
        stack.extend(node.children)
    return rows


def as_regex(text: str) -> str:
    """The query `text`, its `#not-any-of?` predicates written as
    `#not-match?` ones."""

    def regex(match: re.Match) -> str:
        # Each value as written in the query, its escapes undone.
        values = [
            re.sub(r"\\(.)", r"\1", value)
            for value in re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(2))
        ]
        pattern = "^(?:" + "|".join(re.escape(value) for value in values) + ")$"
        literal = pattern.replace("\\", "\\\\").replace('"', '\\"')
        return f'(#not-match? {match.group(1)} "{literal}")'

    return NOT_ANY_OF.sub(regex, text)


def check(parser: Parser, query: Query, file: Path) -> tuple[int, list[str]]:
    source = file.read_bytes()
    path = file.relative_to(ROOT)
    tree = parser.parse(source)
    found = captures(query, tree.root_node)
    comments = comment_rows(tree.root_node)
    problems = []
    count = 0
    target = None
    for row, line in enumerate(source.decode().splitlines()):
        assertion = ASSERTION.match(line)
        if assertion is None:
            target = row
            continue
        if row not in comments:
            problems.append(f"{path}:{row + 1}: an assertion that isn't a comment, never checked")
            continue
        indent, space, arrow, negated, name, up = assertion.groups()
        if up is not None:
            target = row - int(up)
        if target is None or target < 0:
            problems.append(f"{path}:{row + 1}: an assertion before any line to check")
            continue
        if arrow == "<-":
            columns = [len(indent)]
        else:
            start = len(indent) + 2 + len(space)
            columns = range(start, start + len(arrow))
        for column in columns:
            count += 1
            covered = any(
                (start.row, start.column) <= (target, column) < (end.row, end.column)
                for start, end in found.get(name, [])
            )
            if covered == bool(negated):
                expected = f"no {name}" if negated else name
                problems.append(f"{path}:{target + 1}:{column}: expected {expected}")
    if count == 0:
        problems.append(f"{path}: no assertions")
    return count, problems


def main() -> int:
    language = load_language()
    parser = Parser(language)
    failures = 0
    directories = sorted(d for d in TESTS.iterdir() if d.is_dir())
    if not directories:
        raise SystemExit(f"no tests in {TESTS}")
    for directory in directories:
        query = Query(language, as_regex((ROOT / "queries" / f"{directory.name}.scm").read_text()))
        paths = sorted(directory.glob("*.texi"))
        if not paths:
            print(f"FAILED: {directory.name}: no tests")
            failures += 1
        for path in paths:
            count, problems = check(parser, query, path)
            for problem in problems:
                print(problem)
            failures += len(problems)
            mark = "ok" if not problems else "FAILED"
            print(f"{mark}: {directory.name}/{path.name} ({count} assertions)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
