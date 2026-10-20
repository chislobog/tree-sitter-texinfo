#!/usr/bin/env python3
"""Check error recovery and incremental parsing by simulating typing mistakes.

For each file, edits are made at random places inside paragraphs, as while
typing: a character deleted (but not a newline before a line that starts with
a command), or `{`, `}`, `@`, `@code{` or `@xref{` inserted; not an edit
that makes a line start with a block command, or a brace be followed by one
(`@code{@@html` becoming `@code{@html`), which opens a block, as in texi2any.
Not inside a
footnote, a caption, `@math` or an inline conditional, whose argument spans
paragraphs, so that in texi2any too a brace left open there runs to its end,
nor inside `@verb`, which, its delimiter gone, takes the rest of the file, as
in texi2any; nor inside a block in
a paragraph (a raw block, a macro definition or a conditional), whose `@end`
an edit may break, which leaves it open to an `@end` further on.
For each edit:

- Incremental parsing: the old tree, edited as an editor does, parsed again
  must equal a fresh parse of the edited text.
- Locality: the mistake may only change the tree from the start of its
  paragraph to the next blank line (where an unclosed brace closes), or to
  the end of its preformatted block, where a blank line doesn't close it, or
  past a conditional starting in between, inside which a brace can't close;
  every node before or after that, with its ancestors, must stay as it was.

The edits are the same on every run for a given --seed.

With --keep DIR, the edited text of each problem is saved there, with the
original, to inspect with `tree-sitter parse --cst` or `--debug-graph`.

Usage: script/check-edits.py [--manuals] [--test-inputs] [--compared-manuals]
       [--edited-manuals] [--edits N] [--seed N] [--keep DIR] [FILE...]
"""

import argparse
import random
import re
import sys
from collections import Counter
from pathlib import Path

from tree_sitter import Node, Parser, Point, Tree

from texinfo_language import BLOCK_NAMES, add_files_argument, dump, files_argument, load_language

INSERTIONS = ["{", "}", "@", "@code{", "@xref{"]
# Blocks in which, as in texi2any, a blank line doesn't end a brace command,
# so a mistake may reach the end of the block.
PREFORMATTED = {
    "@example",
    "@smallexample",
    "@lisp",
    "@smalllisp",
    "@display",
    "@smalldisplay",
    "@format",
    "@smallformat",
}
# A block command right after a brace, which opens the block there.
BRACE_BLOCK = re.compile(rb"\{[ \t]*@([a-zA-Z][\w-]*)")


def opens_block_after_brace(source: bytes, edited: bytes, offset: int) -> bool:
    """Whether the edit at `offset` makes a block command follow a brace."""

    def count(text: bytes) -> int:
        window = text[max(0, offset - 64) : offset + 64]
        return sum(m.group(1).decode() in BLOCK_NAMES for m in BRACE_BLOCK.finditer(window))

    return count(edited) > count(source)


# A blank line, after which a paragraph has ended.
BLANK_LINE = re.compile(rb"\n[ \t]*\n")


def point(source: bytes, offset: int) -> Point:
    row = source.count(b"\n", 0, offset)
    return Point(row, offset - (source.rfind(b"\n", 0, offset) + 1))


def paragraphs(node: Node) -> list[Node]:
    found = []
    stack = [node]
    while stack:
        current = stack.pop()
        if current.type in ("paragraph", "preformatted"):
            found.append(current)
        else:
            stack.extend(current.children)
    return found


# Commands whose argument spans paragraphs, as in texi2any, so a brace left
# open in it runs to its end, and `@verb`, which left open takes the rest of
# the file: not checked.
SPANNING = {
    b"footnote",
    b"caption",
    b"shortcaption",
    b"math",
    b"inlinefmt",
    b"inlinefmtifelse",
    b"inlineifset",
    b"inlineifclear",
    b"inlineraw",
    b"verb",
}
LINE_COMMAND = re.compile(rb"[ \t]*@([a-zA-Z][\w-]*)")


def inside_spanning(root: Node, offset: int, names: set[bytes]) -> bool:
    node = root.descendant_for_byte_range(offset, offset)
    while node is not None:
        name = node.child_by_field_name("name") if node.type == "inline_command" else None
        if name is not None and name.text[1:] in names:
            return True
        node = node.parent
    return False


