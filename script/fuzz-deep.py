#!/usr/bin/env python3
"""Fuzz texi2any's test inputs and the manuals for longer than CI does, on
several cores.

Writes the corpus with script/fuzz-corpus.py, splits the inputs into --jobs
groups of about equal size, and runs `tree-sitter fuzz` on each group at
once; tree-sitter fuzz uses a single core. The manuals get --iterations,
texi2any's small test inputs ten times as many. Each fuzzer's output goes to
a log in --logs; a failure is reported with the seed that reproduces it
(`TREE_SITTER_SEED=SEED tree-sitter fuzz --include NAME ...`). The corpus
file is removed afterwards.

Usage: script/fuzz-deep.py [--jobs N] [--iterations N] [--edits N] [--logs DIR]
"""

import argparse
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from texinfo_language import ROOT

CORPUS = ROOT / "test" / "corpus" / "fuzz-inputs.txt"
MANUALS = ("*.texi", "*.texinfo", "*.txi")


def inputs() -> list[Path]:
    tests = sorted(Path("texinfo-tests").glob("**/*.texi"))
    manuals = sorted({p for pattern in MANUALS for p in Path("examples").glob(f"**/{pattern}")})
    return tests + manuals


def groups(paths: list[Path], count: int) -> list[list[Path]]:
    """Splits `paths` into `count` groups of about equal total size."""
    result: list[list[Path]] = [[] for _ in range(count)]
    sizes = [0] * count
    for path in sorted(paths, key=lambda p: -p.stat().st_size):
        smallest = sizes.index(min(sizes))
        result[smallest].append(path)
        sizes[smallest] += path.stat().st_size
    return [group for group in result if group]


def main() -> int:
    arguments = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    arguments.add_argument("--jobs", type=int, default=4, help="fuzzers at once (default 4)")
    arguments.add_argument("--iterations", type=int, default=100, help="per manual (default 100)")
    arguments.add_argument("--edits", type=int, default=10, help="per iteration (default 10)")
    arguments.add_argument(
        "--logs", type=Path, help="where to keep the logs (default: a temporary directory)"
    )
    options = arguments.parse_args()

    paths = inputs()
    subprocess.run(
        ["python3", "script/fuzz-corpus.py"],
        input="".join(f"{p}\n" for p in paths),
        text=True,
        cwd=ROOT,
        check=True,
    )
    logs = options.logs or Path(tempfile.mkdtemp(prefix="fuzz-"))
    logs.mkdir(parents=True, exist_ok=True)
    runs = []
    try:
        for kind, iterations in (
            ("texinfo-tests", options.iterations * 10),
            ("examples", options.iterations),
        ):
            selected = [p for p in paths if p.parts[0] == kind]
            jobs = max(1, round(options.jobs * (0.25 if kind == "texinfo-tests" else 0.75)))
            for number, group in enumerate(groups(selected, jobs), 1):
                pattern = "- (" + "|".join(re.escape(str(p)) for p in group) + ")$"
                log = logs / f"{kind}-{number}.log"
                command = [
                    "tree-sitter", "fuzz", f"--include={pattern}",
                    "--iterations", str(iterations), "--edits", str(options.edits),
                ]  # fmt: skip
                runs.append((log, len(group), subprocess.Popen(
                    command, cwd=ROOT, stdout=log.open("w"), stderr=subprocess.STDOUT
                )))  # fmt: skip
        print(f"{len(runs)} fuzzers on {len(paths)} inputs; logs in {logs}")
        start = time.monotonic()
        failed = 0
        for log, count, process in runs:
            process.wait()
            text = log.read_text(errors="replace")
            seed = re.search(r"Seed: (\d+)", text)
            failures = re.findall(
                r"^(?:Incorrect|.*failed fuzzing|thread .* panicked).*$", text, re.M
            )
            status = "ok" if process.returncode == 0 and not failures else "FAILED"
            failed += status == "FAILED"
            print(
                f"{status}: {log.name}, {count} inputs, seed {seed.group(1) if seed else '?'}"
                f", {time.monotonic() - start:.0f} s"
            )
            for failure in failures[:5]:
                print(f"  {failure[:200]}")
    finally:
        for _, _, process in runs:
            if process.poll() is None:
                process.kill()
        CORPUS.unlink(missing_ok=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
