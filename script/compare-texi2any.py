#!/usr/bin/env python3
r"""Compare this grammar's parse trees with texi2any's, the reference parser.

For each file, both trees are flattened into the sequence of @-commands they
contain, in document order: `+name` and `-name` for the start and end of a
block, `@name` for every other command. The two sequences are then diffed.

texi2any does things a tree-sitter grammar cannot, so this side mirrors them:

- Conditionals are evaluated the way texi2any does for its debug output: no
  output format is expanded, so `@ifX` blocks are dropped and `@ifnotX` blocks
  are kept, both without their markers. `@ifset`/`@ifclear` follow `@set` and
  `@clear`; `@ifcommanddefined` is assumed true.
- texi2any keeps an `@set` value as raw text and parses it where `@value`
  uses it, so the commands in a value are moved to each `@value`.
- Calls to user-defined macros are expanded like texi2any does: arguments
  are substituted into the body, which is parsed with this grammar.
  `@linemacro` calls at the start of a line are expanded too.
- Inline conditionals are evaluated the same way: only the arguments
  texi2any expands (`@inlinefmtifelse`'s else, `@inlineifset`'s text when
  the flag is set, ...) contribute commands.
- `@value` of a variable that is never set stays a command, as in texi2any
  (typically one set in an `@include`d file).
- `txicommandconditionals` is set, as texi2any predefines it.
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
       [--expected FILE] [--write-expected FILE] FILE...
"""

import argparse
import ctypes
import difflib
import itertools
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

from tree_sitter import Language, Parser

ROOT = Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "build" / "texinfo.dylib"

FORMATS = ("info", "plaintext", "html", "docbook", "xml", "latex", "tex")
HEADINGS = (
    "part_heading",
    "top_heading",
    "chapter_heading",
    "section_heading",
    "subsection_heading",
    "subsubsection_heading",
    "heading",
)
MACRO_BLOCKS = ("macro", "rmacro", "linemacro")
# Block names, from the scanner's table.
BLOCK_NAMES = set(
    re.findall(r'\{"(\w+)", KIND_\w+(?: \+ \d+)?\}', (ROOT / "src" / "scanner.c").read_text())
)
MAX_EXPANSION_DEPTH = 20
INLINE_CONDITIONALS = (
    "@inlinefmt",
    "@inlineraw",
    "@inlinefmtifelse",
    "@inlineifset",
    "@inlineifclear",
)


@dataclass
class Macro:
    parameters: list[str]
    body: str


@dataclass
class Token:
    name: str
    # Line in the source file; texi2any's dump doesn't record positions.
    line: int | None = None
    # Block markers, items and line commands; the rest are inline.
    structural: bool = False


def load_language() -> Language:
    sources = [*ROOT.glob("src/*.c"), *ROOT.glob("src/tree_sitter/*.h")]
    newest = max(source.stat().st_mtime for source in sources)
    if not LIBRARY.exists() or LIBRARY.stat().st_mtime < newest:
        subprocess.run(
            ["tree-sitter", "build", "-o", str(LIBRARY)],
            cwd=ROOT,
            check=True,
            capture_output=True,
        )
    library = ctypes.cdll.LoadLibrary(str(LIBRARY))
    library.tree_sitter_texinfo.restype = ctypes.c_void_p
    # py-tree-sitter takes the language pointer wrapped in a PyCapsule.
    capsule_new = ctypes.pythonapi.PyCapsule_New
    capsule_new.restype = ctypes.py_object
    capsule_new.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
    return Language(capsule_new(library.tree_sitter_texinfo(), b"tree_sitter.Language", None))


# Our tree ---------------------------------------------------------------


def text(node) -> str:
    return node.text.decode("utf-8", "replace")


def first_word(node) -> str:
    words = text(node).split() if node is not None else []
    return words[0] if words else ""


