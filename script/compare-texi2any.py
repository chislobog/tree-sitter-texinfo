#!/usr/bin/env python3
r"""Compare this grammar's parse trees with texi2any's, the reference parser.

For each file, both trees are flattened into the sequence of @-commands they
contain, in document order: `+name` and `-name` for the start and end of a
block, `@name` for every other command. The two sequences are then diffed.

texi2any expands macros, line macros, `@value` and aliases, and evaluates
conditionals, before or while it parses; a tree-sitter grammar keeps them as
written. Rather than imitate that, this script parses texi2any's own
expansion of the file (`--macro-expand`, written from texi2any's tree in the
same run that dumps the tree), so both sides see the same text. That text
also lacks the commands texi2any doesn't know (TeX primitives, index
commands defined in another file of a manual), which its tree drops.

The expansion stands for the file only where texi2any finds in it what it
found in the file: the same commands, the same messages apart from those
about unknown commands, and no unknown commands left. Written from the
tree, the expansion repairs some errors (a missing `}`, an `@end` that ends
nothing) and loses some text (a brace after a line command), so for invalid
input it is often not the same document. Then:

- If the file uses nothing texi2any expands, it is compared as written, so
  that recovery from its errors is compared.
- Otherwise both sides parse the expansion: texi2any's tree is the one it
  makes of its expansion. Its `@alias` lines are kept, so this tree has
  them applied twice.

The summary counts the files compared each way. In the last two, the
commands texi2any reports it doesn't know in the text compared are left out
on our side. Line numbers in the differences are those of the text
compared. The parse errors are always counted in the file as written.

What is left to mirror on this side:

- Inline conditionals, which texi2any's expansion keeps: only the arguments
  it expands (`@inlinefmtifelse`'s else, `@inlineifset`'s text when the flag
  is set, ...) contribute commands. `txicommandconditionals` is set, as
  texi2any predefines it.
- `@` at the end of a definition line (`@deffn`, `@defunx`, ...) continues
  the line rather than being a command.
- The `\input texinfo` line is preamble text to texi2any, so a comment on
  it is not a command.
- texi2any runs on a lone copy of each file, so `@include` is not followed.

On our side, a command name inside an ERROR node still counts as a command:
error recovery keeps it, and that is what an editor highlights.

The differences that are expected, because they are deliberate or come from
the limits of this comparison, are listed with their reasons in
test/texi2any-differences.txt; `--expected` fails on any other difference,
and on a listed one that is no longer found, and `--write-expected` updates
the list.

Usage: script/compare-texi2any.py [--structure] [--hunks N]
       [--expected FILE] [--write-expected FILE] [--manuals] [--test-inputs]
       [--compared-manuals] [--edited-manuals] [FILE...]
"""

import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from texinfo_language import BLOCK_NAMES, add_files_argument, files_argument, load_language

from tree_sitter import Parser

HEADINGS = (
    "part_heading",
    "top_heading",
    "chapter_heading",
    "section_heading",
    "subsection_heading",
    "subsubsection_heading",
    "heading",
)
INLINE_CONDITIONALS = (
    "@inlinefmt",
    "@inlineraw",
    "@inlinefmtifelse",
    "@inlineifset",
    "@inlineifclear",
)


@dataclass
class Token:
    name: str
    # Line in the text compared; texi2any's dump doesn't record positions.
    line: int | None = None
    # Block markers, items and line commands; the rest are inline.
    structural: bool = False


# Our tree ---------------------------------------------------------------


def text(node) -> str:
    return node.text.decode("utf-8", "replace")


def first_word(node) -> str:
    words = text(node).split() if node is not None else []
    return words[0] if words else ""


