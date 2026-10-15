// The scanner, for the fuzzer, with checks of each token it returns that no
// tree shows: the token is valid where it is returned, no newline is left
// in the padding before it (but before an accent's argument on the next
// line), a zero-width token is zero-width, and nothing is read past the end
// of the file. A scan that reads past a line end and then fails, leaving
// what follows to a later part of the scan, breaks the first two.
#define tree_sitter_texinfo_external_scanner_scan unchecked_scan
#include "../../src/scanner.c"
#undef tree_sitter_texinfo_external_scanner_scan

#include <stdio.h>

bool tree_sitter_texinfo_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols);

typedef struct {
  TSLexer lexer;
  TSLexer *checked;
  // Characters read since the last skipped one: the token's text so far.
  unsigned read;
  // Whether a newline was skipped, before the last `mark_end` if any: then
  // the padding before the token holds it.
  bool newline_skipped;
  bool newline_before_mark;
  bool marked;
  // The token's length at the last `mark_end`, or 0 if a skip came after it,
  // as tree-sitter then starts the token at that end.
  unsigned length_at_mark;
  bool past_end;
} CheckedLexer;

static void checked_advance(TSLexer *lexer, bool skip) {
  CheckedLexer *self = (CheckedLexer *)lexer;
  if (self->checked->eof(self->checked)) self->past_end = true;
  if (skip) {
    if (self->checked->lookahead == '\n') self->newline_skipped = true;
    self->read = 0;
    self->length_at_mark = 0;
  } else {
    self->read++;
  }
  self->checked->advance(self->checked, skip);
  self->lexer.lookahead = self->checked->lookahead;
}

static void checked_mark_end(TSLexer *lexer) {
  CheckedLexer *self = (CheckedLexer *)lexer;
  self->marked = true;
  self->length_at_mark = self->read;
  self->newline_before_mark = self->newline_skipped;
  self->checked->mark_end(self->checked);
}

static uint32_t checked_get_column(TSLexer *lexer) {
  CheckedLexer *self = (CheckedLexer *)lexer;
  return self->checked->get_column(self->checked);
}

static bool checked_eof(const TSLexer *lexer) {
  const CheckedLexer *self = (const CheckedLexer *)lexer;
  return self->checked->eof(self->checked);
}

static bool checked_at_range_start(const TSLexer *lexer) {
  const CheckedLexer *self = (const CheckedLexer *)lexer;
  return self->checked->is_at_included_range_start(self->checked);
}

static bool is_zero_width(enum TokenType token) {
  switch (token) {
    case END_OF_FILE:
    case BRACE_CLOSE_AT_PARAGRAPH_END:
    case BRACE_CLOSE_AT_BLOCK_END:
    case BRACE_CLOSE_AT_LINE_END:
    case MISSING_END:
    case PARAGRAPH_END_AT_LINE_START:
    case NEWLINE_BEFORE_COMMENTS:
    case WORD_GLUE:
    case MENU_ENTRY_START:
    case PARAGRAPH_END_BEFORE_COMMAND:
    case NO_PARAGRAPH_START:
      return true;
    default:
      return false;
  }
}

static void fail(const char *problem, TSSymbol token) {
  fprintf(stderr, "scanner: %s (token %u)\n", problem, token);
  abort();
}

bool tree_sitter_texinfo_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  CheckedLexer checked = {.lexer = *lexer, .checked = lexer};
  checked.lexer.advance = checked_advance;
  checked.lexer.mark_end = checked_mark_end;
  checked.lexer.get_column = checked_get_column;
  checked.lexer.eof = checked_eof;
  checked.lexer.is_at_included_range_start = checked_at_range_start;
  bool found = unchecked_scan(payload, &checked.lexer, valid_symbols);
  TSSymbol token = checked.lexer.result_symbol;
  lexer->result_symbol = token;
  if (checked.past_end) fail("read past the end of the file", token);
  if (!found) return false;
  if (!valid_symbols[token]) fail("returned a token not valid here", token);
  bool newline_padding =
      checked.marked ? checked.newline_before_mark : checked.newline_skipped;
  if (newline_padding && token != ACCENT_ARGUMENT) {
    fail("left a newline in the padding before a token", token);
  }
  unsigned length = checked.marked ? checked.length_at_mark : checked.read;
  if (is_zero_width((enum TokenType)token) && length > 0) {
    fail("gave a zero-width token a length", token);
  }
  return true;
}
