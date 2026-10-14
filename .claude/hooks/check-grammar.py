#!/usr/bin/env python3
"""PostToolUse hook: after an edit to the grammar, the scanner or the corpus
tests, regenerate the parser and run the corpus tests. On failure, exit 2 so
the output is fed back to Claude.

Reads the tool call as JSON on standard input, as Claude Code passes it.
"""

import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

COLOUR = re.compile(r"\x1b\[[0-9;]*m")
# Lines that are noise in a failure: passing tests, section headers, the
# legend of the diff.
NOISE = re.compile(r"✓|^  [a-z_-]*:$|^correct / expected / unexpected$")


def run(root: Path, *command: str) -> subprocess.CompletedProcess:
    """Runs `command` with the dev shell's tools, also when this process
    didn't inherit them."""
    if shutil.which("tree-sitter") is None:
        command = ("direnv", "exec", str(root), *command)
    try:
        return subprocess.run(command, cwd=root, capture_output=True, text=True, check=False)
    except FileNotFoundError as error:
        # Reported as a failure, which blocks: an uncaught exception exits
        # with 1, which Claude Code doesn't treat as one.
        return subprocess.CompletedProcess(
            command, 127, "", f"{error}: the dev shell's tools aren't on the path (direnv)\n"
        )


def labelled(output: str) -> list[str]:
    """The test output without colours, each line of the diff labelled: it
    colours expected lines green and actual lines red, one colour code per
    run of lines, and the reset code can end a line or start the next one."""
    lines = []
    label = ""
    for line in output.splitlines():
        if line.startswith("\x1b[0m"):
            line = line[len("\x1b[0m") :]
            label = ""
        if line.startswith("\x1b[32m"):
            label = "[expected] "
        elif line.startswith("\x1b[31m"):
            label = "[actual]   "
        reset = line.endswith("\x1b[0m")
        line = label + COLOUR.sub("", line)
        if not NOISE.search(line):
            lines.append(line)
        if reset:
            label = ""
    return lines


def main() -> int:
    event = json.load(sys.stdin)
    file = event.get("tool_input", {}).get("file_path") or event.get("tool_response", {}).get(
        "filePath", ""
    )
    root = Path(os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())).resolve()
    path = (root / file).resolve()
    watched = path in (root / "grammar.js", root / "src" / "scanner.c") or (
        path.parent == root / "test" / "corpus"
    )
    if not watched:
        return 0

    generated = run(root, "tree-sitter", "generate")
    if generated.returncode != 0:
        print(
            f"tree-sitter generate failed:\n{generated.stdout}{generated.stderr}", file=sys.stderr
        )
        return 2
    tested = run(root, "tree-sitter", "test")
    if tested.returncode != 0:
        print("tree-sitter test failed:", file=sys.stderr)
        print("\n".join(labelled(tested.stdout + tested.stderr)), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