class OurWalker:
    def __init__(self, parser: Parser):
        self.parser = parser
        self.tokens: list[Token] = []
        self.flags: set[str] = {"txicommandconditionals"}
        # Commands in each `@set` value, emitted again at each `@value`.
        self.values: dict[str, str] = {}
        self.macros: dict[str, Macro | None] = {}
        self.linemacros: dict[str, Macro] = {}
        # The source being walked: the file, or an expansion in it.
        self.sources: list[bytes] = []
        # Tokens of a line macro call's own line, replaced by its expansion:
        # (expansion depth, end byte of the call).
        self.skip: tuple[int, int] | None = None
        # `@alias NEW = EXISTING`: texi2any replaces NEW with EXISTING.
        self.aliases: dict[str, str] = {}
        # The command `@clickstyle` chose, which texi2any puts for `@click`.
        self.click: str | None = None
        # The line of an alias of `@c` or `@comment`, which texi2any makes a
        # comment to the end of the line.
        self.comment_line: int | None = None
        # While expanding a macro, tokens get the line of the outermost call.
        self.call_line: int | None = None
        self.in_definition_line = False
        # Macro expansion depth; errors only count in the file itself.
        self.depth = 0
        self.errors = 0

    def line(self, node) -> int:
        return self.call_line or node.start_point.row + 1

    def emit(self, name: str, node, structural: bool):
        line = self.line(node)
        if self.skip and self.skip[0] == self.depth and node.start_byte < self.skip[1]:
            return
        if line != self.comment_line:
            self.tokens.append(Token(name, line, structural))

    def define_macro(self, kind: str, node):
        argument = node.child_by_field_name("argument")
        header = text(argument).strip() if argument else ""
        name, _, parameters = header.partition("{")
        name = name.strip()
        # A new definition replaces an alias of the same name.
        self.aliases.pop("@" + name, None)
        body = node.child_by_field_name("body")
        macro = Macro(
            [p.strip() for p in parameters.rstrip("}").split(",") if p.strip()],
            text(body).removesuffix("\n") if body else "",
        )
        if kind == "linemacro":
            self.macros.pop(name, None)
            self.linemacros[name] = macro
        else:
            self.linemacros.pop(name, None)
            self.macros[name] = macro

    def expand_linemacro(self, macro: Macro, node):
        """Expands a line macro call at `node`, a command at the start of a
        line: its arguments are the rest of the line, and the next ones while
        a line ends in `@`. The other tokens of those lines are left out."""
        source = self.sources[-1]
        end = node.end_byte
        while True:
            newline = source.find(b"\n", end)
            if newline < 0:
                end = len(source)
                break
            end = newline
            if not source[:end].rstrip(b"\r").endswith(b"@"):
                break
            end += 1
        call = source[node.end_byte : end].decode("utf-8", "replace")
        self.skip = (self.depth, end)
        self.substitute(macro, node, linemacro_arguments(call, len(macro.parameters)))

    def substitute(self, macro: Macro, node, values: list[str]):
        body = macro.body
        # A call may give fewer arguments than the macro has parameters.
        for parameter, value in zip(macro.parameters, values, strict=False):
            body = body.replace("\\" + parameter + "\\", value)
        body = body.replace("\\\\", "\\")
        self.walk_source(body, node)

    def expand_macro(self, macro: Macro, node, call: str):
        if self.depth >= MAX_EXPANSION_DEPTH:
            return
        values = [v.strip() for v in call.split(",", max(len(macro.parameters) - 1, 0))]
        self.substitute(macro, node, values)

    def walk_source(self, source: str, node):
        """Parses and walks text that texi2any parses where it is used (a
        macro expansion or an `@set` value), as if it were at `node`."""
        if self.depth >= MAX_EXPANSION_DEPTH:
            return
        call_line = self.call_line
        self.call_line = self.line(node)
        self.depth += 1
        self.sources.append(source.encode())
        self.walk(self.parser.parse(self.sources[-1]).root_node)
        self.sources.pop()
        self.depth -= 1
        self.call_line = call_line

    def children(self, node, *skip):
        """Walks the children of `node`, except the `skip` nodes."""
        skipped = {n.id for n in skip if n is not None}
        for child in node.children:
            if child.id not in skipped:
                self.walk(child)

    def conditional(self, name: str, argument: str) -> bool | None:
        """Whether texi2any expands a conditional, or None if not one."""
        if name in ("if" + f for f in FORMATS):
            return False
        if name in ("ifnot" + f for f in FORMATS):
            return True
        if name == "ifset":
            return argument in self.flags
        if name == "ifclear":
            return argument not in self.flags
        if name == "ifcommanddefined":
            return True
        if name == "ifcommandnotdefined":
            return False
        return None

    def walk_expanded_arguments(self, name: str, node):
        """Walks only the arguments of an inline conditional that texi2any
        expands: the `condition` and `text` fields, or for @inlinefmtifelse,
        whose argument isn't split, the parts between top-level commas."""
        condition = node.child_by_field_name("condition")
        arguments = node.child_by_field_name("arguments")
        flag = first_word(condition or arguments).rstrip(",")
        expanded = {
            "@inlinefmt": set(),
            "@inlineraw": set(),
            "@inlinefmtifelse": {2},
            "@inlineifset": {1} if flag in self.flags else set(),
            "@inlineifclear": set() if flag in self.flags else {1},
        }[name]
        if arguments is None:
            body = node.child_by_field_name("text")
            if 1 in expanded and body is not None:
                self.walk(body)
            return
        index = 0
        for child in arguments.children:
            if child.type == "text":
                index += text(child).count(",")
            elif index in expanded:
                self.walk(child)

    def walk_definition_line(self, node, *skip):
        """Walks the children of a definition line (or of its argument),
        except the `skip` nodes."""
        previous = self.in_definition_line
        self.in_definition_line = True
        self.children(node, *skip)
        self.in_definition_line = previous

    def walk(self, node):
        kind = node.type
        if self.depth == 0 and (node.is_error or node.is_missing):
            self.errors += 1

        if kind == "definition_block":
            line = node.children[0]
            name_node = line.child_by_field_name("name")
            end = node.child_by_field_name("end")
            name = text(name_node)[1:]
            self.emit("+" + name, node, True)
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

            flag = re.match(r"[\w-]*", first_word(argument)).group()
            expanded = self.conditional(name, flag)
            if expanded is not None:
                if expanded:
                    # texi2any drops comments on the conditional's own lines
                    # together with the conditional.
                    marker_rows = {node.start_point.row, end.start_point.row}
                    on_marker_lines = [
                        child
                        for child in node.children
                        if child.type == "comment" and child.start_point.row in marker_rows
                    ]
                    self.children(node, name_node, argument, end, *on_marker_lines)
                return

            if name in MACRO_BLOCKS:
                self.define_macro(name, node)
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

        if kind in ("line_command", "node_line", "item", "itemx", "bye") or kind in HEADINGS:
            name_node = node.child_by_field_name("name")
            name = self.aliases.get(text(name_node), text(name_node))
            argument = node.child_by_field_name("argument")
            # A macro whose name ends in `index` parses as a line command.
            if name[1:] in self.macros:
                macro = self.macros[name[1:]]
                if macro is not None:
                    call = text(argument).strip() if argument else ""
                    if call.startswith("{") and call.endswith("}"):
                        call = call[1:-1]
                    self.expand_macro(macro, node, call)
                return
            self.emit(name, node, True)
            if name == "@set":
                # The value is raw text, which texi2any parses at each
                # `@value`, with the variables set by then.
                variable = node.child_by_field_name("variable")
                value = node.child_by_field_name("value")
                word = first_word(variable)
                self.flags.add(word)
                self.values[word] = text(value) if value is not None else ""
                self.children(node, name_node, variable, value)  # comments
            elif name == "@clear":
                variable = node.child_by_field_name("variable")
                self.flags.discard(first_word(variable))
                self.children(node, name_node, variable)  # comments
            elif name == "@alias":
                # texi2any only accepts `NEW = EXISTING`, with nothing after,
                # but parses commands on a bad line too.
                new = node.child_by_field_name("alias")
                target = node.child_by_field_name("target")
                if new is not None and target is not None and argument is None:
                    self.aliases["@" + text(new)] = "@" + text(target)
                self.children(node, name_node)
            elif name in ("@defindex", "@defcodeindex", "@definfoenclose"):
                # Defines `@NAMEindex` or `@NAME`, replacing an alias of it.
                word = first_word(argument).split(",")[0] if argument else ""
                suffix = "" if name == "@definfoenclose" else "index"
                self.aliases.pop("@" + word + suffix, None)
                self.walk_definition_line(node, name_node)
            elif name == "@clickstyle":
                # The argument is raw text to texi2any.
                match = (
                    re.match(r"@([a-zA-Z][a-zA-Z0-9-]*)", text(argument).strip())
                    if argument
                    else None
                )
                if match:
                    self.click = "@" + match.group(1)
                self.children(node, name_node, argument)  # comments
            elif name.startswith("@def"):
                self.walk_definition_line(node, name_node)
            else:
                # Includes a comment on the line, also after `@bye`.
                self.children(node, name_node)
            return

        if kind == "inline_command":
            name_node = node.child_by_field_name("name")
            name = self.aliases.get(text(name_node), text(name_node))
            if name == "@click" and self.click:
                name = self.click
            if name in ("@c", "@comment") and text(name_node) != name:
                self.emit(name, node, True)
                self.comment_line = self.line(node)
                return
            if name == "@value":
                arguments = node.child_by_field_name("arguments")
                value = text(arguments).strip() if arguments else ""
                if value in self.values:
                    self.walk_source(self.values[value], node)
                else:
                    self.emit(name, node, False)
                return
            if name[1:] in self.linemacros:
                # Only a call at the start of a line expands.
                start = self.sources[-1].rfind(b"\n", 0, node.start_byte) + 1
                if not self.sources[-1][start : node.start_byte].strip():
                    self.expand_linemacro(self.linemacros[name[1:]], node)
                return
            if name[1:] in self.macros:
                macro = self.macros[name[1:]]
                if macro is not None:
                    arguments = node.child_by_field_name("arguments")
                    self.expand_macro(macro, node, text(arguments) if arguments else "")
                return
            self.emit(name, node, False)
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
            self.emit(text(node).split()[0], node, True)
            return

        if kind == "input_line":
            return

        # Only reached inside an ERROR node, where tree-sitter still
        # recognised the command (e.g. `@code{` unclosed at the end of a file).
        if kind == "command_name":
            self.emit(text(node), node, False)
            return

        self.children(node)


