#!/usr/bin/env bash
# Runs the linters and formatter checks. With --fix, applies the formatters
# and the linters' automatic fixes instead.
#
# Usage: script/lint.sh [--fix]
set -uo pipefail

cd "$(git rev-parse --show-toplevel)" || exit 1

# Use the dev shell's tools if this process didn't inherit them all.
for tool in clang-format ruff shellcheck actionlint nixfmt ts_query_ls tree-sitter npx just; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    if [[ -z ${LINT_IN_DEV_SHELL:-} ]]; then
      LINT_IN_DEV_SHELL=1 exec direnv exec . "$0" "$@"
    fi
    echo "lint: $tool not found, even in the dev shell" >&2
    exit 1
  fi
done
# ESLint and its tree-sitter config are npm dev dependencies.
[[ -d node_modules ]] || npm ci --no-audit --no-fund >/dev/null

fix=false
[[ ${1:-} == --fix ]] && fix=true
status=0

check() {
  local name=$1
  shift
  if ! "$@"; then
    echo "lint: $name failed" >&2
    status=1
  fi
}

if $fix; then
  npx eslint --fix grammar.js
  clang-format -i src/scanner.c
  ruff check --fix script/ && ruff format script/
  nixfmt flake.nix
  just --fmt --unstable
  tree-sitter build >/dev/null && ts_query_ls format queries/
  python3 script/editor-queries.py
else
  check eslint npx eslint grammar.js
  check clang-format clang-format --dry-run --Werror src/scanner.c
  check ruff ruff check script/
  check "ruff format" ruff format --check script/
  check shellcheck shellcheck .githooks/* .claude/hooks/*.sh script/*.sh
  check nixfmt nixfmt --check flake.nix
  check "just --fmt" just --fmt --check --unstable
  check actionlint actionlint
  # queries/ against Helix's capture names (.tsqueryrc.json); the copies
  # generated for other editors against theirs.
  check ts_query_ls bash -c 'tree-sitter build >/dev/null && ts_query_ls check -f queries/'
  check "editor queries" python3 script/editor-queries.py --check
  check "ts_query_ls neovim" ts_query_ls check -f \
    -c "$(cat editors/neovim/tsqueryrc.json)" editors/neovim/queries
fi
exit $status
