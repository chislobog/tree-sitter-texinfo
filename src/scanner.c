/**
 * External scanner for Texinfo.
 *
 * Keeps a stack of open blocks so that `@end NAME` only closes the block that
 * is actually open, and scans the bodies of raw blocks (`@verbatim`,
 * `@ignore`, `@macro`, `@tex`, ...) and of `@verb{xTEXTx}` as plain text.
 * Also closes unclosed brace commands where texi2any does, so that an
 * unclosed brace doesn't swallow the rest of the document.
 *
 * Block names live here rather than in grammar.js: the scanner has to
 * recognise them to know what to push.
 */

#include "tree_sitter/parser.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum TokenType {
  BLOCK_START,
  LIST_START,
  RAW_BLOCK_START,
  RAW_TEXT,
  BLOCK_END,
  END_OF_FILE,
  VERB_OPEN,
  VERB_TEXT,
  VERB_CLOSE,
  // Zero-width closes for an unclosed brace command: before a blank line or
  // the end of the file, and before the `@end` of the innermost open block.
  BRACE_CLOSE_AT_PARAGRAPH_END,
  BRACE_CLOSE_AT_BLOCK_END,
  // Zero-width end of a block still open at the end of the file.
  MISSING_END,
  ERROR_SENTINEL,
};

typedef enum {
  KIND_BLOCK,
  // Body is a sequence of `@item`s.
  KIND_LIST,
  // Body is plain text. A nested block of the same name is counted.
  KIND_RAW,
  // Body is plain text. Nested macro definitions of any kind are counted.
  KIND_MACRO,
} BlockKind;

typedef struct {
  const char *name;
  BlockKind kind;
} Block;

// Classes follow `tta/data/command_data.txt` in the Texinfo sources.
static const Block BLOCKS[] = {
    // Preformatted and display
    {"example", KIND_BLOCK},
    {"smallexample", KIND_BLOCK},
    {"lisp", KIND_BLOCK},
    {"smalllisp", KIND_BLOCK},
    {"display", KIND_BLOCK},
    {"smalldisplay", KIND_BLOCK},
    {"format", KIND_BLOCK},
    {"smallformat", KIND_BLOCK},
    {"quotation", KIND_BLOCK},
    {"smallquotation", KIND_BLOCK},
    {"indentedblock", KIND_BLOCK},
    {"smallindentedblock", KIND_BLOCK},
    {"cartouche", KIND_BLOCK},
    {"flushleft", KIND_BLOCK},
    {"flushright", KIND_BLOCK},
    {"raggedright", KIND_BLOCK},
    {"group", KIND_BLOCK},
    {"float", KIND_BLOCK},
    // Document regions
    {"titlepage", KIND_BLOCK},
    {"copying", KIND_BLOCK},
    {"documentdescription", KIND_BLOCK},
    {"documentinfo", KIND_BLOCK},
    {"publication", KIND_BLOCK},
    {"nodedescriptionblock", KIND_BLOCK},
    // Math: texi2any parses commands inside, so not raw.
    {"displaymath", KIND_BLOCK},
    // Menus
    {"menu", KIND_BLOCK},
    {"detailmenu", KIND_BLOCK},
    {"direntry", KIND_BLOCK},
    // Conditionals
    {"ifinfo", KIND_BLOCK},
    {"ifnotinfo", KIND_BLOCK},
    {"ifhtml", KIND_BLOCK},
    {"ifnothtml", KIND_BLOCK},
    {"iftex", KIND_BLOCK},
    {"ifnottex", KIND_BLOCK},
    {"ifplaintext", KIND_BLOCK},
    {"ifnotplaintext", KIND_BLOCK},
    {"ifdocbook", KIND_BLOCK},
    {"ifnotdocbook", KIND_BLOCK},
    {"ifxml", KIND_BLOCK},
    {"ifnotxml", KIND_BLOCK},
    {"iflatex", KIND_BLOCK},
    {"ifnotlatex", KIND_BLOCK},
    {"ifset", KIND_BLOCK},
    {"ifclear", KIND_BLOCK},
    {"ifcommanddefined", KIND_BLOCK},
    {"ifcommandnotdefined", KIND_BLOCK},
    // Definitions
    {"defblock", KIND_BLOCK},
    {"deffn", KIND_BLOCK},
    {"defvr", KIND_BLOCK},
    {"deftypefn", KIND_BLOCK},
    {"deftypevr", KIND_BLOCK},
    {"deftp", KIND_BLOCK},
    {"defop", KIND_BLOCK},
    {"deftypeop", KIND_BLOCK},
    {"defcv", KIND_BLOCK},
    {"deftypecv", KIND_BLOCK},
    {"defun", KIND_BLOCK},
    {"defmac", KIND_BLOCK},
    {"defspec", KIND_BLOCK},
    {"defvar", KIND_BLOCK},
    {"defopt", KIND_BLOCK},
    {"deftypefun", KIND_BLOCK},
    {"deftypevar", KIND_BLOCK},
    {"defmethod", KIND_BLOCK},
    {"deftypemethod", KIND_BLOCK},
    {"defivar", KIND_BLOCK},
    {"deftypeivar", KIND_BLOCK},
    // Lists
    {"itemize", KIND_LIST},
    {"enumerate", KIND_LIST},
    {"table", KIND_LIST},
    {"ftable", KIND_LIST},
    {"vtable", KIND_LIST},
    {"multitable", KIND_LIST},
    // Raw
    {"verbatim", KIND_RAW},
    {"ignore", KIND_RAW},
    {"tex", KIND_RAW},
    {"latex", KIND_RAW},
    {"html", KIND_RAW},
    {"xml", KIND_RAW},
    {"docbook", KIND_RAW},
    {"macro", KIND_MACRO},
    {"rmacro", KIND_MACRO},
    {"linemacro", KIND_MACRO},
};

