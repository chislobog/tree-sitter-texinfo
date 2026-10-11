#!/usr/bin/env python3
"""Write a libFuzzer dictionary of the grammar's literal tokens, as
tree-sitter's fuzz-action makes: the strings in src/grammar.json, without
those libFuzzer's dictionary format can't hold.

Usage: script/fuzz-dict.py > dict
"""

import json
import sys
from pathlib import Path

GRAMMAR = Path(__file__).resolve().parent.parent / "src" / "grammar.json"


def strings(node, found: set[str]):
    if isinstance(node, dict):
        anonymous = node.get("type") == "ALIAS" and node.get("named") is False
        if (node.get("type") == "STRING" or anonymous) and node.get("value"):
            found.add(node["value"])
        for value in node.values():
            strings(value, found)
    elif isinstance(node, list):
        for value in node:
            strings(value, found)


def main() -> int:
    found: set[str] = set()
    strings(json.loads(GRAMMAR.read_text()), found)
    for word in sorted(found):
        # libFuzzer's dictionaries only allow some escapes.
        quoted = json.dumps(word)
        if "\\" not in quoted and word.isascii():
            print(quoted)
    return 0


if __name__ == "__main__":
    sys.exit(main())
