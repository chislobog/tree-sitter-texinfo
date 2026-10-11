#!/usr/bin/env bash
# Run locally what CI's check job runs (the justfile's recipes), to check
# before pushing. Each step's output goes to a log; all steps run, and the
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

git fetch --quiet origin 2>/dev/null || true
# The recipes in the justfile, as CI runs them.
step "parser size" just parser-size "$base"
step "generated parser" just generated
step "tests" just test
step "lint" just lint
step "scanner warnings" just scanner
step "incremental parsing" just fuzz
step "fuzz seeds" just fuzz-seeds
step "checks catch" just checks-catch
step "parse manuals" just parse
step "compare manuals" just compare-manuals
step "compare test inputs" just compare-tests
step "tree snapshots" just snapshots
step "fuzz inputs" just fuzz-inputs
step "editing simulation" just edits

grep -h '::warning::' "$logs"/*.log
if ((${#failed[@]})); then
  echo "${#failed[@]} failed: ${failed[*]}. Logs in $logs"
  exit 1
fi
echo "All passed. Logs in $logs"
