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
  `@linemacro` calls are skipped.
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

Usage: script/compare-texi2any.py [--structure] [--hunks N] FILE...
"""

import argparse
import ctypes
import dataclasses
import difflib
import itertools
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from tree_sitter import Language, Parser

ROOT = Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "build" / "texinfo.dylib"

FORMATS = ("info", "plaintext", "html", "docbook", "xml", "latex", "tex")
HEADINGS = (
    "part_heading", "top_heading", "chapter_heading", "section_heading",
    "subsection_heading", "subsubsection_heading",
)
MACRO_BLOCKS = ("macro", "rmacro", "linemacro")
# Block names, from the scanner's table.
BLOCK_NAMES = set(re.findall(r'\{"(\w+)", KIND_\w+\}',
                             (ROOT / "src" / "scanner.c").read_text()))
MAX_EXPANSION_DEPTH = 20
INLINE_CONDITIONALS = (
    "@inlinefmt", "@inlineraw", "@inlinefmtifelse", "@inlineifset", "@inlineifclear",
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
            cwd=ROOT, check=True, capture_output=True,
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
        self.values: dict[str, list[Token]] = {}
        self.macros: dict[str, Macro | None] = {}
        # While expanding a macro, tokens get the line of the outermost call.
        self.call_line: int | None = None
        self.in_definition_line = False
        # Macro expansion depth; errors only count in the file itself.
        self.depth = 0
        self.errors = 0

    def line(self, node) -> int:
        return self.call_line or node.start_point.row + 1

    def emit(self, name: str, node, structural: bool):
        self.tokens.append(Token(name, self.line(node), structural))

    def define_macro(self, kind: str, node):
        argument = node.child_by_field_name("argument")
        header = text(argument).strip() if argument else ""
        name, _, parameters = header.partition("{")
        name = name.strip()
        if kind == "linemacro":
            self.macros[name] = None
            return
        body = node.child_by_field_name("body")
        self.macros[name] = Macro(
            [p.strip() for p in parameters.rstrip("}").split(",") if p.strip()],
            text(body).removesuffix("\n") if body else "",
        )

    def expand_macro(self, macro: Macro, node):
        if self.depth >= MAX_EXPANSION_DEPTH:
            return
        arguments = node.child_by_field_name("arguments")
        call = text(arguments) if arguments else ""
        values = [v.strip() for v in call.split(",", max(len(macro.parameters) - 1, 0))]
        body = macro.body
        for parameter, value in zip(macro.parameters, values):
            body = body.replace("\\" + parameter + "\\", value)
        body = body.replace("\\\\", "\\")

        call_line = self.call_line
        self.call_line = self.line(node)
        self.depth += 1
        self.walk(self.parser.parse(body.encode()).root_node)
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

    def walk_expanded_arguments(self, name: str, arguments):
        """Walks only the arguments of an inline conditional that texi2any
        expands. Arguments are separated by top-level commas in text."""
        flag = first_word(arguments).rstrip(",")
        expanded = {
            "@inlinefmt": set(),
            "@inlineraw": set(),
            "@inlinefmtifelse": {2},
            "@inlineifset": {1} if flag in self.flags else set(),
            "@inlineifclear": set() if flag in self.flags else {1},
        }[name]
        index = 0
        for child in arguments.children:
            if child.type == "text":
                index += text(child).count(",")
            elif index in expanded:
                self.walk(child)

    def walk_definition_line(self, argument):
        previous = self.in_definition_line
        self.in_definition_line = True
        self.walk(argument)
        self.in_definition_line = previous

    def walk(self, node):
        kind = node.type
        if self.depth == 0 and (node.is_error or node.is_missing):
            self.errors += 1

        if kind in ("block", "list", "raw_block"):
            name_node = node.child_by_field_name("name")
            argument = node.child_by_field_name("argument")
            end = node.child_by_field_name("end")
            name = text(name_node)[1:]

            expanded = self.conditional(name, first_word(argument))
            if expanded is not None:
                if expanded:
                    self.children(node, name_node, argument, end)
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

        if kind in ("line_command", "item", "itemx", "bye") or kind in HEADINGS:
            # Headings name the field `command`; `@bye` has no field.
            name_node = (node.child_by_field_name("name")
                         or node.child_by_field_name("command")
                         or node.children[0])
            name = text(name_node)
            argument = node.child_by_field_name("argument")
            self.emit(name, node, True)
            if name == "@set":
                word = first_word(argument)
                self.flags.add(word)
                start = len(self.tokens)
                self.children(node, name_node)
                self.values[word] = self.tokens[start:]
                del self.tokens[start:]
            elif name == "@clear":
                self.flags.discard(first_word(argument))
                self.children(node, name_node)
            elif name.startswith("@def") and argument is not None:
                self.walk_definition_line(argument)
            elif kind != "bye":
                self.children(node, name_node)
            return

        if kind == "inline_command":
            name_node = node.child_by_field_name("name")
            name = text(name_node)
            if name == "@value":
                arguments = node.child_by_field_name("arguments")
                value = text(arguments).strip() if arguments else ""
                if value in self.values:
                    self.tokens.extend(dataclasses.replace(t, line=self.line(node))
                                       for t in self.values[value])
                else:
                    self.emit(name, node, False)
                return
            if name[1:] in self.macros:
                macro = self.macros[name[1:]]
                if macro is not None:
                    self.expand_macro(macro, node)
                return
            self.emit(name, node, False)
            arguments = node.child_by_field_name("arguments")
            if name in INLINE_CONDITIONALS and arguments is not None:
                self.walk_expanded_arguments(name, arguments)
            else:
                self.children(node, name_node)
            return

        if kind == "symbol_command":
            if not (self.in_definition_line and text(node) == "@\n"):
                self.emit(text(node), node, False)
            return

        if kind == "comment":
            self.emit(text(node).split()[0], node, True)
            return

        if kind == "input_line":
            return

        self.children(node)


def our_tokens(parser: Parser, path: Path) -> tuple[list[Token], int]:
    walker = OurWalker(parser)
    walker.walk(parser.parse(path.read_bytes()).root_node)
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
    return root


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


def run_texi2any(path: Path) -> str:
    """Returns texi2any's tree for `path`, as a debug dump."""
    with tempfile.TemporaryDirectory() as directory:
        shutil.copyfile(path, Path(directory) / path.name)
        result = subprocess.run(
            ["texi2any", "--force", "-c", "TEXINFO_OUTPUT_FORMAT=debugtree",
             "-o", "-", path.name],
            cwd=directory, capture_output=True,
        )
    return result.stdout.decode("utf-8", "replace")


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
                tokens.append(Token("-" + dump_text(children[-1]), structural=True))
            return
        tokens.append(Token(name, structural=name in structural_names))
        for child in children:
            walk(child)

    walk(read_dump(dump))
    return tokens