def linemacro_arguments(call: str, count: int) -> list[str]:
    """Splits a line macro call's arguments at spaces, as texi2any does:
    braces around an argument are removed, and the last one takes the rest."""
    arguments: list[str] = []
    # A newline kept after a final `@` belongs to the last argument.
    rest = call.strip(" \t")
    while rest and len(arguments) < count - 1:
        depth = index = 0
        while index < len(rest):
            char = rest[index]
            if char == "@":
                index += 1
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
            elif char.isspace() and depth == 0:
                break
            index += 1
        arguments.append(rest[:index])
        rest = rest[index:].lstrip()
    if rest:
        arguments.append(rest)
    return [
        a[1:-1] if a.startswith("{") and a.endswith("}") and _balanced(a[1:-1]) else a
        for a in arguments
    ]


def _balanced(text: str) -> bool:
    depth = 0
    escaped = False
    for char in text:
        if escaped:
            escaped = False
        elif char == "@":
            escaped = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth < 0:
                return False
    return depth == 0


def our_tokens(parser: Parser, path: Path) -> tuple[list[Token], int]:
    walker = OurWalker(parser)
    walker.sources.append(path.read_bytes())
    walker.walk(parser.parse(walker.sources[0]).root_node)
    return walker.tokens, walker.errors


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


