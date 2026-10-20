#!/usr/bin/env python3
"""Run the tests of the queries `tree-sitter test` can't test: folds,
textobjects and injections, on the files in test/queries/QUERY/, and the
tags tests asserting that a tag is absent, which its tags tests read as
present. test/queries/EDITOR-QUERY/ tests an editor's generated query,
editors/EDITOR/queries/QUERY.scm, for the patterns of its own.

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

Then, in a document with every command the scanner and the grammar know,
each where it may be, and in the highlight and tags tests: that no two
patterns of the highlights capture the same node, nor of the tags tag it,
as editors resolve such overlaps differently (CLAUDE.md), so that the lists
of command names that keep patterns apart (`#any-of?` and `#not-any-of?`)
stay complementary; and that every command name and symbol command is
highlighted, and every definition line tagged. With `--patterns DIR`,
only that, on the highlights and tags in DIR instead of queries/.

Usage: script/test-queries.py [--patterns DIR]
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

from tree_sitter import Parser, Query, QueryCursor

from texinfo_language import ARGUMENT_FIELDS, ROOT, load_language

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


def c_strings(source: str, table: str) -> list[str]:
    """The string literals in the scanner's C array `table`."""
    match = re.search(rf"\b{table}\[\]\s*=\s*\{{(.*?)\n?\}};", source, re.S)
    if match is None:
        raise SystemExit(f"no table {table} in src/scanner.c")
    return re.findall(r'"([^"]*)"', match.group(1))


def every_command() -> str:
    """A document with every command the scanner's tables and the grammar
    name, each where it may be: brace commands in a paragraph, line commands
    on a line of their own, each kind of block with what it holds, and the
    sectioning commands last, before `@bye`."""
    scanner = (ROOT / "src" / "scanner.c").read_text()
    grammar = (ROOT / "grammar.js").read_text()
    brace = c_strings(scanner, "BRACE_COMMANDS")
    parts = ["\\input texinfo\n"]
    parts += [f"Text @{name}{{x}} text.\n\n" for name in brace]
    parts += [f"@{name}{{a, b, c, d, e}}\n\n" for name in ARGUMENT_FIELDS]
    parts.append(
        "Text @verb{|x|} @footnote{x} @math{x} @value{x} @inlinefmt{html, x}\n"
        "@inlinefmtifelse{html, x, y} @inlineraw{html, x} @inlineifset{a, x}\n"
        "@inlineifclear{a, x} @'e @\"u @^a @`a @~n @=o @,{c} @@ @{ @} @* @. @: @u o.\n\n"
    )
    for name in c_strings(scanner, "NO_PARAGRAPH_COMMANDS"):
        parts.append(f"@{name}{{x}}\n\n" if name in brace else f"@{name}\nText.\n\n")
    for table in ("LINE_COMMANDS", "PARAGRAPH_ENDING_LINE_COMMANDS"):
        parts += [f"@{name} x\n\n" for name in c_strings(scanner, table)]
    parts.append(
        "@set x y\n@clear x\n@alias a = b\n@clickstyle @result\n@vskip 1pt\n"
        "@unmacro m\n@majorheading x\n@chapheading x\n@heading x\n@subheading x\n"
        "@subsubheading x\n\n"
    )
    layouts = re.findall(r"\{fields: \[([^\]]*)\], lines: \[([^\]]*)\]", grammar)
    for name, kind in re.findall(r'\{"(\w+)", KIND_(\w+(?: \+ \d+)?)\}', scanner):
        if kind.startswith("DEFINITION"):
            fields, lines = layouts[int(kind.partition(" + ")[2] or 0)]
            words = " ".join(f"w{i}" for i in range(len(re.findall(r"'\w+'", fields))))
            parts.append(f"@{name} {words} (a, b)\n")
            parts += [f"@{line} {words} (c)\n" for line in re.findall(r"'(\w+)'", lines)]
            parts.append(f"Body.\n@end {name}\n\n")
        elif kind == "LIST" and name in ("itemize", "enumerate"):
            parts.append(f"@{name} @bullet\n@item a\n@end {name}\n\n")
        elif kind == "LIST":
            parts.append(f"@{name} @code\n@item a\n@itemx b\nc\n@end {name}\n\n")
        elif kind == "MULTITABLE":
            parts.append(
                f"@{name} @columnfractions .5 .5\n@headitem a @tab b\n@item c @tab d\n"
                f"@end {name}\n\n"
            )
        elif kind == "FLOAT":
            parts.append(f"@{name} Figure, fig\n@caption{{c}}\n@shortcaption{{s}}\n@end {name}\n\n")
        elif kind == "MACRO":
            parts.append(f"@{name} m {{a}}\nx \\a\\\n@end {name}\n\n")
        elif kind == "MENU":
            parts.append(f"@{name}\n* N::  d.\n* L: N.  d.\n@end {name}\n\n")
        elif name == "defblock":
            parts.append(
                f"@{name}\n@defline c n (a)\nBody.\n@deftypeline c t n (a)\nBody.\n@end {name}\n\n"
            )
        else:
            parts.append(f"@{name} x\nText.\n@end {name}\n\n")
    sectioning = re.search(r"\} SECTIONING\[\] = \{(.*?)\n\};", scanner, re.S)
    if sectioning is None:
        raise SystemExit("no table SECTIONING in src/scanner.c")
    for name in re.findall(r'\{"(\w+)", \d\}', sectioning.group(1)):
        parts.append(f"@node {name}\n@nodedescription d\n@{name} Title\n\nText.\n\n")
    parts.append("@bye\n")
    return "".join(parts)


