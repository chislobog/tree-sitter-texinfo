// The libFuzzer target for the parser and scanner: each input is parsed as
// a whole document, as tree-sitter's fuzz-action does.
#include <stdint.h>
#include <stdlib.h>
#include <tree_sitter/api.h>

const TSLanguage *tree_sitter_texinfo(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  TSParser *parser = ts_parser_new();
  if (!ts_parser_set_language(parser, tree_sitter_texinfo())) abort();
  TSTree *tree =
      ts_parser_parse_string(parser, NULL, (const char *)data, (uint32_t)size);
  ts_tree_delete(tree);
  ts_parser_delete(parser);
  return 0;
}