# Blocks that a paragraph may hold.
PARAGRAPH_BLOCKS = {"block", "raw_block", "macro_definition"}


def inside_block(paragraph: Node, offset: int) -> bool:
    """Whether `offset` is inside a block that `paragraph` holds."""
    node = paragraph.descendant_for_byte_range(offset, offset)
    while node is not None and node.id != paragraph.id:
        if node.type in PARAGRAPH_BLOCKS:
            return True
        node = node.parent
    return False


def conditionals(root: Node, start: int, end: int) -> list[Node]:
    """The conditional and raw blocks other than `@verbatim` that start
    between `start` and `end`: a brace command left open before them keeps
    them inside, as in texi2any, and blank lines in them don't close it."""
    found = []
    stack = [root]
    while stack:
        node = stack.pop()
        if node.end_byte <= start or node.start_byte >= end:
            continue
        name = node.child_by_field_name("name")
        conditional = node.type == "block" and name is not None and name.text.startswith(b"@if")
        raw = node.type == "raw_block" and name is not None and name.text != b"@verbatim"
        if (conditional or raw) and node.start_byte >= start:
            found.append(node)
        stack.extend(node.children)
    return found


def preformatted_block(node: Node) -> Node | None:
    while node.parent is not None:
        node = node.parent
        name = node.child_by_field_name("name")
        if node.type == "block" and name is not None and name.text.decode() in PREFORMATTED:
            return node
    return None


def outside(node: Node, start: int, end: int, shift: int) -> Counter:
    """The nodes that end before `start` or start after `end`, with their
    field and their ancestors' types; those after `end` with their positions
    moved by `shift`."""
    found: Counter = Counter()

    def walk(current: Node, field: str | None, ancestors: tuple[str, ...]):
        if current.end_byte <= start or current.start_byte >= end:
            moved = shift if current.start_byte >= end else 0
            found[
                (
                    current.type,
                    field,
                    current.start_byte + moved,
                    current.end_byte + moved,
                    current.is_error,
                    current.is_missing,
                    ancestors,
                )
            ] += 1
        if current.start_byte < end and current.end_byte > start or current.child_count:
            for index, child in enumerate(current.children):
                walk(child, current.field_name_for_child(index), (*ancestors, current.type))

    walk(node, None, ())
    return found


def check_file(
    parser: Parser, path: Path, edits: int, rng: random.Random, keep: Path | None
) -> tuple[int, list[str]]:
    """The number of edits made in `path`, and the problems found."""
    source = path.read_bytes()
    old_tree = parser.parse(source)
    candidates = [p for p in paragraphs(old_tree.root_node) if p.end_byte - p.start_byte > 1]
    problems = []
    made = 0
    for _ in range(min(edits, len(candidates))):
        paragraph = rng.choice(candidates)
        offset = rng.randrange(paragraph.start_byte, paragraph.end_byte)
        if inside_spanning(old_tree.root_node, offset, SPANNING) or inside_block(paragraph, offset):
            continue
        # Joining a line that starts with a command to the one before makes it
        # part of a paragraph, which may end a block early or late by design.
        joins_command = source[offset : offset + 1] == b"\n" and source[offset + 1 :].lstrip(
            b" \t"
        ).startswith(b"@")
        if rng.random() < 0.5 and not joins_command:
            removed, inserted = 1, b""
        else:
            removed, inserted = 0, rng.choice(INSERTIONS).encode()
        edited = source[:offset] + inserted + source[offset + removed :]
        line_start = edited.rfind(b"\n", 0, offset) + 1
        opener = LINE_COMMAND.match(edited, line_start)
        before = LINE_COMMAND.match(source, line_start)
        if (
            opener
            # A block opener, which runs on to its `@end` by design.
            and opener.group(1).decode() in BLOCK_NAMES
            and (before is None or before.group(1) != opener.group(1))
        ) or opens_block_after_brace(source, edited, offset):
            continue
        made += 1
        problems.extend(
            check_edit(parser, path, source, old_tree, paragraph, offset, removed, inserted, keep)
        )
    return made, problems