def pattern_lines(source: str, query: Query) -> list[int]:
    """The line of the query's source on which each pattern starts."""
    encoded = source.encode()
    return [
        encoded[: query.start_byte_for_pattern(i)].count(b"\n") + 1
        for i in range(query.pattern_count)
    ]


def overlaps(name: str, source: str, query: Query, root, ignored: set[str]) -> list[str]:
    """The nodes that two patterns of `query` capture, but for the captures
    `ignored` and private ones (`@_name`)."""
    lines = pattern_lines(source, query)
    patterns: dict[int, set[int]] = defaultdict(set)
    nodes = {}
    for pattern, match in QueryCursor(query).matches(root):
        for capture, captured in match.items():
            if capture.startswith("_") or capture in ignored:
                continue
            for node in captured:
                patterns[node.id].add(pattern)
                nodes[node.id] = node
    problems = []
    for node_id, found in patterns.items():
        if len(found) > 1:
            node = nodes[node_id]
            where = ", ".join(f"{name}:{lines[p]}" for p in sorted(found))
            problems.append(
                f"line {node.start_point.row + 1}: {node.type} {node.text.decode()[:40]!r} "
                f"captured by the patterns at {where}"
            )
    return problems


def uncaptured(query: Query, root, types: set[str], prefix: str = "") -> list[str]:
    """The nodes of `types` that no capture whose name starts with `prefix`
    covers."""
    captured = {
        node.id
        for _, match in QueryCursor(query).matches(root)
        for capture, nodes in match.items()
        if not capture.startswith("_") and capture.startswith(prefix)
        for node in nodes
    }
    problems = []
    stack = [root]
    while stack:
        node = stack.pop()
        if node.type in types and node.id not in captured:
            problems.append(
                f"line {node.start_point.row + 1}: {node.type} {node.text.decode()[:40]!r} "
                f"has no {prefix or 'capture'}"
            )
        stack.extend(node.children)
    return problems


def check_patterns(language, parser: Parser, queries_directory: Path) -> int:
    """Checks that the patterns of the highlights and the tags in
    `queries_directory` don't overlap and leave nothing out, in a document
    with every command and in the tests."""
    documents = [("every command", every_command().encode())]
    for directory in ("highlight", "tags"):
        for path in sorted((ROOT / "test" / directory).glob("*.texi")):
            documents.append((str(path.relative_to(ROOT)), path.read_bytes()))
    queries = {}
    for name in ("highlights", "tags"):
        source = (queries_directory / f"{name}.scm").read_text()
        queries[name] = (source, Query(language, as_regex(source)))
    failures = 0
    for title, document in documents:
        root = parser.parse(document).root_node
        problems = []
        if title == "every command" and root.has_error:
            problems.append("the document of every command has a parse error")
        source, query = queries["highlights"]
        problems += overlaps("highlights.scm", source, query, root, set())
        problems += uncaptured(query, root, {"command_name", "symbol_command"})
        source, query = queries["tags"]
        problems += overlaps("tags.scm", source, query, root, {"name"})
        problems += uncaptured(query, root, {"definition_line"}, "definition.")
        for problem in problems:
            print(f"{title}: {problem}")
        failures += len(problems)
        print(f"{'ok' if not problems else 'FAILED'}: patterns apart in {title}")
    return failures


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.partition("\n")[0])
    arguments.add_argument("--patterns", type=Path, metavar="DIR")
    options = arguments.parse_args()
    language = load_language()
    parser = Parser(language)
    if options.patterns is not None:
        return 1 if check_patterns(language, parser, options.patterns) else 0
    failures = check_patterns(language, parser, ROOT / "queries")
    directories = sorted(d for d in TESTS.iterdir() if d.is_dir())
    if not directories:
        raise SystemExit(f"no tests in {TESTS}")
    for directory in directories:
        editor, _, name = directory.name.rpartition("-")
        queries = ROOT / "editors" / editor / "queries" if editor else ROOT / "queries"
        query = Query(language, as_regex((queries / f"{name}.scm").read_text()))
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
