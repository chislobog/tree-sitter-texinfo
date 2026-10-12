# Short names for the checks and tools, each one line. The checks are
# defined in flake.nix (`nix flake check`), the formatters there too
# (`nix fmt`), the git hooks with pre-commit (git-hooks.nix). `just` alone
# lists the recipes.

# List the recipes.
default:
    @just --list --unsorted

# Every check, cached by Nix: only those whose inputs changed run.
check:
    nix flake check -L

# One check of `nix flake check`, by name (`just run compare-manuals`).
run name:
    nix build --no-link -L ".#checks.$(nix config show system).{{ name }}"

# Format every file.
fmt:
    nix fmt

# The corpus, highlight and tags tests, directly (faster while editing).
test *args:
    tree-sitter test {{ args }}

# Record the trees after a deliberate change, reviewed with snapshots-diff.
snapshots-write:
    python3 script/tree-snapshots.py --write --manuals --test-inputs

# Show how the trees of FILES differ from those at git revision REV.
snapshots-diff rev="HEAD" *files:
    python3 script/tree-snapshots.py --diff {{ rev }} {{ files }}

# Record the differences from texi2any, then give each new file a reason.
compare-write:
    python3 script/compare-texi2any.py --hunks 0 --write-expected test/texi2any-differences.txt --compared-manuals --test-inputs

# Simulate typing mistakes in the manuals with another seed.
edits seed="0" count="5":
    python3 script/check-edits.py --edits {{ count }} --seed {{ seed }} --edited-manuals

# Longer tree-sitter fuzzing than the checks, on several cores.
fuzz-deep *args:
    python3 script/fuzz-deep.py {{ args }}

# libFuzzer with sanitizers on the scanner, for longer than the check.
fuzz-scanner seconds="600" jobs="4":
    nix run .#fuzz-scanner -- {{ seconds }} {{ jobs }}

# The parser's state count against an earlier commit.
parser-size base="origin/main":
    python3 script/parser-size.py {{ base }}

# Regenerate the queries for other editors from queries/.
editor-queries:
    python3 script/editor-queries.py
