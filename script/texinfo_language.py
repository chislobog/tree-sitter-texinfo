"""This grammar's parser for the scripts in script/, and a full dump of a tree.

The parser is built into build/ when src/ is newer than the build, or into
another checkout's build/ for comparing with an earlier revision.

Also reads the scanner's tables of commands and blocks (scanner_commands,
scanner_blocks, BLOCK_NAMES), and holds ARGUMENT_FIELDS (from the
grammar's table), the file sets (manuals,
edited-manuals, compared-manuals, test-inputs) and helpers to add them as
arguments to argparse.
"""

import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

from tree_sitter import Language, Node

ROOT = Path(__file__).resolve().parent.parent


def _library_extension() -> str:
    """The dynamic library suffix for this platform."""
    if sys.platform == "darwin":
        return ".dylib"
    return ".so"


def _scanner_table(source: str, table: str) -> list[tuple[str, str]]:
    match = re.search(rf"\b{table}\[\] = \{{\n(.*?)\n\}};", source, re.S)
    if match is None:
        raise SystemExit(f"no table {table} in the scanner")
    return re.findall(r'\{"([^"]+)", ([^}]+)\}', match.group(1))


def scanner_commands(source: str) -> dict[str, frozenset[str]]:
    """The scanner's table of commands other than blocks (COMMANDS in
    src/scanner.c's `source`), in its order: each name with its flags, a
    sectioning command's level among them as `LEVEL(N)`."""
    return {
        name: frozenset(flag.strip() for flag in flags.split("|"))
        for name, flags in _scanner_table(source, "COMMANDS")
    }


def scanner_blocks(source: str) -> dict[str, str]:
    """The scanner's table of blocks (BLOCKS in src/scanner.c's `source`),
    in its order: each name with its kind, such as `KIND_DEFINITION + 2`."""
    return dict(_scanner_table(source, "BLOCKS"))


# Block names, from the scanner's table.
BLOCK_NAMES = frozenset(scanner_blocks((ROOT / "src" / "scanner.c").read_text()))


def _argument_fields() -> dict[str, tuple[str, ...]]:
    table = re.search(
        r"^const ARGUMENT_COMMANDS = \{\n(.*?)^\};", (ROOT / "grammar.js").read_text(), re.M | re.S
    )
    if table is None:
        raise SystemExit("grammar.js has no ARGUMENT_COMMANDS table")
    return {
        command: tuple(fields.split())
        for fields, commands in re.findall(r"'([\w ]+)': \[([^\]]*)\]", table.group(1))
        for command in re.findall(r"'(\w+)'", commands)
    }


# The brace commands whose arguments split at commas, with the field of each
# argument by position, from the grammar's ARGUMENT_COMMANDS.
ARGUMENT_FIELDS = _argument_fields()


def texi2any_table(module: str, table: str) -> dict[str, str]:
    """A command table of the texi2any on the path, such as `%line_commands`
    in its Commands.pm: each command with its value."""
    share = Path(shutil.which("texi2any") or "texi2any").resolve().parent.parent / "share"
    source = (share / "texi2any" / "Texinfo" / f"{module}.pm").read_text()
    match = re.search(rf"our %{table} = \((.*?)\n\);", source, re.S)
    if match is None:
        raise SystemExit(f"texi2any's {module}.pm has no %{table} table")
    # Names in either quotes, as `"'"`, the accent.
    entries = re.findall(r'(?:"([^"]+)"|\'([^\']+)\')\s*=>\s*\'?([^\',\n]*)', match.group(1))
    return {double or single: value for double, single, value in entries}


def load_language(root: Path = ROOT) -> Language:
    """The parser generated in `root`'s src/, built if needed."""
    library = root / "build" / f"texinfo{_library_extension()}"
    sources = [*root.glob("src/*.c"), *root.glob("src/tree_sitter/*.h")]
    newest = max(source.stat().st_mtime for source in sources)
    if not library.exists() or library.stat().st_mtime < newest:
        library.parent.mkdir(exist_ok=True)
        built = subprocess.run(
            ["tree-sitter", "build", "-o", str(library)],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        if built.returncode != 0:
            raise SystemExit(f"tree-sitter build failed:\n{built.stdout}{built.stderr}")
    loaded = ctypes.cdll.LoadLibrary(str(library))
    loaded.tree_sitter_texinfo.restype = ctypes.c_void_p
    # py-tree-sitter takes the language pointer wrapped in a PyCapsule.
    capsule_new = ctypes.pythonapi.PyCapsule_New
    capsule_new.restype = ctypes.py_object
    capsule_new.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_void_p]
    return Language(capsule_new(loaded.tree_sitter_texinfo(), b"tree_sitter.Language", None))


def dump(node: Node) -> Iterator[str]:
    """Every node of the tree, named or not, one per line with its field, its
    byte range and whether it is an error or missing, indented by depth."""
    cursor = node.walk()
    depth = 0
    while True:
        current = cursor.node
        field = cursor.field_name
        kind = current.type if current.is_named else repr(current.type)
        flags = " ERROR" if current.is_error else " MISSING" if current.is_missing else ""
        prefix = f"{field}: " if field else ""
        yield f"{'  ' * depth}{prefix}{kind} {current.start_byte}-{current.end_byte}{flags}"
        if cursor.goto_first_child():
            depth += 1
            continue
        while not cursor.goto_next_sibling():
            if not cursor.goto_parent():
                return
            depth -= 1


