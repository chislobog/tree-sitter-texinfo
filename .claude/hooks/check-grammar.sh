#!/usr/bin/env bash
# PostToolUse hook: after an edit to the grammar, the scanner or the corpus
# tests, regenerate the parser and run the corpus tests. On failure, exit 2 so
# the output is fed back to Claude.
set -uo pipefail

file=$(jq -r '.tool_input.file_path // .tool_response.filePath // empty')
root=${CLAUDE_PROJECT_DIR:-$(pwd)}

case "$file" in
  "$root/grammar.js" | "$root/src/scanner.c" | "$root"/test/corpus/*) ;;
  *) exit 0 ;;
esac

cd "$root" || exit 0

# Use the dev shell's tools if this process didn't inherit them.
run() {
  if command -v tree-sitter >/dev/null 2>&1; then
    "$@"
  else
    direnv exec "$root" "$@"
  fi
}

if ! output=$(run tree-sitter generate 2>&1); then
  printf 'tree-sitter generate failed:\n%s\n' "$output" >&2
  exit 2
fi

if ! output=$(run tree-sitter test 2>&1); then
  # Passing tests and section headers are noise; keep failures and the diff.
  # The diff colours expected lines green and actual lines red, one colour
  # code per run of lines; label every line before stripping the colours.
  printf 'tree-sitter test failed:\n%s\n' "$output" |
    awk '
      /^\x1b\[32m/ { label = "[expected] " }
      /^\x1b\[31m/ { label = "[actual]   " }
      { line = $0; gsub(/\x1b\[[0-9;]*m/, "", line); print label line }
      /\x1b\[0m$/ { label = "" }
    ' |
    grep -v -e '✓' -e '^  [a-z_-]*:$' -e '^correct / expected / unexpected$' >&2
  exit 2
fi

exit 0