class OurWalker:
    def __init__(self):
        self.tokens: list[Token] = []
        # The flags set, for `@inlineifset` and `@inlineifclear`.
        self.flags: set[str] = {"txicommandconditionals"}
        self.in_definition_line = False

    def emit(self, name: str, node, structural: bool):
        self.tokens.append(Token(name, node.start_point.row + 1, structural))

    def children(self, node, *skip):
        """Walks the children of `node`, except the `skip` nodes."""
        skipped = {n.id for n in skip if n is not None}
        for child in node.children:
            if child.id not in skipped:
                self.walk(child)

    def walk_expanded_arguments(self, name: str, node):
        """Walks only the arguments of an inline conditional that texi2any
        expands, given by position after the condition: `text` (or
        @inlinefmtifelse's `then`) is 1, its `else` is 2."""
        flag = first_word(node.child_by_field_name("condition"))
        expanded = {
            "@inlinefmt": set(),
            "@inlineraw": set(),
            "@inlinefmtifelse": {2},
            "@inlineifset": {1} if flag in self.flags else set(),
            "@inlineifclear": set() if flag in self.flags else {1},
        }[name]
        fields = {1: ("text", "then"), 2: ("else",)}
        for position in sorted(expanded):
            for field in fields[position]:
                argument = node.child_by_field_name(field)
                if argument is not None:
                    self.walk(argument)

    def walk_definition_line(self, node, *skip):
        """Walks the children of a definition line (or of its argument),
        except the `skip` nodes."""
        previous = self.in_definition_line
        self.in_definition_line = True
        self.children(node, *skip)
        self.in_definition_line = previous

    def walk(self, node):
        kind = node.type

        if kind == "definition_block":
            line = node.children[0]
            name_node = line.child_by_field_name("name")
            end = node.child_by_field_name("end")
            self.emit("+" + text(name_node)[1:], node, True)
            self.walk_definition_line(line, name_node)
            self.children(node, line, end)
            if end.end_byte > end.start_byte:
                self.emit("-" + text(end).split()[-1], end, True)
            return

        if kind in ("block", "menu", "list", "raw_block", "macro_definition"):
            name_node = node.child_by_field_name("name")
            argument = node.child_by_field_name("argument")
            end = node.child_by_field_name("end")
            name = text(name_node)[1:]
            self.emit("+" + name, node, True)
            if argument is not None and name.startswith("def"):
                self.walk_definition_line(argument)
                self.children(node, name_node, argument, end)
            else:
                self.children(node, name_node, end)
            if end.end_byte > end.start_byte:
                self.emit("-" + text(end).split()[-1], end, True)
            return

        if kind == "definition_line":
            name_node = node.child_by_field_name("name")
            self.emit(text(name_node), node, True)
            self.walk_definition_line(node, name_node)
            return

        if kind in ("line_command", "node_line", "item", "itemx", "subentry", "bye") or (
            kind in HEADINGS
        ):
            name_node = node.child_by_field_name("name")
            name = text(name_node)
            self.emit(name, node, True)
            if name in ("@set", "@clear"):
                variable = node.child_by_field_name("variable")
                if name == "@set":
                    self.flags.add(first_word(variable))
                else:
                    self.flags.discard(first_word(variable))
                # Comments; the value is raw text.
                self.children(node, name_node, variable)
            elif name.startswith("@def"):
                self.walk_definition_line(node, name_node)
            else:
                # Includes a comment on the line, also after `@bye`.
                self.children(node, name_node)
            return

        if kind == "inline_command":
            name_node = node.child_by_field_name("name")
            name = text(name_node)
            # texi2any has `@columnfractions` as a line command inside the
            # multitable's argument.
            self.emit(name, node, name == "@columnfractions")
            if name in INLINE_CONDITIONALS:
                self.walk_expanded_arguments(name, node)
            else:
                self.children(node, name_node)
            return

        if kind == "symbol_command":
            # A lone `@` is at the end of a line (or of the file): texi2any's
            # end-of-line command, which keeps the newline in its name.
            name = "@\n" if text(node) == "@" else text(node)
            if not (self.in_definition_line and name == "@\n"):
                self.emit(name, node, False)
            return

        if kind == "comment":
            # `@c` or `@comment`, which anything but a name's characters
            # may follow.
            self.emit(re.match(r"@c(?:omment)?", text(node)).group(), node, True)
            return

        if kind == "input_line":
            return

        # Only reached inside an ERROR node, where tree-sitter still
        # recognised the command (e.g. `@code{` unclosed at the end of a file).
        if kind == "command_name":
            self.emit(text(node), node, False)
            return

        self.children(node)