def check_edit(
    parser: Parser,
    path: Path,
    source: bytes,
    old_tree: Tree,
    paragraph: Node,
    offset: int,
    removed: int,
    inserted: bytes,
    keep: Path | None = None,
) -> list[str]:
    """The problems with an edit of `source`, the text of `path`, whose tree
    is `old_tree`: `removed` bytes at `offset`, in `paragraph`, replaced by
    `inserted`."""
    edited = source[:offset] + inserted + source[offset + removed :]
    description = (
        f"insert {inserted.decode()!r}" if inserted else f"delete {source[offset : offset + 1]!r}"
    )
    shift = len(inserted) - removed
    location = f"{path}:{point(source, offset).row + 1}:{point(source, offset).column + 1}"

    fresh = parser.parse(edited)
    tree = old_tree.copy()
    tree.edit(
        start_byte=offset,
        old_end_byte=offset + removed,
        new_end_byte=offset + len(inserted),
        start_point=point(source, offset),
        old_end_point=point(source, offset + removed),
        new_end_point=point(edited, offset + len(inserted)),
    )
    incremental = parser.parse(edited, tree)
    found = []
    if list(dump(incremental.root_node)) != list(dump(fresh.root_node)):
        found.append(f"{location}: {description}: incremental parse differs from a fresh one")

    # The paragraph, and the next one if the edit joined them, up to the
    # next blank line.
    # From the start of the paragraph's line (a multitable cell starts
    # after `@tab`).
    start = source.rfind(b"\n", 0, paragraph.start_byte) + 1
    block = preformatted_block(paragraph)
    if block is not None:
        end = block.end_byte + shift
    else:
        blank = BLANK_LINE.search(edited, max(offset, paragraph.end_byte + shift - 1))
        end = blank.end() if blank else len(edited)
        # A brace left open before a conditional can't close inside it,
        # where texi2any, which has evaluated the conditional, closes it:
        # it closes after the conditional (docs/design.md). A raw block stays
        # inside it, as in texi2any, whatever blank lines it holds. Up to
        # the next blank line after them, which may be after more of them.
        previous = None
        while end != previous:
            previous = end
            for conditional in conditionals(old_tree.root_node, start, end - shift):
                blank = BLANK_LINE.search(edited, conditional.end_byte + shift)
                end = max(end, blank.end() if blank else len(edited))
    before = outside(old_tree.root_node, start, end - shift, shift)
    after = outside(fresh.root_node, start, end, 0)
    if before != after:
        changed = sum(((before - after) + (after - before)).values())
        first = min((after - before) or (before - after), key=lambda n: n[2])
        found.append(
            f"{location}: {description}: {changed} nodes changed outside "
            f"bytes {start}-{end} of the edited text, the first a {first[0]} "
            f"at line {point(edited, first[2]).row + 1}"
        )
    if found and keep is not None:
        row, column = point(source, offset)
        copy = keep_copy(keep, path, row + 1, column + 1, edited)
        found = [f"{problem} (edited copy: {copy})" for problem in found]
    return found


def keep_copy(keep: Path, path: Path, row: int, column: int, edited: bytes) -> Path:
    """Writes the edited text, to inspect with `tree-sitter parse --cst` or
    `--debug-graph`, next to a copy of the original."""
    keep.mkdir(parents=True, exist_ok=True)
    copy = keep / f"{path.stem}-{row}-{column}{path.suffix}"
    copy.write_bytes(edited)
    original = keep / path.name
    if not original.exists():
        original.write_bytes(path.read_bytes())
    return copy


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    arguments.add_argument("--edits", type=int, default=3, help="edits per file (default 3)")
    arguments.add_argument("--seed", type=int, default=0, help="random seed (default 0)")
    arguments.add_argument(
        "--keep",
        type=Path,
        metavar="DIR",
        help="save the edited text of each problem, and the original, in DIR",
    )
    add_files_argument(arguments)
    options = arguments.parse_args()
    options.files = files_argument(options)

    parser = Parser(load_language())
    rng = random.Random(options.seed)
    problems = []
    made = 0
    for path in options.files:
        count, found = check_file(parser, path, options.edits, rng, options.keep)
        made += count
        problems.extend(found)
    for problem in problems:
        print(problem)
    print(f"{len(problems)} problems in {len(options.files)} files, {made} edits", file=sys.stderr)
    if made == 0:
        # No paragraph to edit, or every edit skipped: nothing was checked.
        print("No edit was made.", file=sys.stderr)
        return 1
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
