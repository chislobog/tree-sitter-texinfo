"""This grammar's parser for the scripts in script/, and a full dump of a tree.

The parser is built into build/ when src/ is newer than the build, or into
another checkout's build/ for comparing with an earlier revision.
"""

import ctypes
import subprocess
from collections.abc import Iterator
from pathlib import Path

from tree_sitter import Language, Node

ROOT = Path(__file__).resolve().parent.parent


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