# Texinfo files' extensions.
TEXINFO_SUFFIXES = (".texi", ".texinfo", ".txi")
# Files texi2any itself fails on, or meant only for @include: left out of
# the comparison with it.
NOT_COMPARED = ("texinfo-tex-test.texi", "html_default_commands_args.texi")


def _find(directory: Path, suffixes: tuple[str, ...]) -> list[Path]:
    """The files under `directory` with one of `suffixes`, following
    symbolic links, as `find -L` does, sorted. Relative to the current
    directory, so that from the repository's root the names are those the
    snapshots and the lists of differences record."""
    if Path.cwd().resolve() != ROOT:
        raise SystemExit(f"run from the repository's root, {ROOT}, as the names are relative to it")

    def unreadable(error: OSError):
        raise SystemExit(f"can't read {error.filename}: {error.strerror}")

    found = []
    for parent, _, names in os.walk(directory, followlinks=True, onerror=unreadable):
        found.extend(
            Path(os.path.relpath(Path(parent, name))) for name in names if name.endswith(suffixes)
        )
    return sorted(found)


def manuals() -> list[Path]:
    """The example manuals (examples/, linked by the dev shell)."""
    return _find(ROOT / "examples", TEXINFO_SUFFIXES)


def edited_manuals() -> list[Path]:
    """The example manuals, except gawk's Italian ones (in another encoding)."""
    return [p for p in manuals() if not (p.parent.name == "it" and "gawk" in p.parent.parent.name)]


def compared_manuals() -> list[Path]:
    """The manuals compared with texi2any."""
    return [p for p in edited_manuals() if p.name not in NOT_COMPARED]


def test_inputs() -> list[Path]:
    """texi2any's test inputs (texinfo-tests/, linked by the dev shell)."""
    return _find(ROOT / "texinfo-tests", TEXINFO_SUFFIXES)


# A corpus test: a header (a line of `=`, the name and attributes, a line of
# `=`), the input, a line of `-`, and the expected tree.
CORPUS_TEST = re.compile(r"^=+\n(.*?)\n=+\n(.*?)\n-{3,}\n", re.S | re.M)


def corpus_tests() -> Iterator[tuple[Path, str, str]]:
    """Each corpus test's file, title and input, with its carriage returns."""
    for corpus in sorted((ROOT / "test" / "corpus").glob("*.txt")):
        with corpus.open(newline="") as file:
            text = file.read()
        for match in CORPUS_TEST.finditer(text):
            yield corpus, match.group(1).splitlines()[0], match.group(2)


def corpus_inputs() -> list[Path]:
    """The input of each corpus test, written to
    build/corpus/FILE/TITLE.texi, named after its title so that the name
    stays when tests are added before it."""
    found = []
    for corpus, title, text in corpus_tests():
        slug = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")
        path = ROOT / "build" / "corpus" / corpus.stem / f"{slug}.texi"
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("w", newline="") as file:
            file.write(text)
        found.append(Path(os.path.relpath(path)))
    return found


FILE_SETS = {
    "manuals": (manuals, "example manuals (examples/, linked by the dev shell)"),
    "edited-manuals": (
        edited_manuals,
        "example manuals, except gawk's Italian ones (in another encoding)",
    ),
    "compared-manuals": (compared_manuals, "manuals compared with texi2any"),
    "test-inputs": (
        test_inputs,
        "texi2any's test inputs (texinfo-tests/, linked by the dev shell)",
    ),
    "corpus-inputs": (
        corpus_inputs,
        "the corpus tests' inputs, written to build/corpus/ under their titles",
    ),
}


def add_files_argument(arguments: argparse.ArgumentParser):
    """The files a script checks: given as arguments, selected with the
    options below, or else one per line on standard input, so that a check
    sees all of them at once, as `xargs` wouldn't ensure."""
    arguments.add_argument(
        "files",
        nargs="*",
        type=Path,
        help="the files; read from standard input, one per line, if none are given",
    )
    for name, (function, help_text) in FILE_SETS.items():
        arguments.add_argument(
            f"--{name}",
            action="append_const",
            const=function,
            dest="file_sets",
            help=f"also {help_text}",
        )


def files_argument(options: argparse.Namespace) -> list[Path]:
    files = list(options.files)
    for function in options.file_sets or []:
        found = function()
        if not found:
            set_name = function.__name__.replace("_", "-")
            raise SystemExit(f"no files in {set_name} (enter the dev shell, which links it)")
        files.extend(found)
    if files:
        return list(dict.fromkeys(files))
    if sys.stdin.isatty():
        raise SystemExit("no files given (use arguments or --manuals/--test-inputs/...)")
    files = [Path(line) for line in sys.stdin.read().splitlines() if line]
    if not files:
        raise SystemExit("no files given")
    return files
