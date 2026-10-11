"""This grammar's parser for the scripts in script/, and a full dump of a tree.

The parser is built into build/ when src/ is newer than the build, or into
another checkout's build/ for comparing with an earlier revision.
"""

import argparse
import ctypes
import os
import re
import subprocess
import sys
from collections.abc import Iterator
from pathlib import Path

from tree_sitter import Language, Node

ROOT = Path(__file__).resolve().parent.parent

# Block names, from the scanner's table.
BLOCK_NAMES = frozenset(
    re.findall(r'\{"(\w+)", KIND_\w+(?: \+ \d+)?\}', (ROOT / "src" / "scanner.c").read_text())
)


def load_language(root: Path = ROOT) -> Language:
    """The parser generated in `root`'s src/, built if needed."""
    library = root / "build" / "texinfo.dylib"
    sources = [*root.glob("src/*.c"), *root.glob("src/tree_sitter/*.h")]
    newest = max(source.stat().st_mtime for source in sources)
    if not library.exists() or library.stat().st_mtime < newest:
        library.parent.mkdir(exist_ok=True)
        subprocess.run(
            ["tree-sitter", "build", "-o", str(library)],
            cwd=root,
            check=True,
            capture_output=True,
        )
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


def _find(directory: str, suffixes: tuple[str, ...]) -> list[Path]:
    """The files under `directory` with one of `suffixes`, following
    symbolic links, as `find -L` does, sorted."""
    found = []
    for parent, _, names in os.walk(directory, followlinks=True):
        found.extend(Path(parent, name) for name in names if name.endswith(suffixes))
    return sorted(found)


def manuals() -> list[Path]:
    """The example manuals (examples/, linked by the dev shell)."""
    return _find("examples", TEXINFO_SUFFIXES)


def edited_manuals() -> list[Path]:
    """The manuals without gawk's Italian ones, in another encoding."""
    return [p for p in manuals() if "it" not in p.parts]


def compared_manuals() -> list[Path]:
    """The manuals compared with texi2any."""
    return [p for p in edited_manuals() if p.name not in NOT_COMPARED]


def test_inputs() -> list[Path]:
    """texi2any's test inputs (texinfo-tests/, linked by the dev shell)."""
    return _find("texinfo-tests", (".texi",))


FILE_SETS = {
    "manuals": manuals,
    "edited-manuals": edited_manuals,
    "compared-manuals": compared_manuals,
    "test-inputs": test_inputs,
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
    for name, function in FILE_SETS.items():
        arguments.add_argument(
            f"--{name}",
            action="append_const",
            const=function,
            dest="file_sets",
            help=f"also {function.__doc__.splitlines()[0][4:].rstrip('.')}",
        )


def files_argument(options: argparse.Namespace) -> list[Path]:
    files = list(options.files)
    for function in options.file_sets or []:
        files.extend(function())
    if files:
        return list(dict.fromkeys(files))
    files = [Path(line) for line in sys.stdin.read().splitlines() if line]
    if not files:
        raise SystemExit("no files given")
    return files
