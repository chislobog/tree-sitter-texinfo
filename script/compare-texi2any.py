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
import difflib
import shutil
import subprocess
import sys
import tempfile
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
MAX_EXPANSION_DEPTH = 20


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


class OurWalker:
    def __init__(self, parser: Parser, source: bytes):
        self.parser = parser
        self.source = source
        self.tokens: list[Token] = []
        self.flags: set[str] = {"txicommandconditionals"}
        # Commands in each `@set` value, emitted again at each `@value`.
        self.values: dict[str, list[Token]] = {}
        self.macros: dict[str, Macro | None] = {}
        # While expanding a macro, tokens get the line of the outermost call.
        self.call_line: int | None = None
        self.in_definition_line = False
        self.depth = 0
        self.errors = 0

    def text(self, node) -> str:
        return self.source[node.start_byte:node.end_byte].decode("utf-8", "replace")

    def first_word(self, node) -> str:
        if node is None:
            return ""
        words = self.text(node).split()
        return words[0] if words else ""

    def line(self, node) -> int:
        return self.call_line or node.start_point.row + 1

    def emit(self, name: str, node, structural: bool):
        self.tokens.append(Token(name, self.line(node), structural))

    def define_macro(self, kind: str, node):
        argument = node.child_by_field_name("argument")
        header = self.text(argument).strip() if argument else ""
        name, _, parameters = header.partition("{")
        name = name.strip()
        if kind == "linemacro":
            self.macros[name] = None
            return
        body = node.child_by_field_name("body")
        self.macros[name] = Macro(
            [p.strip() for p in parameters.rstrip("}").split(",") if p.strip()],
            self.text(body).removesuffix("\n") if body else "",
        )

    def expand_macro(self, macro: Macro, node):
        arguments = node.child_by_field_name("arguments")
        text = self.text(arguments) if arguments else ""
        if len(macro.parameters) > 1:
            values = [v.strip() for v in text.split(",", len(macro.parameters) - 1)]
        else:
            values = [text.strip()]
        body = macro.body
        for parameter, value in zip(macro.parameters, values):
            body = body.replace("\\" + parameter + "\\", value)
        body = body.replace("\\\\", "\\")
        if self.depth >= MAX_EXPANSION_DEPTH:
            return

        source, call_line = self.source, self.call_line
        self.call_line = self.line(node)
        self.source = body.encode()
        self.depth += 1
        root = self.parser.parse(self.source).root_node
        self.walk(root, count_errors=False)
        self.depth -= 1
        self.source, self.call_line = source, call_line

    def children(self, node, skip=()):
        for child in node.children:
            if child.id not in skip:
                self.walk(child, count_errors=self.depth == 0)

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

    def walk_definition_line(self, argument):
        self.in_definition_line = True
        self.walk(argument, count_errors=self.depth == 0)
        self.in_definition_line = False

    def walk(self, node, count_errors=True):
        kind = node.type
        if count_errors and (node.is_error or node.is_missing):
            self.errors += 1

        if kind in ("block", "list", "raw_block"):
            name_node = node.child_by_field_name("name")
            argument = node.child_by_field_name("argument")
            end = node.child_by_field_name("end")
            name = self.text(name_node)[1:]

            expanded = self.conditional(name, self.first_word(argument))
            if expanded is not None:
                if expanded:
                    skip = {name_node.id, end.id} | ({argument.id} if argument else set())
                    self.children(node, skip)
                return

            if name in MACRO_BLOCKS:
                self.define_macro(name, node)
            self.emit("+" + name, node, True)
            if argument is not None and name.startswith("def"):
                self.walk_definition_line(argument)
                self.children(node, {name_node.id, end.id, argument.id})
            else:
                self.children(node, {name_node.id, end.id})
            if end.end_byte > end.start_byte:
                self.emit("-" + self.text(end).split()[-1], end, True)
            return

        if kind in ("line_command", "item", "itemx", "bye") or kind in HEADINGS:
            name_node = node.child_by_field_name("name") or node.child_by_field_name("command")
            if name_node is None:
                name_node = node.children[0]
            name = self.text(name_node)
            argument = node.child_by_field_name("argument")
            if name in ("@set", "@clear"):
                word = self.first_word(argument)
                (self.flags.add if name == "@set" else self.flags.discard)(word)
            self.emit(name, node, True)
            if name == "@set":
                start = len(self.tokens)
                self.children(node, {name_node.id})
                self.values[word] = self.tokens[start:]
                del self.tokens[start:]
            elif name.startswith("@def") and argument is not None:
                self.walk_definition_line(argument)
            elif kind != "bye":
                self.children(node, {name_node.id})
            return

        if kind == "inline_command":
            name_node = node.child_by_field_name("name")
            name = self.text(name_node)
            if name == "@value":
                arguments = node.child_by_field_name("arguments")
                value = self.text(arguments).strip() if arguments else ""
                if value not in self.values:
                    self.emit(name, node, False)
                self.tokens.extend(Token(t.name, self.line(node), t.structural)
                                   for t in self.values.get(value, []))
                return
            if name[1:] in self.macros:
                macro = self.macros[name[1:]]
                if macro is not None:
                    self.expand_macro(macro, node)
                return
            self.emit(name, node, False)
            self.children(node, {name_node.id})
            return

        if kind == "symbol_command":
            if not (self.in_definition_line and self.text(node) == "@\n"):
                self.emit(self.text(node), node, False)
            return

        if kind == "comment":
            self.emit(self.text(node).split()[0], node, True)
            return

        if kind == "input_line":
            return

        self.children(node)


def our_tokens(parser: Parser, path: Path) -> tuple[list[Token], int]:
    source = path.read_bytes()
    walker = OurWalker(parser, source)
    walker.walk(parser.parse(source).root_node)
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
    if label == "@\n":
        return label
    rest = label[1:]
    if rest[:1].isalpha():
        end = 1
        while end < len(rest) and rest[end].isalnum():
            end += 1
        return "@" + rest[:end]
    return "@" + rest[:1]


def dump_text(node: DumpNode) -> str:
    for child in node.children:
        if child.label.startswith("|"):
            return child.label.strip("|")
        found = dump_text(child)
        if found:
            return found
    return ""


def texi2any_tokens(path: Path, structural_names: set[str]) -> list[Token]:
    with tempfile.TemporaryDirectory() as directory:
        copy = Path(directory) / path.name
        shutil.copyfile(path, copy)
        result = subprocess.run(
            ["texi2any", "--force", "-c", "TEXINFO_OUTPUT_FORMAT=debugtree",
             "-o", "-", copy.name],
            cwd=directory, capture_output=True,
        )
    tokens: list[Token] = []

    def walk(node: DumpNode):
        name = command_name(node.label)
        if name is None or name == "@end":
            for child in node.children:
                walk(child)
            return
        children = node.children
        if children and command_name(children[-1].label) == "@end":
            tokens.append(Token("+" + name[1:], structural=True))
            for child in children[:-1]:
                walk(child)
            tokens.append(Token("-" + dump_text(children[-1]), structural=True))
            return
        tokens.append(Token(name, structural=name in structural_names))
        for child in children:
            walk(child)

    walk(read_dump(result.stdout.decode("utf-8", "replace")))
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
    for path in options.files:
        ours, errors = our_tokens(parser, path)
        structural_names = {t.name for t in ours if t.structural}
        theirs = texi2any_tokens(path, structural_names)
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
