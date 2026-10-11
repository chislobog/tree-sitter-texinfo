# The checks, each defined once: CI (.github/workflows/ci.yml),
# script/check-all.sh and CLAUDE.md call these recipes. `just` alone lists
# them. Run in the dev shell, which provides just and the tools.

set shell := ["bash", "-euo", "pipefail", "-c"]

texinfo := "\\( -name '*.texi' -o -name '*.texinfo' -o -name '*.txi' \\)"
# The example manuals and texi2any's test inputs, one name per line.
manuals := "find -L examples " + texinfo
test_inputs := "find -L texinfo-tests -name '*.texi'"
# Manuals texi2any itself fails on, and a fragment meant for @include.
compared_manuals := manuals + " ! -path '*/it/*' ! -name texinfo-tex-test.texi ! -name html_default_commands_args.texi"

# List the recipes.
default:
    @just --list --unsorted

# Everything CI's check job runs, with a summary of what fails.
check *base:
    script/check-all.sh {{ base }}

# The parser's state count against BASE: warns at +10%, fails at +50%.
parser-size base="origin/main":
    script/check-parser-size.sh {{ base }}

# src/ is what `tree-sitter generate` makes of grammar.js.
generated:
    #!/usr/bin/env bash
    set -euo pipefail
    before=$(cat src/grammar.json src/node-types.json src/parser.c | shasum)
    tree-sitter generate
    after=$(cat src/grammar.json src/node-types.json src/parser.c | shasum)
    [[ $after == "$before" ]] || { echo "src/ didn't match grammar.js; it is regenerated now" >&2; exit 1; }

# Corpus, highlight and tags tests.
test *args:
    tree-sitter test {{ args }}

# Linters, formatters and the query checks.
lint:
    script/lint.sh

# Apply the formatters and the linters' fixes.
fix:
    script/lint.sh --fix

# The scanner compiles without warnings.
scanner:
    cc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc -c src/scanner.c -o "${TMPDIR:-/tmp}/scanner.o"

# Incremental parsing after random edits of the corpus tests.
fuzz iterations="100":
    tree-sitter fuzz --iterations {{ iterations }}

# CI's scanner fuzzing seeds, written with a plain python3 as there.
fuzz-seeds:
    env -i PATH=/usr/bin:/bin python3 script/corpus-inputs.py "$(mktemp -d)"

# Each check still fails on the inputs in test/checks/ it is meant to catch.
checks-catch:
    python3 script/test-checks.py

# Only the manuals in test/parse-errors.txt fail to parse, none slowly.
parse:
    {{ manuals }} | sort | python3 script/parse-examples.py

# Differences from texi2any are exactly those in test/texi2any-differences.txt.
compare: compare-manuals compare-tests

# The comparison on the manuals.
compare-manuals *args:
    {{ compared_manuals }} | sort | python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt {{ args }}

# texi2any's test inputs; only the summary line, as each file prints one.
compare-tests *args:
    {{ test_inputs }} | sort | python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt {{ args }} | tail -1

# Every tree is as recorded in test/tree-snapshots.txt.
snapshots:
    { {{ manuals }}; {{ test_inputs }}; } | sort | python3 script/tree-snapshots.py --check

# Record the trees, after reviewing a deliberate change with snapshots-diff.
snapshots-write:
    { {{ manuals }}; {{ test_inputs }}; } | sort | python3 script/tree-snapshots.py --write

# Show how the trees of FILES differ from those at git revision REV.
snapshots-diff rev="HEAD" *files:
    python3 script/tree-snapshots.py --diff {{ rev }} {{ files }}

# Incremental parsing after random edits of the test inputs and manuals.
fuzz-inputs:
    #!/usr/bin/env bash
    set -euo pipefail
    trap 'rm -f test/corpus/fuzz-inputs.txt' EXIT
    { {{ test_inputs }}; {{ manuals }}; } | sort | python3 script/fuzz-corpus.py
    tree-sitter test --file-name fuzz-inputs.txt >/dev/null
    tree-sitter fuzz --include texinfo-tests --iterations 50
    tree-sitter fuzz --include examples --iterations 10

# Typing mistakes in the manuals stay local, and incremental parsing matches.
edits seed="0" count="5":
    {{ manuals }} ! -path '*/it/*' | sort | python3 script/check-edits.py --edits {{ count }} --seed {{ seed }}

# Longer tree-sitter fuzzing than CI, on several cores.
fuzz-deep *args:
    python3 script/fuzz-deep.py {{ args }}

# libFuzzer with sanitizers on the scanner, on several cores.
fuzz-scanner seconds="600" jobs="4":
    script/fuzz-scanner.sh {{ seconds }} {{ jobs }}

# Regenerate the queries for other editors from queries/.
editor-queries:
    python3 script/editor-queries.py