#define BLOCK_COUNT (sizeof(BLOCKS) / sizeof(BLOCKS[0]))

// Longest block name is "nodedescriptionblock"; longer words never match.
#define MAX_NAME 32

// Deepest nesting tracked inside a single raw block body.
#define MAX_RAW_DEPTH 64

// Serialized state: the `@verb` delimiter, then the block stack.
#define MAX_DEPTH (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - sizeof(int32_t))

typedef struct {
  // Indices into BLOCKS, innermost last.
  uint8_t stack[MAX_DEPTH];
  unsigned depth;
  // Delimiter of the `@verb` being scanned, from its opening to its closing.
  int32_t verb_delimiter;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static inline bool is_alpha(int32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }

static inline bool is_blank(int32_t c) { return c == ' ' || c == '\t'; }

static void skip_blanks(TSLexer *lexer) {
  while (is_blank(lexer->lookahead)) advance(lexer);
}

static void skip_to_next_line(TSLexer *lexer) {
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') advance(lexer);
  if (lexer->lookahead == '\n') advance(lexer);
}

/**
 * Reads a command or block name: a letter followed by letters, digits, `-`
 * or `_` (the characters `texi2any` accepts after `@end`). Returns false if
 * the word is empty or too long to be a known name.
 */
static bool read_name(TSLexer *lexer, char *name) {
  unsigned length = 0;
  if (!is_alpha(lexer->lookahead)) return false;
  while (is_alpha(lexer->lookahead) || is_digit(lexer->lookahead) ||
         lexer->lookahead == '-' || lexer->lookahead == '_') {
    if (length < MAX_NAME) name[length] = (char)lexer->lookahead;
    length++;
    advance(lexer);
  }
  if (length >= MAX_NAME) return false;
  name[length] = '\0';
  return true;
}

static int find_block(const char *name) {
  for (unsigned i = 0; i < BLOCK_COUNT; i++) {
    if (strcmp(BLOCKS[i].name, name) == 0) return (int)i;
  }
  return -1;
}

/**
 * After `@end`, reads the separating blanks and the block name. Returns false
 * if there is no name.
 */
static bool read_end_name(TSLexer *lexer, char *name) {
  if (!is_blank(lexer->lookahead)) return false;
  skip_blanks(lexer);
  return read_name(lexer, name);
}

/** Pops the innermost block, whose `@end NAME` has just been read. */
static bool pop_block(Scanner *scanner, TSLexer *lexer) {
  scanner->depth--;
  lexer->mark_end(lexer);
  lexer->result_symbol = BLOCK_END;
  return true;
}

/**
 * Scans a block opener such as `@example` at the start of a line. The `@` has
 * already been consumed.
 */
static bool scan_block_start(Scanner *scanner, TSLexer *lexer,
                             const bool *valid_symbols, const char *name) {
  int index = find_block(name);
  if (index < 0) return false;

  enum TokenType token;
  switch (BLOCKS[index].kind) {
    case KIND_BLOCK:
      token = BLOCK_START;
      break;
    case KIND_LIST:
      token = LIST_START;
      break;
    default:
      token = RAW_BLOCK_START;
      break;
  }
  if (!valid_symbols[token]) return false;
  if (scanner->depth >= MAX_DEPTH) return false;

  scanner->stack[scanner->depth++] = (uint8_t)index;
  lexer->mark_end(lexer);
  lexer->result_symbol = token;
  return true;
}

/**
 * Scans `@end NAME` for the innermost open block. The `@` has already been
 * consumed and `name` holds the word after it.
 */
static bool scan_block_end(Scanner *scanner, TSLexer *lexer, const char *name) {
  if (strcmp(name, "end") != 0 || scanner->depth == 0) return false;

  char end_name[MAX_NAME + 1];
  if (!read_end_name(lexer, end_name)) return false;
  if (strcmp(end_name, BLOCKS[scanner->stack[scanner->depth - 1]].name) != 0) {
    return false;
  }
  return pop_block(scanner, lexer);
}

/**
 * Scans the body of the innermost raw block, line by line, up to (but not
 * including) the line holding its `@end`. Follows `texi2any`: nested blocks
 * of the same name are counted, and inside macro definitions nested
 * `@macro`, `@rmacro` and `@linemacro` are tracked on a stack.
 *
 * If the body is empty, scans the `@end` line itself as BLOCK_END.
 */
static bool scan_raw(Scanner *scanner, TSLexer *lexer) {
  if (scanner->depth == 0) return false;
  const Block *block = &BLOCKS[scanner->stack[scanner->depth - 1]];

  // Names expected by nested `@end`s, innermost last.
  const char *nested[MAX_RAW_DEPTH];
  unsigned nested_depth = 0;
  bool has_text = false;

  lexer->mark_end(lexer);
  while (!lexer->eof(lexer)) {
    skip_blanks(lexer);
    if (lexer->lookahead == '@') {
      advance(lexer);
      char name[MAX_NAME + 1];
      if (read_name(lexer, name)) {
        char end_name[MAX_NAME + 1];
        if (strcmp(name, "end") == 0 && read_end_name(lexer, end_name)) {
          const char *expected =
              nested_depth > 0 ? nested[nested_depth - 1] : block->name;
          if (strcmp(end_name, expected) == 0) {
            if (nested_depth > 0) {
              nested_depth--;
            } else if (has_text) {
              lexer->result_symbol = RAW_TEXT;
              return true;
            } else {
              return pop_block(scanner, lexer);
            }
          }
        } else if (nested_depth < MAX_RAW_DEPTH &&
                   (is_blank(lexer->lookahead) || lexer->lookahead == '\n' ||
                    lexer->lookahead == '\r' || lexer->lookahead == '@')) {
          // `texi2any` requires a separator after a nested opener.
          if (block->kind == KIND_MACRO) {
            int index = find_block(name);
            if (index >= 0 && BLOCKS[index].kind == KIND_MACRO) {
              nested[nested_depth++] = BLOCKS[index].name;
            }
          } else if (strcmp(name, block->name) == 0) {
            // KIND_RAW, the only other kind with a raw body.
            nested[nested_depth++] = block->name;
          }
        }
      }
    }
    skip_to_next_line(lexer);
    has_text = true;
    lexer->mark_end(lexer);
  }

  // Unclosed at end of file; the missing `@end` is a parse error.
  if (!has_text) return false;
  lexer->result_symbol = RAW_TEXT;
  return true;
}

/**
 * Scans `@verb{xTEXTx}` after its `{`: the delimiter `x`, the text up to the
 * first `x}`, and the closing `x`. The text may contain `@`, braces and
 * newlines.
 */
static bool scan_verb(Scanner *scanner, TSLexer *lexer,
                      const bool *valid_symbols) {
  if (valid_symbols[VERB_OPEN]) {
    if (lexer->eof(lexer)) return false;
    scanner->verb_delimiter = lexer->lookahead;
    advance(lexer);
    lexer->mark_end(lexer);
    lexer->result_symbol = VERB_OPEN;
    return true;
  }

  bool has_text = false;
  while (!lexer->eof(lexer)) {
    if (lexer->lookahead == scanner->verb_delimiter) {
      lexer->mark_end(lexer);
      advance(lexer);
      if (lexer->lookahead == '}') {
        if (has_text) {
          lexer->result_symbol = VERB_TEXT;
          return true;
        }
        lexer->mark_end(lexer);
        scanner->verb_delimiter = 0;
        lexer->result_symbol = VERB_CLOSE;
        return true;
      }
    } else {
      advance(lexer);
    }
    has_text = true;
  }

  // Unclosed at end of file; the missing delimiter is a parse error.
  if (!has_text || !valid_symbols[VERB_TEXT]) return false;
  lexer->mark_end(lexer);
  lexer->result_symbol = VERB_TEXT;
  return true;
}

/**
 * Closes an unclosed brace command before the end of the current line if
 * texi2any would close it there: the next line is blank (ordinary brace
 * commands only) or the `@end` of the innermost open block, or the file ends.
 * The lexer is at the line end, after any blanks.
 */
static bool scan_missing_brace(Scanner *scanner, TSLexer *lexer,
                               const bool *valid_symbols) {
  bool paragraph_end = valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END];
  bool block_end = valid_symbols[BRACE_CLOSE_AT_BLOCK_END];

  lexer->mark_end(lexer);
  if (!lexer->eof(lexer)) {
    if (lexer->lookahead == '\r') advance(lexer);
    if (lexer->lookahead != '\n') return false;
    advance(lexer);
    skip_blanks(lexer);
  }

  if (lexer->eof(lexer)) {
    lexer->result_symbol =
        paragraph_end ? BRACE_CLOSE_AT_PARAGRAPH_END : BRACE_CLOSE_AT_BLOCK_END;
    return true;
  }
  if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
    lexer->result_symbol = BRACE_CLOSE_AT_PARAGRAPH_END;
    return paragraph_end;
  }

  char name[MAX_NAME + 1];
  char end_name[MAX_NAME + 1];
  if (!block_end || scanner->depth == 0 || lexer->lookahead != '@') return false;
  advance(lexer);
  if (!read_name(lexer, name) || strcmp(name, "end") != 0 ||
      !read_end_name(lexer, end_name) ||
      strcmp(end_name, BLOCKS[scanner->stack[scanner->depth - 1]].name) != 0) {
    return false;
  }
  lexer->result_symbol = BRACE_CLOSE_AT_BLOCK_END;
  return true;
}