def our_tokens(parser: Parser, source: bytes) -> list[Token]:
    walker = OurWalker()
    walker.walk(parser.parse(source).root_node)
    return walker.tokens


def parse_errors(parser: Parser, source: bytes) -> int:
    """The ERROR and MISSING nodes in our tree of `source`."""
    count = 0
    cursor = parser.parse(source).walk()
    while True:
        node = cursor.node
        count += node.is_error or node.is_missing
        if node.has_error and cursor.goto_first_child():
            continue
        while not cursor.goto_next_sibling():
            if not cursor.goto_parent():
                return count


# texi2any's tree -------------------------------------------------------


@dataclass
class DumpNode:
    label: str
    children: list["DumpNode"]


def read_dump(text: str) -> DumpNode:
    """Parses `-c TEXINFO_OUTPUT_FORMAT=debugtree` output, which indents each
    node one space deeper than its parent."""
    root = DumpNode("", [])
    stack: list[tuple[int, DumpNode]] = [(-1, root)]
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        i += 1
        stripped = line.lstrip(" ")
        if not stripped:
            continue
        depth = len(line) - len(stripped)
        if stripped == "@":
            # `@` at the end of a line: the command name is a newline, so the
            # rest of the node is on the next line.
            stripped = "@\n"
            i += 1
        node = DumpNode(stripped, [])
        while stack[-1][0] >= depth:
            stack.pop()
        stack[-1][1].children.append(node)
        stack.append((depth, node))
    _move_end_comments(root)
    return root


def _move_end_comments(node: DumpNode):
    """texi2any prints a comment at the end of a line (`/comment_at_end/`)
    before the rest of the line; moves it after, where it is in the source."""
    node.children.sort(key=lambda child: child.label.startswith("/comment_at_end/"))
    for child in node.children:
        _move_end_comments(child)


# A command name as texi2any reads it, such as `@foo-barindex` after
# `@defindex foo-bar`.
COMMAND_NAME = re.compile(r"@[A-Za-z0-9][A-Za-z0-9_-]*")


def command_name(label: str) -> str | None:
    if not label.startswith("@"):
        return None
    name = COMMAND_NAME.match(label)
    if name:
        return name.group()
    # A single other character, including the newline of `@` at the end of a
    # line.
    return label[:2]


def dump_text(node: DumpNode) -> str:
    for child in node.children:
        if child.label.startswith("|"):
            return child.label.strip("|")
        found = dump_text(child)
        if found:
            return found
    return ""


def run_texi2any(path: Path, source: bytes) -> tuple[str, str, bytes | None]:
    """Runs texi2any on `source`, as the file `path`: returns its tree as a
    debug dump, its messages, and its expansion of the file, or None if it
    wrote none."""
    with tempfile.TemporaryDirectory() as directory:
        (Path(directory) / path.name).write_bytes(source)
        expansion = Path(directory) / "expanded.texi"
        result = subprocess.run(
            [
                "texi2any",
                "--force",
                # The default limit of 100 errors stops texi2any before it
                # prints anything, as in a chapter file whose references go to
                # other chapters.
                "--error-limit=1000000",
                "-c",
                "TEXINFO_OUTPUT_FORMAT=debugtree",
                f"--macro-expand={expansion}",
                "-o",
                "-",
                path.name,
            ],
            cwd=directory,
            # texi2any's parser written in C, which it uses where it was
            # built, treats invalid input such as Latin-1 bytes differently
            # from its Perl one: always use the Perl one, so the results are
            # the same on every platform.
            env={**os.environ, "TEXINFO_XS": "omit"},
            capture_output=True,
            # texi2any exits with an error on invalid input, but still
            # prints its tree, which is what is compared.
            check=False,
        )
        expanded = expansion.read_bytes() if expansion.exists() else None
    return (
        result.stdout.decode("utf-8", "replace"),
        result.stderr.decode("utf-8", "replace"),
        expanded,
    )