def command_name(label: str) -> str | None:
    if not label.startswith("@"):
        return None
    rest = label[1:]
    if rest[:1].isalpha():
        return "@" + "".join(itertools.takewhile(str.isalnum, rest))
    # A single non-letter, including the newline of `@` at the end of a line.
    return "@" + rest[:1]


def dump_text(node: DumpNode) -> str:
    for child in node.children:
        if child.label.startswith("|"):
            return child.label.strip("|")
        found = dump_text(child)
        if found:
            return found
    return ""


def run_texi2any(path: Path) -> tuple[str, set[tuple[int, str]]]:
    """Returns texi2any's tree for `path`, as a debug dump, and the commands
    it doesn't know, as (line, name) pairs."""
    with tempfile.TemporaryDirectory() as directory:
        shutil.copyfile(path, Path(directory) / path.name)
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
                "-o",
                "-",
                path.name,
            ],
            cwd=directory,
            capture_output=True,
            # texi2any exits with an error on invalid input, but still
            # prints its tree, which is what is compared.
            check=False,
        )
    stderr = result.stderr.decode("utf-8", "replace")
    unknown = {(int(line), name) for line, name in UNKNOWN_COMMAND.findall(stderr)}
    return result.stdout.decode("utf-8", "replace"), unknown


# texi2any's warning for a command it doesn't know, such as a TeX primitive or
# an index command defined in another file of the manual.
UNKNOWN_COMMAND = re.compile(r":(\d+): unknown command `([^']+)'")


def texi2any_tokens(dump: str, structural_names: set[str]) -> list[Token]:
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
        tokens.append(Token(name, structural=name in structural_names))
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
    arguments.add_argument("files", nargs="+", type=Path)
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

    parser = Parser(load_language())
    total_ours = total_matched = total_differences = 0
    found: dict[str, list[str]] = {}
    # texi2any is the slow part: run it for all files in parallel, and report
    # in file order.
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        dumps = [pool.submit(run_texi2any, path) for path in options.files]
        for path, dump in zip(options.files, dumps, strict=True):
            ours, errors = our_tokens(parser, path)
            output, unknown = dump.result()
            # texi2any drops the commands it doesn't know, so they can't be
            # compared.
            ours = [t for t in ours if (t.line, t.name[1:]) not in unknown]
            structural_names = {t.name for t in ours if t.structural}
            theirs = texi2any_tokens(output, structural_names)
            if options.structure:
                ours = [t for t in ours if t.structural]
                theirs = [t for t in theirs if t.structural]
            file_differences, matched = differences(ours, theirs)
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
                "the regression, or if the change is deliberate, rerun with "
                "--write-expected and give each new file a `#` comment with "
                "the reason.",
                file=sys.stderr,
            )
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