void *tree_sitter_texinfo_external_scanner_create(void) {
  return calloc(1, sizeof(Scanner));
}

void tree_sitter_texinfo_external_scanner_destroy(void *payload) {
  free(payload);
}

unsigned tree_sitter_texinfo_external_scanner_serialize(void *payload,
                                                        char *buffer) {
  Scanner *scanner = payload;
  memcpy(buffer, &scanner->verb_delimiter, sizeof(int32_t));
  memcpy(buffer + sizeof(int32_t), scanner->stack, scanner->depth);
  return sizeof(int32_t) + scanner->depth;
}

void tree_sitter_texinfo_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = payload;
  scanner->verb_delimiter = 0;
  scanner->depth = 0;
  if (length < sizeof(int32_t)) return;
  memcpy(&scanner->verb_delimiter, buffer, sizeof(int32_t));
  scanner->depth = length - sizeof(int32_t);
  memcpy(scanner->stack, buffer + sizeof(int32_t), scanner->depth);
}

bool tree_sitter_texinfo_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  Scanner *scanner = payload;

  // During error recovery every symbol is valid; let the internal lexer
  // handle it rather than pushing or popping blocks speculatively.
  if (valid_symbols[ERROR_SENTINEL]) return false;

  if (valid_symbols[VERB_OPEN] || valid_symbols[VERB_TEXT] ||
      valid_symbols[VERB_CLOSE]) {
    return scan_verb(scanner, lexer, valid_symbols);
  }

  if (valid_symbols[RAW_TEXT]) return scan_raw(scanner, lexer);

  if (valid_symbols[MISSING_END] && lexer->eof(lexer) && scanner->depth > 0) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }

  if (valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] ||
      valid_symbols[BRACE_CLOSE_AT_BLOCK_END]) {
    while (is_blank(lexer->lookahead)) skip(lexer);
    if (lexer->eof(lexer) || lexer->lookahead == '\n' ||
        lexer->lookahead == '\r') {
      return scan_missing_brace(scanner, lexer, valid_symbols);
    }
  }

  // Leading blanks are left out of the token. The grammar only allows block
  // openers and `@end` at the start of a line; checking the column here
  // instead would break incremental parsing, since tree-sitter doesn't know
  // to re-scan a token when text before it on the same line changes.
  while (is_blank(lexer->lookahead)) skip(lexer);

  if (valid_symbols[END_OF_FILE] && lexer->eof(lexer)) {
    lexer->result_symbol = END_OF_FILE;
    return true;
  }

  if (lexer->lookahead != '@') return false;
  advance(lexer);

  char name[MAX_NAME + 1];
  if (!read_name(lexer, name)) return false;

  if (valid_symbols[BLOCK_END] && scan_block_end(scanner, lexer, name)) {
    return true;
  }

  // read_name stops at the first character that can't be part of a name, so
  // `@html@c` opens a raw block, as in texi2any.
  return scan_block_start(scanner, lexer, valid_symbols, name);
}