# A message, `FILE:LINE: TEXT` or `FILE: TEXT`, with what texi2any adds to
# one in a macro expansion.
MESSAGE = re.compile(r"^.*?:(?:\d+:)? (.*?)(?: \(possibly involving @[^)]*\))?$")
UNKNOWN = ("unknown command", "unexpected @")
MISPLACED = ("misplaced {", "misplaced }")


def messages_without_unknown(messages: str) -> Counter[str]:
    """texi2any's messages, without where they are (lines differ between a
    file and its expansion), and without those about unknown commands, which
    its expansion drops: their own, and if there are any, those about
    misplaced braces, as the braces of `@name{...}` are, on its line or the
    next ones."""
    texts = [m.group(1) for m in map(MESSAGE.match, messages.splitlines()) if m]
    unknown = any(t.startswith(UNKNOWN) for t in texts)
    return Counter(
        t for t in texts if not t.startswith(UNKNOWN) and not (unknown and t in MISPLACED)
    )


# texi2any's error for a command it doesn't know, or for `@` followed by a
# character that makes no command.
UNKNOWN_COMMAND = re.compile(r":(\d+): (?:unknown command `([^']+)'|(unexpected) @)")
# A C preprocessor line directive, which texi2any follows when it reports
# lines: `#line 10 "file"`, `# 10 "file" 2`. Its own pattern, so that a line
# such as `#  80  80` in an example isn't taken for one.
CPP_LINE = re.compile(rb'^\s*#\s*(?:line)? (\d+)(?: "[^"]+"(?:\s+\d+)*)?\s*$')


def unknown_commands(messages: str, source: bytes) -> set[tuple[int, str]]:
    """The commands texi2any reports it doesn't know in `source`, as (line,
    name) pairs; `unexpected` stands for a symbol."""
    reported: list[int] = []
    line = 1
    for text_line in source.split(b"\n"):
        reported.append(line)
        directive = CPP_LINE.match(text_line)
        # texi2any numbers the line after `#line N` as N + 1.
        line = int(directive.group(1)) + 1 if directive else line + 1
    unknown: set[tuple[int, str]] = set()
    for number, name, unexpected in UNKNOWN_COMMAND.findall(messages):
        lines = [i + 1 for i, n in enumerate(reported) if n == int(number)]
        unknown.update((line, name or unexpected) for line in lines or [int(number)])
    return unknown


def is_unknown(token: Token, unknown: set[tuple[int, str]]) -> bool:
    name = token.name[1:]
    symbol = len(name) == 1 and not name.isalpha()
    return (token.line, name) in unknown or (symbol and (token.line, "unexpected") in unknown)


# What texi2any expands or evaluates in a file.
EXPANDED = re.compile(rb"@(?:r?macro|linemacro|alias|clickstyle|value|if[a-z]+)\b")


@dataclass
class Side:
    """texi2any's commands for a file, and the text to parse on our side."""

    tokens: list[Token]
    text: bytes
    # "expanded", "as written" or "both expanded".
    how: str
    # The commands texi2any doesn't know in `text`, to leave out on our side.
    unknown: set[tuple[int, str]]


def texi2any_side(path: Path, line_commands: set[str]) -> Side:
    source = path.read_bytes()
    dump, messages, expanded = run_texi2any(path, source)
    tokens = texi2any_tokens(dump, line_commands)
    if expanded is None or (expanded != source and not EXPANDED.search(source)):
        # Nothing to expand: the expansion would only repair errors.
        return Side(tokens, source, "as written", unknown_commands(messages, source))
    if expanded == source:
        return Side(tokens, expanded, "expanded", set())
    again, again_messages, _ = run_texi2any(path, expanded)
    again_tokens = texi2any_tokens(again, line_commands)
    unknown = unknown_commands(again_messages, expanded)
    if (
        not unknown
        and [t.name for t in again_tokens] == [t.name for t in tokens]
        and messages_without_unknown(again_messages) == messages_without_unknown(messages)
    ):
        return Side(tokens, expanded, "expanded", set())
    return Side(again_tokens, expanded, "both expanded", unknown)


