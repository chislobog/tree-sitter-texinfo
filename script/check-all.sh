#!/usr/bin/env bash
# Run locally what CI's check job runs, to check before pushing (CI minutes
# are limited). Each step's output goes to a log; all steps run, and the
# summary lists the ones that failed. About 7 minutes.
#
# Usage: script/check-all.sh [BASE]   (BASE for the parser size, default origin/main)
set -uo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root" || exit
if [[ -z ${IN_NIX_SHELL:-} ]]; then
  exec nix develop --command "$0" "$@"
fi

base=${1:-origin/main}
logs=$(mktemp -d "${TMPDIR:-/tmp}/check-all.XXXXXX")
# The manuals, sorted; arguments add tests to find, in the steps run by
# `bash -c`, which shellcheck doesn't see.
# shellcheck disable=SC2120
manuals() { find -L examples \( -name '*.texi' -o -name '*.texinfo' -o -name '*.txi' \) "$@" | sort; }
failed=()

step() {
  local name=$1
  shift
  local log="$logs/${name// /-}.log" start=$SECONDS
  if "$@" >"$log" 2>&1; then
    printf 'ok      %4ds  %s\n' $((SECONDS - start)) "$name"
  else
    printf 'FAILED  %4ds  %s (%s)\n' $((SECONDS - start)) "$name" "$log"
    failed+=("$name")
  fi
}

generated() {
  local before
  before=$(cat src/grammar.json src/node-types.json src/parser.c | shasum)
  tree-sitter generate &&
    [[ $(cat src/grammar.json src/node-types.json src/parser.c | shasum) == "$before" ]] ||
    { echo "src/ didn't match grammar.js; it is regenerated now"; return 1; }
}

scanner() {
  cc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Werror -Isrc -c src/scanner.c \
    -o "$logs/scanner.o"
}

fuzz_inputs() {
  # shellcheck disable=SC2119
  { find -L texinfo-tests -name '*.texi'; manuals; } | python3 script/fuzz-corpus.py &&
    tree-sitter test --file-name fuzz-inputs.txt >/dev/null &&
    tree-sitter fuzz --include texinfo-tests --iterations 50 &&
    tree-sitter fuzz --include examples --iterations 10
  local status=$?
  rm -f test/corpus/fuzz-inputs.txt
  return $status
}

git fetch --quiet origin 2>/dev/null || true
step "parser size" script/check-parser-size.sh "$base"
step "generated parser" generated
step "tests" tree-sitter test
step "lint" script/lint.sh
step "scanner warnings" scanner
step "incremental parsing" tree-sitter fuzz --iterations 100
step "checks catch" python3 script/test-checks.py
step "parse manuals" bash -c "$(declare -f manuals); manuals | python3 script/parse-examples.py"
step "compare manuals" bash -c "$(declare -f manuals); manuals ! -path '*/it/*' \
  ! -name texinfo-tex-test.texi ! -name html_default_commands_args.texi |
  python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt"
step "compare test inputs" bash -c "find -L texinfo-tests -name '*.texi' | sort |
  python3 script/compare-texi2any.py --hunks 0 --expected test/texi2any-differences.txt"
step "tree snapshots" bash -c "{ find -L texinfo-tests -name '*.texi'; $(declare -f manuals); manuals; } |
  sort | python3 script/tree-snapshots.py --check"
step "fuzz inputs" fuzz_inputs
step "editing simulation" bash -c "$(declare -f manuals); manuals ! -path '*/it/*' |
  python3 script/check-edits.py --edits 5 --seed 0"

grep -h '::warning::' "$logs"/*.log
if ((${#failed[@]})); then
  echo "${#failed[@]} failed: ${failed[*]}. Logs in $logs"
  exit 1
fi
echo "All passed. Logs in $logs"
