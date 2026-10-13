#!/usr/bin/env python3
"""Check that the scanner agrees with the grammar where only names tie them.

- The scanner's `TokenType` enum lists the grammar's externals in their
  order: each external's name, without its leading `_` and in capitals, is
  the enum's name for it. A family of externals (`_definition_start_3`,
  `_node_before_chapter`) takes the gap the enum leaves after its first
  name (`NODE_BEFORE`), so its size must be the gap.
- Each definition block's layout (`KIND_DEFINITION + N` in `BLOCKS`) is the
  one whose line commands include its `x` form (`@deffn`, `@deffnx`).
- Each sectioning command's level (`SECTIONING`) is that of the section
  whose heading has it.
- The tables of command names don't overlap, nor end in `index` (which
  makes a line command), so the order in which the scanner tries them
  doesn't matter; and the accents without braces are brace commands.

Usage: script/check-scanner.py [--scanner FILE] [--grammar FILE]
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def c_strings(source: str, table: str) -> list[str]:
    """The string literals in the C array `table`."""
    match = re.search(rf"\b{table}\[\]\s*=\s*\{{(.*?)\n?\}};", source, re.S)
    if not match:
        raise SystemExit(f"no table {table} in the scanner")
    return re.findall(r'"([^"]*)"', match.group(1))


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\([^)]*\)\s*\{{(.*?)\n\}}", source, re.S)
    if not match:
        raise SystemExit(f"no function {name} in the scanner")
    return match.group(1)


def token_enum(source: str) -> list[tuple[str, int]]:
    """The names of `enum TokenType` with their values."""
    match = re.search(r"enum TokenType \{(.*?)\};", source, re.S)
    if not match:
        raise SystemExit("no enum TokenType in the scanner")
    body = re.sub(r"//[^\n]*", "", match.group(1))
    values: dict[str, int] = {}
    entries = []
    value = 0
    for entry in filter(None, (e.strip() for e in body.split(","))):
        name, _, expression = (part.strip() for part in entry.partition("="))
        if expression:
            base, _, offset = (part.strip() for part in expression.partition("+"))
            value = values[base] + int(offset or 0)
        values[name] = value
        entries.append((name, value))
        value += 1
    return entries


def string_choices(rule) -> list[str]:
    """The strings a rule's `name` field may be, without their `@`."""
    found = []
    stack = [rule]
    while stack:
        node = stack.pop()
        if node.get("type") == "STRING":
            found.append(node["value"].removeprefix("@"))
        stack.extend(node.get("members", []))
        if "content" in node:
            stack.append(node["content"])
    return found


def name_field(rule) -> dict:
    """The content of the first `name` field in a rule."""
    stack = [rule]
    while stack:
        node = stack.pop(0)
        if node.get("type") == "FIELD" and node["name"] == "name":
            return node["content"]
        stack.extend(node.get("members", []))
        if "content" in node:
            stack.append(node["content"])
    raise SystemExit("a rule without a name field")


def check_tokens(source: str, grammar: dict) -> list[str]:
    externals = [e["name"] for e in grammar["externals"]]
    entries = token_enum(source)
    problems = []
    for index, (name, value) in enumerate(entries):
        end = entries[index + 1][1] if index + 1 < len(entries) else value + 1
        members = externals[value:end]
        if len(members) != end - value:
            problems.append(f"{name}: the grammar has no external {value}")
            continue
        family = end - value > 1
        for position, external in enumerate(members, value):
            expected = external.lstrip("_").upper()
            if expected != name and not (family and expected.startswith(name + "_")):
                problems.append(
                    f"{name} = {value}, but the grammar's external {position} is {external}"
                )
                break
    if entries and len(externals) != entries[-1][1] + 1:
        problems.append(
            f"the grammar has {len(externals)} externals, the scanner {entries[-1][1] + 1}"
        )
    return problems


def check_layouts(source: str, grammar: dict) -> list[str]:
    layouts = [
        set(string_choices(name_field(member)))
        for member in grammar["rules"]["_definition_continuation"]["members"]
    ]
    problems = []
    for block, offset in re.findall(r'\{"(\w+)", KIND_DEFINITION(?: \+ (\d+))?\}', source):
        layout = int(offset or 0)
        if layout >= len(layouts) or f"{block}x" not in layouts[layout]:
            right = [i for i, lines in enumerate(layouts) if f"{block}x" in lines]
            problems.append(f"@{block} has layout {layout}, but @{block}x is in layout {right}")
    return problems


def check_sections(source: str, grammar: dict) -> list[str]:
    sections = [
        name.removeprefix("_node_before_")
        for name in (e["name"] for e in grammar["externals"])
        if name.startswith("_node_before_")
    ]
    match = re.search(r"\} SECTIONING\[\] = \{(.*?)\};", source, re.S)
    if not match:
        raise SystemExit("no table SECTIONING in the scanner")
    problems = []
    for command, level in re.findall(r'\{"(\w+)", (\d+)\}', match.group(1)):
        level = int(level)
        heading = (
            grammar["rules"].get(f"{sections[level]}_heading") if level < len(sections) else None
        )
        if heading is None or command not in string_choices(name_field(heading)):
            problems.append(f"@{command} has level {level}, which isn't its section's")
    return problems


def check_names(source: str) -> list[str]:
    tables = {
        "LINE_COMMANDS": set(c_strings(source, "LINE_COMMANDS")),
        "PARAGRAPH_ENDING_LINE_COMMANDS": set(c_strings(source, "PARAGRAPH_ENDING_LINE_COMMANDS")),
        "BRACE_COMMANDS": set(c_strings(source, "BRACE_COMMANDS")),
        "BLOCKS": set(re.findall(r'\{"(\w+)", KIND_\w+(?: \+ \d+)?\}', source)),
        "SECTIONING": set(re.findall(r'\{"(\w+)", \d+\}', source)),
        "command_token": set(re.findall(r'"(\w+)"', function_body(source, "command_token"))),
        "scan": {"end", "node"},
    }
    problems = []
    names = list(tables)
    for i, first in enumerate(names):
        for second in names[i + 1 :]:
            for name in sorted(tables[first] & tables[second]):
                problems.append(f"@{name} is in both {first} and {second}")
        if first not in ("LINE_COMMANDS", "PARAGRAPH_ENDING_LINE_COMMANDS"):
            for name in sorted(n for n in tables[first] if len(n) > 5 and n.endswith("index")):
                problems.append(f"@{name} in {first} ends in `index`, which makes a line command")
    for name in sorted(set(c_strings(source, "LETTER_ACCENTS")) - tables["BRACE_COMMANDS"]):
        problems.append(f"the accent @{name} isn't in BRACE_COMMANDS")
    return problems


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    arguments.add_argument("--scanner", type=Path, default=ROOT / "src" / "scanner.c")
    arguments.add_argument("--grammar", type=Path, default=ROOT / "src" / "grammar.json")
    options = arguments.parse_args()
    source = options.scanner.read_text()
    grammar = json.loads(options.grammar.read_text())
    problems = [
        *check_tokens(source, grammar),
        *check_layouts(source, grammar),
        *check_sections(source, grammar),
        *check_names(source),
    ]
    for problem in problems:
        print(problem)
    if problems:
        print(f"{options.scanner} doesn't agree with {options.grammar}")
        return 1
    print("The scanner agrees with the grammar.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