def texi2any_line_commands() -> set[str]:
    """texi2any's line commands, from its own command table, with the items
    of lists and tables: what `--structure` keeps of its tree, whatever this
    grammar makes of them."""
    commands = Path(shutil.which("texi2any") or "texi2any").resolve().parent.parent
    table = (commands / "share" / "texi2any" / "Texinfo" / "Commands.pm").read_text()
    line_commands = re.search(r"our %line_commands = \((.*?)\n\);", table, re.S)
    if line_commands is None:
        raise SystemExit("texi2any's Commands.pm has no %line_commands table")
    return set(re.findall(r'"([^"]+)"\s*=>', line_commands.group(1))) | {"item", "headitem"}


def texi2any_tokens(dump: str, line_commands: set[str]) -> list[Token]:
    tokens: list[Token] = []

    def walk(node: DumpNode):
        name = command_name(node.label)
        if name is None or name == "@end":
            for child in node.children:
                walk(child)
            return
        children = node.children
        closed = bool(children) and command_name(children[-1].label) == "@end"
        if closed or name[1:] in BLOCK_NAMES:
            # An unclosed block has no `@end` child.
            tokens.append(Token("+" + name[1:], structural=True))
            for child in children[:-1] if closed else children:
                walk(child)
            if closed:
                end_name = re.match(r"\s*([\w-]*)", dump_text(children[-1])).group(1)
                tokens.append(Token("-" + end_name, structural=True))
                # Comments on the `@end` line.
                for child in children[-1].children:
                    walk(child)
            return
        # User-defined index commands (`@defindex op` makes `@opindex`).
        structural = name[1:] in line_commands or "index_entry_command" in node.label
        tokens.append(Token(name, structural=structural))
        for child in children:
            walk(child)

    walk(read_dump(dump))
    return tokens


# Comparison ------------------------------------------------------------


def differences(ours: list[Token], theirs: list[Token]) -> tuple[list[str], int]:
    """The differences between the two command sequences, one line each, and
    the number of commands that match."""
    matcher = difflib.SequenceMatcher(
        a=[t.name for t in ours],
        b=[t.name for t in theirs],
        autojunk=False,
    )
    found = []
    matched = 0
    for tag, a1, a2, b1, b2 in matcher.get_opcodes():
        if tag == "equal":
            matched += a2 - a1
            continue
        line = ours[a1].line if a1 < len(ours) else ours[-1].line if ours else "?"
        mine = " ".join(t.name for t in ours[a1:a2]) or "(nothing)"
        reference = " ".join(t.name for t in theirs[b1:b2]) or "(nothing)"
        found.append(f"line {line}: ours {mine!r}, texi2any {reference!r}")
    return found, matched


@dataclass
class Expected:
    """A file's expected differences, with the comment that explains them."""

    comment: list[str]
    differences: list[str]


def read_expected(path: Path) -> dict[str, Expected]:
    """Reads the expected differences: comment lines (`#`), then a file's path,
    then its differences, each indented by two spaces."""
    entries: dict[str, Expected] = {}
    comment: list[str] = []
    current = None
    if not path.exists():
        return entries
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            comment.append(line)
        elif line.startswith("  ") and current is not None:
            current.differences.append(line[2:])
        elif line.strip():
            current = entries[line] = Expected(comment, [])
            comment = []
    return entries


