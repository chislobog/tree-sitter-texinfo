#!/usr/bin/env bash
# Compares the parser's state count with an earlier commit's: warns when it
# grows by 10% or more, fails at 50% or more. Each feature so far added 20-35%,
# while splitting arguments into fields multiplied it by 8.6, so large jumps
# deserve a look before they land.
#
# Usage: script/check-parser-size.sh BASE_COMMIT
set -euo pipefail

base=${1:?usage: $0 BASE_COMMIT}
count() { grep -E '^#define STATE_COUNT ' | awk '{print $3}'; }

if ! old=$(git show "$base:src/parser.c" 2>/dev/null | count) || [ -z "$old" ]; then
  echo "No parser at $base to compare with."
  exit 0
fi
new=$(count < src/parser.c)
change=$(( (new - old) * 100 / old ))
size=$(( $(wc -c < src/parser.c) / 1048576 ))

echo "Parser states: $old -> $new ($change%), parser.c $size MB"
if (( change >= 50 )); then
  echo "::error::The parser's state count grew by $change%. Check the grammar change for a combinatorial blowup."
  exit 1
elif (( change >= 10 )); then
  echo "::warning::The parser's state count grew by $change% (parser.c is $size MB)."
fi