# Comparison ------------------------------------------------------------


def report(path: Path, ours: list[Token], theirs: list[Token], hunks: int) -> float:
    matcher = difflib.SequenceMatcher(
        a=[t.name for t in ours], b=[t.name for t in theirs], autojunk=False,
    )
    ratio = matcher.ratio()
    differences = [op for op in matcher.get_opcodes() if op[0] != "equal"]
    print(f"{path}: {ratio:.2%} match, {len(ours)} vs {len(theirs)} commands, "
          f"{len(differences)} differences")
    for tag, a1, a2, b1, b2 in differences[:hunks]:
        line = ours[a1].line if a1 < len(ours) else ours[-1].line if ours else "?"
        mine = " ".join(t.name for t in ours[a1:a2]) or "(nothing)"
        reference = " ".join(t.name for t in theirs[b1:b2]) or "(nothing)"
        print(f"  line {line}: ours {mine!r}, texi2any {reference!r}")
    if len(differences) > hunks:
        print(f"  ... {len(differences) - hunks} more")
    return ratio


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    arguments.add_argument("files", nargs="+", type=Path)
    arguments.add_argument("--structure", action="store_true",
                           help="compare only blocks, items and line commands")
    arguments.add_argument("--hunks", type=int, default=5,
                           help="differences to show per file (default 5)")
    options = arguments.parse_args()

    parser = Parser(load_language())
    total_ours = total_matched = 0
    # texi2any is the slow part: run it for all files in parallel, and report
    # in file order.
    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        dumps = [pool.submit(run_texi2any, path) for path in options.files]
        for path, dump in zip(options.files, dumps):
            ours, errors = our_tokens(parser, path)
            structural_names = {t.name for t in ours if t.structural}
            theirs = texi2any_tokens(dump.result(), structural_names)
            if options.structure:
                ours = [t for t in ours if t.structural]
                theirs = [t for t in theirs if t.structural]
            ratio = report(path, ours, theirs, options.hunks)
            if errors:
                print(f"  {errors} parse errors")
            total_ours += len(ours)
            total_matched += round(ratio * len(ours))
    if len(options.files) > 1 and total_ours:
        print(f"\nOverall: {total_matched / total_ours:.2%} of {total_ours} commands match")
    return 0


if __name__ == "__main__":
    sys.exit(main())