def write_expected(path: Path, entries: dict[str, Expected]):
    lines = []
    for name in sorted(entries):
        entry = entries[name]
        if lines and entry.comment:
            lines.append("")
        lines.extend(entry.comment)
        lines.append(name)
        lines.extend("  " + d for d in entry.differences)
    path.write_text("\n".join(lines) + "\n")


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    add_files_argument(arguments)
    arguments.add_argument(
        "--structure", action="store_true", help="compare only blocks, items and line commands"
    )
    arguments.add_argument(
        "--hunks", type=int, default=5, help="differences to show per file (default 5)"
    )
    arguments.add_argument(
        "--expected",
        type=Path,
        metavar="FILE",
        help="exit with an error on any difference not listed in FILE, or "
        "listed for a compared file but not found",
    )
    arguments.add_argument(
        "--write-expected",
        type=Path,
        metavar="FILE",
        help="record the differences of the compared files in FILE, keeping "
        "its comments and its other files",
    )
    options = arguments.parse_args()
    options.files = files_argument(options)

    parser = Parser(load_language())
    line_commands = texi2any_line_commands()
    total_ours = total_matched = total_differences = 0
    found: dict[str, list[str]] = {}
    how_compared: Counter[str] = Counter()
    # texi2any is the slow part: run it for all files in parallel, and report
    # in file order.
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        sides = [pool.submit(texi2any_side, path, line_commands) for path in options.files]
        for path, side in zip(options.files, sides, strict=True):
            texi2any = side.result()
            theirs = texi2any.tokens
            how_compared[texi2any.how] += 1
            ours = [
                t for t in our_tokens(parser, texi2any.text) if not is_unknown(t, texi2any.unknown)
            ]
            if options.structure:
                ours = [t for t in ours if t.structural]
                theirs = [t for t in theirs if t.structural]
            file_differences, matched = differences(ours, theirs)
            errors = parse_errors(parser, path.read_bytes())
            if errors:
                file_differences.append(f"{errors} parse errors")
            found[str(path)] = file_differences
            print(
                f"{path}: {len(file_differences)} differences, {matched} of "
                f"{len(ours)} vs {len(theirs)} commands match"
            )
            for difference in file_differences[: options.hunks]:
                print(f"  {difference}")
            if len(file_differences) > options.hunks:
                print(f"  ... {len(file_differences) - options.hunks} more")
            total_ours += len(ours)
            total_matched += matched
            total_differences += len(file_differences)
    if len(options.files) > 1:
        differing = sum(1 for d in found.values() if d)
        print(
            f"\nOverall: {total_differences} differences in {differing} of "
            f"{len(found)} files; {total_matched} of {total_ours} commands match"
        )
        print(
            f"Compared: {how_compared['expanded']} as texi2any expanded them, "
            f"{how_compared['as written']} as written, {how_compared['both expanded']} "
            "as texi2any expanded them on both sides (its expansion not being the same)"
        )

    if options.write_expected:
        entries = read_expected(options.write_expected)
        for name, file_differences in found.items():
            if file_differences:
                comment = entries[name].comment if name in entries else []
                entries[name] = Expected(comment, file_differences)
            else:
                entries.pop(name, None)
        write_expected(options.write_expected, entries)

    if options.expected:
        entries = read_expected(options.expected)
        failed = False
        for name in found:
            if name in entries and not entries[name].comment:
                print(f"No reason given: {name}", file=sys.stderr)
                failed = True
        for name, file_differences in found.items():
            expected = entries[name].differences if name in entries else []
            unexpected = Counter(file_differences) - Counter(expected)
            gone = Counter(expected) - Counter(file_differences)
            for difference in unexpected.elements():
                print(f"Unexpected: {name}: {difference}", file=sys.stderr)
            for difference in gone.elements():
                print(f"No longer found: {name}: {difference}", file=sys.stderr)
            failed = failed or bool(unexpected or gone)
        if failed:
            print(
                f"Differences from texi2any don't match {options.expected}: fix "
                "the regression, or if the change is deliberate, run "
                "`just compare-write` and give each new file a `#` comment with "
                "the reason.",
                file=sys.stderr,
            )
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
