#!/usr/bin/env bash
# Fuzz the parser and scanner with libFuzzer, AddressSanitizer and
# UndefinedBehaviorSanitizer, as CI's tree-sitter/fuzz-action does, but for
# as long as you like and on several cores.
#
# Usage: script/fuzz-scanner.sh [SECONDS] [JOBS]   (default 600 and 4)
#
# Seeded with the corpus tests' inputs. Inputs that crash, leak or hang are
# saved in build/fuzz/artifacts/; replay one with
# `build/fuzz/fuzzer build/fuzz/artifacts/FILE`. Run it in the dev shell,
# whose clang is LLVM 23: LLVM 21's ASan deadlocks at startup on macOS 27.
set -euo pipefail

seconds=${1:-600}
jobs=${2:-4}
root=$(cd "$(dirname "$0")/.." && pwd)
dir="$root/build/fuzz"
ts=$(dirname "$(dirname "$(readlink -f "$(command -v tree-sitter)")")")

mkdir -p "$dir/artifacts" "$dir/out"

# The harness: parse each input as a whole document.
cat >"$dir/fuzzer.c" <<'C'
#include <stdint.h>
#include <stdlib.h>
#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_texinfo(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  TSParser *parser = ts_parser_new();
  if (!ts_parser_set_language(parser, tree_sitter_texinfo())) abort();
  TSTree *tree = ts_parser_parse_string(parser, NULL, (const char *)data,
                                        (uint32_t)size);
  ts_tree_delete(tree);
  ts_parser_delete(parser);
  return 0;
}
C

# As in fuzz-action: the serializer may be called with a null buffer.
cat >"$dir/ignorelist.ini" <<'INI'
[nonnull-attribute]
fun:tree_sitter_*_external_scanner_serialize
fun:tree_sitter_*_external_scanner_deserialize
INI

# A dictionary of the grammar's literal tokens, as fuzz-action makes.
python3 - "$root/src/grammar.json" >"$dir/dict" <<'PY'
import json, sys
words = set()
def walk(node):
    if isinstance(node, dict):
        anonymous = node.get("type") == "ALIAS" and node.get("named") is False
        if (node.get("type") == "STRING" or anonymous) and node.get("value"):
            words.add(node["value"])
        for value in node.values():
            walk(value)
    elif isinstance(node, list):
        for value in node:
            walk(value)
walk(json.load(open(sys.argv[1])))
for word in sorted(words):
    # libFuzzer's dictionaries only allow some escapes.
    quoted = json.dumps(word)
    if "\\" not in quoted and word.isascii():
        print(quoted)
PY

if [[ ! -x $dir/fuzzer || -n $(find "$root/src" -newer "$dir/fuzzer" -name '*.[ch]') ]]; then
  echo "Building the fuzzer (the parser takes a minute or two)..."
  clang -std=c11 -Og -g -I"$root/src" -I"$ts/include" \
    -fsanitize=fuzzer,address,undefined -fsanitize-ignorelist="$dir/ignorelist.ini" \
    "$dir/fuzzer.c" "$root/src/parser.c" "$root/src/scanner.c" \
    -L"$ts/lib" -ltree-sitter -Wl,-rpath,"$ts/lib" -o "$dir/fuzzer"
fi

python3 "$root/script/corpus-inputs.py" "$dir/seeds" >/dev/null
cd "$dir"
exec ./fuzzer out seeds -dict=dict -artifact_prefix=artifacts/ -timeout=10 \
  -max_len=4096 -max_total_time="$seconds" -fork="$jobs" -ignore_crashes=1
