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
  // the end of the file, and before the `@end` of an open block.
  BRACE_CLOSE_AT_PARAGRAPH_END,
  BRACE_CLOSE_AT_BLOCK_END,
  BRACE_CLOSE_AT_LINE_END,
  // Zero-width end of a block still open at the end of the file, or before
  // an `@end` of an enclosing block or of no open block, which closes it.
  MISSING_END,
  MENU_START,
  // Openers of definition blocks, one per layout of the definition line, in
  // the order of DEFINITION_LAYOUTS in grammar.js.
  DEFINITION_START,
  // `@node` lines followed by a sectioning command, one per level, in the
  // order of SECTIONS in grammar.js.
  NODE_BEFORE_SECTION = DEFINITION_START + 8,
  LINE_COMMAND_NAME = NODE_BEFORE_SECTION + 6,
  BRACE_COMMAND_NAME,
  MULTITABLE_START,
  // `@tab` between the cells of a multitable row.
  TAB,
  // `@item` or `@headitem` starting a multitable row.
  ROW_NAME,
  // `@subentry` after the argument of a line command.
  SUBENTRY_NAME,
  // The one-character argument of an accent command without braces.
  ACCENT_ARGUMENT,
  FLOAT_START,
  // `@end` that doesn't end the innermost open block, to the end of its line.
  UNMATCHED_END,
  // The raw text of `@inlineraw`, up to the matching brace.
  INLINE_RAW_TEXT,
  // `@footnote`, `@caption`, `@shortcaption`, whose text is paragraphs.
  CONTEXT_COMMAND_NAME,
  // `@item` in `@itemize` or `@enumerate`, whose text is a paragraph.
  CONTAINER_ITEM_NAME,
  // `@macro`, `@rmacro` and `@linemacro`, whose raw body is Texinfo.
  MACRO_START,
  // `@math`, in whose argument nothing ends at a blank line.
  MATH_NAME,
  // The end of a paragraph at the start of a line that ends it (blank, an
  // `@end` or a block), after a line command inside a brace command took the
  // newline before it.
  PARAGRAPH_END_AT_LINE_START,
  // Zero-width, between two parts of a definition line's word with no space
  // between them, which make one word, as in texi2any.
  WORD_GLUE,
  // A run of dots in a menu node, but for the last one before whitespace,
  // which ends the node.
  MENU_NODE_DOTS,
  ERROR_SENTINEL,
};

typedef enum {
  KIND_BLOCK,
  // Body is a sequence of `@item`s.
  KIND_LIST,
  // `@multitable`: a list whose items are rows of cells separated by `@tab`.
  KIND_MULTITABLE,
  // `@float`: a block whose argument is a type and a label.
  KIND_FLOAT,
  // Body is plain text. A nested block of the same name is counted.
  KIND_RAW,
  // Body is plain text. Nested macro definitions of any kind are counted.
  KIND_MACRO,
  // `@menu`, `@detailmenu`, `@direntry`: may contain menu entries.
  KIND_MENU,
  // `@deffn` and friends: KIND_DEFINITION plus the index of the definition
  // line's layout in DEFINITION_LAYOUTS in grammar.js. Must be last.
  KIND_DEFINITION,
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
    {"float", KIND_FLOAT},
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
    {"menu", KIND_MENU},
    {"detailmenu", KIND_MENU},
    {"direntry", KIND_MENU},
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
    // Definitions; the layout numbers index DEFINITION_LAYOUTS in grammar.js
    {"defblock", KIND_BLOCK},
    {"deffn", KIND_DEFINITION},
    {"defvr", KIND_DEFINITION},
    {"deftypefn", KIND_DEFINITION + 1},
    {"deftypevr", KIND_DEFINITION + 1},
    {"deftp", KIND_DEFINITION},
    {"defop", KIND_DEFINITION + 2},
    {"deftypeop", KIND_DEFINITION + 3},
    {"defcv", KIND_DEFINITION + 2},
    {"deftypecv", KIND_DEFINITION + 3},
    {"defun", KIND_DEFINITION + 4},
    {"defmac", KIND_DEFINITION + 4},
    {"defspec", KIND_DEFINITION + 4},
    {"defvar", KIND_DEFINITION + 4},
    {"defopt", KIND_DEFINITION + 4},
    {"deftypefun", KIND_DEFINITION + 5},
    {"deftypevar", KIND_DEFINITION + 5},
    {"defmethod", KIND_DEFINITION + 6},
    {"deftypemethod", KIND_DEFINITION + 7},
    {"defivar", KIND_DEFINITION + 6},
    {"deftypeivar", KIND_DEFINITION + 7},
    // Lists
    {"itemize", KIND_LIST},
    {"enumerate", KIND_LIST},
    {"table", KIND_LIST},
    {"ftable", KIND_LIST},
    {"vtable", KIND_LIST},
    {"multitable", KIND_MULTITABLE},
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

// Serialized state: the `@verb` delimiter and the accent flag, then the
// block stack, which takes the rest of tree-sitter's buffer.
#define STATE_HEADER (sizeof(int32_t) + 1)
#define MAX_DEPTH (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - STATE_HEADER)

typedef struct {
  // Indices into BLOCKS, innermost last.
  uint8_t stack[MAX_DEPTH];
  unsigned depth;
  // Delimiter of the `@verb` being scanned, from its opening to its closing.
  int32_t verb_delimiter;
  // Whether the last token was the name of an accent command.
  bool after_accent;
} Scanner;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

// Ends the token here, as `token`.
static inline bool accept(TSLexer *lexer, enum TokenType token) {
  lexer->mark_end(lexer);
  lexer->result_symbol = token;
  return true;
}

static inline bool is_alpha(int32_t c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static inline bool is_digit(int32_t c) { return c >= '0' && c <= '9'; }

static inline bool is_blank(int32_t c) {
  return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

static void skip_blanks(TSLexer *lexer) {
  while (is_blank(lexer->lookahead)) advance(lexer);
}

static void skip_to_next_line(TSLexer *lexer) {
  while (!lexer->eof(lexer) && lexer->lookahead != '\n') advance(lexer);
  if (lexer->lookahead == '\n') advance(lexer);
}

// A character of a command or block name after its first.
static inline bool is_name_char(int32_t c) {
  return is_alpha(c) || is_digit(c) || c == '-' || c == '_';
}

/**
 * Reads a command or block name whole, as texi2any does: a letter or digit
 * followed by letters, digits, `-` or `_`, of any length. Returns false if
 * there is none or it is too long to be a built-in name; `name` holds it
 * otherwise.
 */
static bool read_name(TSLexer *lexer, char *name) {
  unsigned length = 0;
  if (!is_alpha(lexer->lookahead) && !is_digit(lexer->lookahead)) return false;
  while (is_name_char(lexer->lookahead)) {
    if (length < MAX_NAME) name[length] = (char)lexer->lookahead;
    length++;
    advance(lexer);
  }
  name[length < MAX_NAME ? length : MAX_NAME] = '\0';
  return length < MAX_NAME;
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

// Commands whose argument is the rest of the line.
static const char *const LINE_COMMANDS[] = {
    // Structure and cross-referencing
    "printindex", "include", "insertcopying", "contents", "shortcontents",
    "summarycontents", "listoffloats", "nodedescription",
    // Indices
    "cindex", "findex", "kindex", "pindex", "tindex", "vindex", "defindex",
    "defcodeindex", "synindex", "syncodeindex",
    // Header and title page
    "setfilename", "settitle", "title", "subtitle", "author", "shorttitlepage",
    "dircategory", "documentencoding", "documentlanguage", "setchapternewpage",
    "headings", "evenheading", "oddheading", "everyheading", "evenfooting",
    "oddfooting", "everyfooting",
    // Variables and macros
    "definfoenclose",
    // Layout
    "page", "need", "sp", "center", "exdent", "finalout", "smallbook",
    "afourpaper", "afivepaper", "afourlatex", "afourwide", "pagesizes",
    "paragraphindent", "firstparagraphindent", "exampleindent", "footnotestyle",
    "frenchspacing", "allowcodebreaks", "kbdinputstyle", "codequoteundirected",
    "codequotebacktick", "deftypefnnewline", "urefbreakstyle",
    "xrefautomaticsectiontitle", "setcontentsaftertitlepage",
    "setshortcontentsaftertitlepage", "novalidate", "raisesections",
    "lowersections",
    // Less common: two-letter index names, page layout, cross-reference names
    "cpindex", "fnindex", "kyindex", "pgindex", "tpindex", "vrindex",
    "bsixpaper", "fonttextsize", "microtype", "maketitle", "verbatiminclude",
    "xrefname", "evenfootingmarks", "evenheadingmarks", "everyfootingmarks",
    "everyheadingmarks", "oddfootingmarks", "oddheadingmarks"};

// Built-in brace commands whose argument ends at a blank line, as in texi2any:
// all of them except @verb, the "context" commands @footnote, @caption,
// @shortcaption and @math, and the inline conditionals (@inlinefmt, ...),
// whose skipped arguments may contain blank lines. Only these may have blanks
// before their brace. Any other command, a macro call or a command texi2any
// doesn't know, also ends at a blank line (in grammar.js), although in
// texi2any a macro call's arguments may contain one.
static const char *const BRACE_COMMANDS[] = {"AA",
                                             "AE",
                                             "DH",
                                             "H",
                                             "L",
                                             "LaTeX",
                                             "O",
                                             "OE",
                                             "TH",
                                             "TeX",
                                             "U",
                                             "aa",
                                             "ae",
                                             "ampchar",
                                             "anchor",
                                             "arrow",
                                             "asis",
                                             "atchar",
                                             "b",
                                             "backslashchar",
                                             "bullet",
                                             "cite",
                                             "click",
                                             "clicksequence",
                                             "code",
                                             "comma",
                                             "command",
                                             "copyright",
                                             "dfn",
                                             "dh",
                                             "dmn",
                                             "dotaccent",
                                             "dotless",
                                             "dots",
                                             "emph",
                                             "enddots",
                                             "env",
                                             "equiv",
                                             "error",
                                             "errormsg",
                                             "euro",
                                             "exclamdown",
                                             "expansion",
                                             "file",
                                             "geq",
                                             "guillemetleft",
                                             "guillemetright",
                                             "guillemotleft",
                                             "guillemotright",
                                             "guilsinglleft",
                                             "guilsinglright",
                                             "hashchar",
                                             "headitemfont",
                                             "hyphenation",
                                             "i",
                                             "indicateurl",
                                             "kbd",
                                             "key",
                                             "l",
                                             "lbracechar",
                                             "leq",
                                             "minus",
                                             "o",
                                             "oe",
                                             "ogonek",
                                             "option",
                                             "ordf",
                                             "ordm",
                                             "point",
                                             "pounds",
                                             "print",
                                             "questiondown",
                                             "quotedblbase",
                                             "quotedblleft",
                                             "quotedblright",
                                             "quoteleft",
                                             "quoteright",
                                             "quotesinglbase",
                                             "r",
                                             "rbracechar",
                                             "registeredsymbol",
                                             "result",
                                             "ringaccent",
                                             "samp",
                                             "sansserif",
                                             "sc",
                                             "seealso",
                                             "seeentry",
                                             "slanted",
                                             "sortas",
                                             "ss",
                                             "strong",
                                             "sub",
                                             "sup",
                                             "t",
                                             "textdegree",
                                             "th",
                                             "tie",
                                             "tieaccent",
                                             "titlefont",
                                             "today",
                                             "u",
                                             "ubaraccent",
                                             "udotaccent",
                                             "v",
                                             "value",
                                             "var",
                                             "w"};

static bool in_table(const char *const *table, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(table[i], name) == 0) return true;
  }
  return false;
}

#define TABLE_COUNT(table) (sizeof(table) / sizeof(table[0]))

/**
 * Whether `@name` at the start of a line is a line command. Commands ending
 * in `index` count, including user-defined index commands (`@defindex op`
 * makes `@opindex`), as texi2any treats them as index entries.
 */
static bool is_line_command(const char *name) {
  size_t length = strlen(name);
  return in_table(LINE_COMMANDS, TABLE_COUNT(LINE_COMMANDS), name) ||
         (length > 5 && strcmp(name + length - 5, "index") == 0);
}

// Sectioning commands by level: the index of the section in SECTIONS in
// grammar.js (part, top, chapter, section, subsection, subsubsection).
static const struct {
  const char *name;
  uint8_t level;
} SECTIONING[] = {
    {"part", 0},
    {"top", 1},
    {"chapter", 2},
    {"unnumbered", 2},
    {"appendix", 2},
    {"centerchap", 2},
    {"section", 3},
    {"unnumberedsec", 3},
    {"appendixsec", 3},
    {"appendixsection", 3},
    {"subsection", 4},
    {"unnumberedsubsec", 4},
    {"appendixsubsec", 4},
    {"subsubsection", 5},
    {"unnumberedsubsubsec", 5},
    {"appendixsubsubsec", 5},
};

#define SECTIONING_COUNT (sizeof(SECTIONING) / sizeof(SECTIONING[0]))

/**
 * Scans `@node` when the next line, after blank and comment lines, the
 * node's description (`@nodedescription`, `@nodedescriptionblock`) and
 * `@anchor` lines, is a sectioning command: emits the node token for that
 * section's level, so the node line starts the section. The `@node` has already
 * been read; otherwise it is left to the grammar as a plain node line.
 */
static bool scan_node_before_section(TSLexer *lexer,
                                     const bool *valid_symbols) {
  lexer->mark_end(lexer);
  skip_to_next_line(lexer);
  for (;;) {
    skip_blanks(lexer);
    if (lexer->eof(lexer)) return false;
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      skip_to_next_line(lexer);
      continue;
    }
    if (lexer->lookahead != '@') return false;
    advance(lexer);
    char name[MAX_NAME + 1];
    if (!read_name(lexer, name)) return false;

    if (strcmp(name, "c") == 0 || strcmp(name, "comment") == 0 ||
        strcmp(name, "nodedescription") == 0) {
      if (!is_blank(lexer->lookahead) && lexer->lookahead != '\n' &&
          lexer->lookahead != '\r' && !lexer->eof(lexer)) {
        return false;
      }
      skip_to_next_line(lexer);
      continue;
    }

    // `@anchor{Old name}` lines keep old node names working. Blanks may come
    // before the brace, as in texi2any.
    if (strcmp(name, "anchor") == 0) {
      skip_blanks(lexer);
      if (lexer->lookahead != '{') return false;
      skip_to_next_line(lexer);
      continue;
    }

    if (strcmp(name, "nodedescriptionblock") == 0) {
      // Skip to the line after its `@end`.
      for (;;) {
        skip_to_next_line(lexer);
        if (lexer->eof(lexer)) return false;
        skip_blanks(lexer);
        if (lexer->lookahead != '@') continue;
        advance(lexer);
        char end_name[MAX_NAME + 1];
        if (read_name(lexer, name) && strcmp(name, "end") == 0 &&
            read_end_name(lexer, end_name) &&
            strcmp(end_name, "nodedescriptionblock") == 0) {
          break;
        }
      }
      skip_to_next_line(lexer);
      continue;
    }

    for (unsigned i = 0; i < SECTIONING_COUNT; i++) {
      if (strcmp(SECTIONING[i].name, name) == 0) {
        enum TokenType token = NODE_BEFORE_SECTION + SECTIONING[i].level;
        if (!valid_symbols[token]) return false;
        lexer->result_symbol = token;
        return true;
      }
    }
    return false;
  }
}

// Conditionals, which texi2any evaluates before parsing.
static inline bool is_conditional(const char *name) {
  return strncmp(name, "if", 2) == 0;
}

/** The name of the innermost open block; there must be one. */
static inline const char *innermost_block(const Scanner *scanner) {
  return BLOCKS[scanner->stack[scanner->depth - 1]].name;
}

/** Pops the innermost block, whose `@end NAME` has just been read. */
static bool pop_block(Scanner *scanner, TSLexer *lexer) {
  scanner->depth--;
  return accept(lexer, BLOCK_END);
}

// Accent commands, which texi2any also accepts without braces: `@'e`,
// `@u o`.
static const char *const LETTER_ACCENTS[] = {
    "H",         "dotaccent", "dotless",    "ogonek",     "ringaccent",
    "tieaccent", "u",         "ubaraccent", "udotaccent", "v"};

static bool is_symbol_accent(int32_t c) {
  return c == '\'' || c == '"' || c == '^' || c == '`' || c == '~' ||
         c == '=' || c == ',';
}

/**
 * Scans the argument of an accent command without braces: one character
 * after any blanks and at most one newline, as in texi2any. Not `@`, a brace
 * or a blank line.
 */
static bool scan_accent_argument(TSLexer *lexer) {
  while (is_blank(lexer->lookahead)) skip(lexer);
  if (lexer->lookahead == '\r') skip(lexer);
  if (lexer->lookahead == '\n') {
    skip(lexer);
    while (is_blank(lexer->lookahead)) skip(lexer);
  }
  int32_t c = lexer->lookahead;
  if (lexer->eof(lexer) || c == '@' || c == '{' || c == '}' || c == '\n' ||
      c == '\r') {
    return false;
  }
  advance(lexer);
  return accept(lexer, ACCENT_ARGUMENT);
}

/**
 * Scans the raw text of `@inlineraw` up to, not including, the brace that
 * closes the command: braces nest, and `@` escapes the next character. Leading
 * blanks are left out.
 */
static bool scan_inline_raw(TSLexer *lexer) {
  while (is_blank(lexer->lookahead)) skip(lexer);
  unsigned depth = 0;
  bool has_text = false;
  while (!lexer->eof(lexer)) {
    int32_t c = lexer->lookahead;
    if (c == '}') {
      if (depth == 0) break;
      depth--;
    } else if (c == '{') {
      depth++;
    } else if (c == '@') {
      advance(lexer);
      if (lexer->eof(lexer)) break;
    }
    advance(lexer);
    has_text = true;
    lexer->mark_end(lexer);
  }
  lexer->result_symbol = INLINE_RAW_TEXT;
  return has_text;
}

/**
 * The innermost open block other than conditionals, which texi2any evaluates
 * before parsing, or NULL.
 */
static const Block *innermost_structure(const Scanner *scanner) {
  for (unsigned i = scanner->depth; i > 0; i--) {
    const Block *block = &BLOCKS[scanner->stack[i - 1]];
    if (!is_conditional(block->name)) return block;
  }
  return NULL;
}

static bool in_multitable(const Scanner *scanner) {
  const Block *block = innermost_structure(scanner);
  return block != NULL && block->kind == KIND_MULTITABLE;
}

// Inside a preformatted block, where a blank line doesn't end a brace
// command. Every block nested in one, such as `@group` or `@quotation`, keeps
// it preformatted, as in texi2any.
static bool in_preformatted(const Scanner *scanner) {
  static const char *const PREFORMATTED[] = {
      "example", "smallexample", "lisp",   "smalllisp",
      "display", "smalldisplay", "format", "smallformat"};
  for (unsigned i = 0; i < scanner->depth; i++) {
    if (in_table(PREFORMATTED, TABLE_COUNT(PREFORMATTED),
                 BLOCKS[scanner->stack[i]].name)) {
      return true;
    }
  }
  return false;
}

// `@itemize` and `@enumerate`, where the text after `@item` is a paragraph,
// unlike in tables, where it is the item's line argument.
static bool in_item_container(const Scanner *scanner) {
  const Block *block = innermost_structure(scanner);
  return block != NULL && (strcmp(block->name, "itemize") == 0 ||
                           strcmp(block->name, "enumerate") == 0);
}

// Whether `@name` starts a list's next item or a multitable's next cell,
// which ends a paragraph and an ordinary brace command left open before it,
// as in texi2any.
static bool starts_item(const Scanner *scanner, const char *name) {
  const Block *block = innermost_structure(scanner);
  if (block == NULL ||
      (block->kind != KIND_LIST && block->kind != KIND_MULTITABLE)) {
    return false;
  }
  return strcmp(name, "item") == 0 || strcmp(name, "itemx") == 0 ||
         strcmp(name, "headitem") == 0 || strcmp(name, "tab") == 0;
}

/**
 * Scans a block opener such as `@example` at the start of a line. The `@` has
 * already been consumed.
 */
static bool scan_block_start(Scanner *scanner, TSLexer *lexer,
                             const bool *valid_symbols, const char *name) {
  int index = find_block(name);
  if (index < 0) return false;

  // The opener's token for each kind of block, in BlockKind's order.
  static const enum TokenType START_TOKENS[] = {
      BLOCK_START,     LIST_START,  MULTITABLE_START, FLOAT_START,
      RAW_BLOCK_START, MACRO_START, MENU_START};
  BlockKind kind = BLOCKS[index].kind;
  enum TokenType token = kind >= KIND_DEFINITION
                             ? DEFINITION_START + (kind - KIND_DEFINITION)
                             : START_TOKENS[kind];
  if (!valid_symbols[token]) return false;
  if (scanner->depth >= MAX_DEPTH) return false;

  scanner->stack[scanner->depth++] = (uint8_t)index;
  lexer->mark_end(lexer);
  lexer->result_symbol = token;
  return true;
}

// Whether a block named `name` is open, other than the innermost one.
static bool encloses(const Scanner *scanner, const char *name) {
  for (unsigned i = scanner->depth > 0 ? scanner->depth - 1 : 0; i > 0; i--) {
    if (strcmp(BLOCKS[scanner->stack[i - 1]].name, name) == 0) return true;
  }
  return false;
}

/**
 * Scans `@end NAME`: the end of the innermost open block, or, at the start of
 * a line where it ends no block, an unmatched end up to the end of the line,
 * which texi2any reports and ignores. The `@end` has already been consumed.
 */
static bool scan_end(Scanner *scanner, TSLexer *lexer,
                     const bool *valid_symbols, bool brace_at_line_start,
                     bool paragraph_at_line_start) {
  char end_name[MAX_NAME + 1];
  bool has_name = read_end_name(lexer, end_name);
  bool matches = has_name && scanner->depth > 0 &&
                 strcmp(end_name, innermost_block(scanner)) == 0;
  // A brace command still open at the start of the line closes first, with
  // a zero-width token before the `@end`, which is then read again; also at
  // the `@end` of an enclosing block, which closes the blocks inside it.
  if ((matches || (has_name && encloses(scanner, end_name))) &&
      brace_at_line_start && valid_symbols[BRACE_CLOSE_AT_BLOCK_END]) {
    lexer->result_symbol = BRACE_CLOSE_AT_BLOCK_END;
    return true;
  }
  // Then the paragraph it was in.
  if (paragraph_at_line_start) {
    lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
    return true;
  }
  if (matches) {
    return valid_symbols[BLOCK_END] && pop_block(scanner, lexer);
  }
  // The `@end` of an enclosing block first closes the blocks inside it, as in
  // texi2any, with a zero-width end each; the `@end` is then read again.
  if (has_name && valid_symbols[MISSING_END] && encloses(scanner, end_name)) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }
  // An `@end` of a block that isn't open closes the open blocks too, as in
  // texi2any, before it is reported as unmatched; but not conditionals,
  // which texi2any has evaluated, nor for an `@end` without a name.
  if (has_name && valid_symbols[MISSING_END] && scanner->depth > 0 &&
      !is_conditional(innermost_block(scanner))) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }
  if (!valid_symbols[UNMATCHED_END]) return false;
  while (!lexer->eof(lexer) && lexer->lookahead != '\n' &&
         lexer->lookahead != '\r') {
    advance(lexer);
  }
  return accept(lexer, UNMATCHED_END);
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
    return accept(lexer, VERB_OPEN);
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
  return accept(lexer, VERB_TEXT);
}

/**
 * Whether a block opened by `@name` closes an ordinary brace command left
 * open before it. texi2any evaluates conditionals before parsing, so they
 * close nothing, and keeps raw blocks other than `@verbatim` inside the
 * command.
 */
static bool closes_brace(const char *name) {
  int index = find_block(name);
  if (index < 0 || is_conditional(name)) return false;
  return BLOCKS[index].kind != KIND_RAW || strcmp(name, "verbatim") == 0;
}

/**
 * Closes an unclosed brace command before the end of the current line if
 * texi2any would close it there: the next line is blank or opens a block
 * other than a conditional (ordinary brace commands only), or it is the `@end`
 * of an open block, or the file ends. The lexer is at the line
 * end, after any blanks.
 */
/**
 * Scans a run of dots in a menu node, as texi2any reads it: before `,`, `@`
 * or `{` the node takes all of it; before whitespace or the end of the line,
 * all but the last, which ends the node. Otherwise, as in `foo.bar`, the
 * node's text takes the dots.
 */
static bool scan_menu_node_dots(TSLexer *lexer) {
  unsigned count = 0;
  while (lexer->lookahead == '.') {
    // Up to here, the run without its last dot.
    if (count > 0) lexer->mark_end(lexer);
    advance(lexer);
    count++;
  }
  int32_t next = lexer->lookahead;
  lexer->result_symbol = MENU_NODE_DOTS;
  if (next == ',' || next == '@' || next == '{') {
    lexer->mark_end(lexer);
    return true;
  }
  if (lexer->eof(lexer) || is_blank(next) || next == '\n' || next == '\r') {
    return count > 1;
  }
  return false;
}

static bool scan_missing_brace(Scanner *scanner, TSLexer *lexer,
                               const bool *valid_symbols) {
  bool paragraph_end = valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END];
  bool block_end = valid_symbols[BRACE_CLOSE_AT_BLOCK_END];

  lexer->mark_end(lexer);
  if (lexer->lookahead == '\r') {
    advance(lexer);
    if (lexer->lookahead != '\n') {
      // Lone carriage returns are blanks, at the end of the file too.
      while (lexer->lookahead == '\r') advance(lexer);
      if (!lexer->eof(lexer)) return false;
    }
  }
  if (!lexer->eof(lexer)) {
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
    return paragraph_end && !in_preformatted(scanner);
  }

  char name[MAX_NAME + 1];
  if (lexer->lookahead != '@') return false;
  advance(lexer);
  if (!read_name(lexer, name)) return false;

  if (strcmp(name, "end") == 0) {
    char end_name[MAX_NAME + 1];
    // The end of the innermost block, or of an enclosing one, which closes
    // the blocks inside it, as in texi2any.
    if (!block_end || scanner->depth == 0 || !read_end_name(lexer, end_name) ||
        (strcmp(end_name, innermost_block(scanner)) != 0 &&
         !encloses(scanner, end_name))) {
      return false;
    }
    lexer->result_symbol = BRACE_CLOSE_AT_BLOCK_END;
    return true;
  }

  if (!paragraph_end || !closes_brace(name)) return false;
  lexer->result_symbol = BRACE_CLOSE_AT_PARAGRAPH_END;
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
  unsigned size = 0;
  memcpy(buffer, &scanner->verb_delimiter, sizeof(int32_t));
  size += sizeof(int32_t);
  buffer[size++] = (char)scanner->after_accent;
  memcpy(buffer + size, scanner->stack, scanner->depth);
  return size + scanner->depth;
}

void tree_sitter_texinfo_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = payload;
  scanner->verb_delimiter = 0;
  scanner->after_accent = false;
  scanner->depth = 0;
  if (length < STATE_HEADER) return;
  unsigned size = 0;
  memcpy(&scanner->verb_delimiter, buffer, sizeof(int32_t));
  size += sizeof(int32_t);
  scanner->after_accent = buffer[size++];
  scanner->depth = length - size;
  memcpy(scanner->stack, buffer + size, scanner->depth);
}

/**
 * Scans the zero-width glue after a part of a definition line's word when
 * another part follows with no space between them: text, a command or a
 * brace group, but not a line ending in `@` (a continuation, which is a
 * space) or a comment. Only the next characters are read, never the
 * column, so incremental parsing re-scans it when they change.
 */
static bool scan_word_glue(TSLexer *lexer) {
  int32_t c = lexer->lookahead;
  if (lexer->eof(lexer) || is_blank(c) || c == '\n' || c == '\r' || c == '}') {
    return false;
  }
  lexer->mark_end(lexer);
  if (c == '@') {
    advance(lexer);
    c = lexer->lookahead;
    if (lexer->eof(lexer) || c == '\n' || c == '\r') return false;
    char name[MAX_NAME + 1];
    if (read_name(lexer, name) &&
        (strcmp(name, "c") == 0 || strcmp(name, "comment") == 0) &&
        (lexer->eof(lexer) || is_blank(lexer->lookahead) ||
         lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
      return false;
    }
  }
  lexer->result_symbol = WORD_GLUE;
  return true;
}

bool tree_sitter_texinfo_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  Scanner *scanner = payload;

  // During error recovery every symbol is valid; let the internal lexer
  // handle it rather than pushing or popping blocks speculatively.
  if (valid_symbols[ERROR_SENTINEL]) return false;

  // tree-sitter restores the state saved after the last external token
  // before each scan, so this is only set right after an accent's name.
  bool after_accent = scanner->after_accent;
  scanner->after_accent = false;
  if (after_accent && valid_symbols[ACCENT_ARGUMENT] &&
      scan_accent_argument(lexer)) {
    return true;
  }

  if (valid_symbols[VERB_OPEN] || valid_symbols[VERB_TEXT] ||
      valid_symbols[VERB_CLOSE]) {
    return scan_verb(scanner, lexer, valid_symbols);
  }

  if (valid_symbols[WORD_GLUE] && scan_word_glue(lexer)) return true;

  // Valid only in a menu node, after the entry's `:`, never at column 0, so
  // skipping blanks here changes nothing below.
  if (valid_symbols[MENU_NODE_DOTS]) {
    while (is_blank(lexer->lookahead)) skip(lexer);
    if (lexer->lookahead == '.') return scan_menu_node_dots(lexer);
  }

  // Before raw text: a raw block opened on the last line has none.
  if (valid_symbols[MISSING_END] && lexer->eof(lexer) && scanner->depth > 0) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }

  if (valid_symbols[RAW_TEXT]) return scan_raw(scanner, lexer);

  // Only blanks are skipped before deciding, so on an empty body the other
  // tokens can still be scanned.
  if (valid_symbols[INLINE_RAW_TEXT]) {
    while (is_blank(lexer->lookahead)) skip(lexer);
    if (!lexer->eof(lexer) && lexer->lookahead != '}') {
      return scan_inline_raw(lexer);
    }
  }

  // Right after a brace closed at a blank line there, the paragraph ends
  // too; at an `@end` or a block, it does so when the name has been read.
  bool paragraph_at_line_start = valid_symbols[PARAGRAPH_END_AT_LINE_START] &&
                                 lexer->get_column(lexer) == 0;
  if (paragraph_at_line_start) {
    lexer->mark_end(lexer);
    while (is_blank(lexer->lookahead)) skip(lexer);
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
      return true;
    }
  }

  // A brace still open at the start of a line, where a line command inside
  // the braces has taken the newline before it, closes on that line as it
  // would from the end of the previous one. The only column checked in the
  // scanner: nothing comes before it on its line, and an edit that puts
  // something there touches the newline right before it, which tree-sitter
  // re-scans.
  bool brace_at_line_start = false;
  if (valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] ||
      valid_symbols[BRACE_CLOSE_AT_BLOCK_END] ||
      valid_symbols[BRACE_CLOSE_AT_LINE_END]) {
    bool line_start = lexer->get_column(lexer) == 0;
    brace_at_line_start = line_start;
    while (is_blank(lexer->lookahead)) skip(lexer);
    if (line_start && valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] &&
        !in_preformatted(scanner) &&
        (lexer->lookahead == '\n' || lexer->lookahead == '\r')) {
      // The line is blank: the paragraph has ended.
      return accept(lexer, BRACE_CLOSE_AT_PARAGRAPH_END);
    }
    if (lexer->eof(lexer) || lexer->lookahead == '\n' ||
        lexer->lookahead == '\r') {
      // In a line command's argument, a brace closes at the end of the line.
      if (valid_symbols[BRACE_CLOSE_AT_LINE_END]) {
        return accept(lexer, BRACE_CLOSE_AT_LINE_END);
      }
      return scan_missing_brace(scanner, lexer, valid_symbols);
    }
  }

  // Leading blanks are left out of the token. The grammar only allows block
  // openers and `@end` at the start of a line; checking the column here
  // instead would break incremental parsing with tree-sitter before 0.27.1,
  // which doesn't record a column check in a scan that returns false, so the
  // internal lexer's `@end` would be kept after an edit that moves it to the
  // start of its line (CLAUDE.md).
  while (is_blank(lexer->lookahead)) skip(lexer);

  if (valid_symbols[END_OF_FILE]) {
    // A lone carriage return is a blank too (see `extras` in grammar.js).
    // Only a command can start past here, so skipping the `\r` of a line
    // end changes nothing.
    while (is_blank(lexer->lookahead) || lexer->lookahead == '\r') {
      skip(lexer);
    }
    if (lexer->eof(lexer)) {
      lexer->result_symbol = END_OF_FILE;
      return true;
    }
  }

  if (lexer->lookahead != '@') return false;
  // Zero-width tokens end here; every other token marks its own end.
  lexer->mark_end(lexer);
  advance(lexer);

  if (valid_symbols[BRACE_COMMAND_NAME] && is_symbol_accent(lexer->lookahead)) {
    advance(lexer);
    lexer->mark_end(lexer);
    scanner->after_accent = true;
    lexer->result_symbol = BRACE_COMMAND_NAME;
    return true;
  }

  char name[MAX_NAME + 1];
  if (!read_name(lexer, name)) return false;

  if (strcmp(name, "end") == 0) {
    return scan_end(scanner, lexer, valid_symbols, brace_at_line_start,
                    paragraph_at_line_start);
  }
  if (paragraph_at_line_start &&
      (find_block(name) >= 0 || starts_item(scanner, name))) {
    lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
    return true;
  }
  // A block other than a conditional, or a raw block texi2any keeps inside,
  // closes an ordinary brace command at the start of its line too, and so
  // does a list's next item.
  if (brace_at_line_start && valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] &&
      (closes_brace(name) || starts_item(scanner, name))) {
    lexer->result_symbol = BRACE_CLOSE_AT_PARAGRAPH_END;
    return true;
  }

  if (strcmp(name, "node") == 0) {
    return scan_node_before_section(lexer, valid_symbols);
  }

  if (valid_symbols[ROW_NAME] &&
      (strcmp(name, "item") == 0 || strcmp(name, "headitem") == 0) &&
      in_multitable(scanner)) {
    return accept(lexer, ROW_NAME);
  }
  if (valid_symbols[MATH_NAME] && strcmp(name, "math") == 0) {
    return accept(lexer, MATH_NAME);
  }
  if (valid_symbols[CONTAINER_ITEM_NAME] && strcmp(name, "item") == 0 &&
      in_item_container(scanner)) {
    return accept(lexer, CONTAINER_ITEM_NAME);
  }
  if (valid_symbols[CONTEXT_COMMAND_NAME] &&
      (strcmp(name, "footnote") == 0 || strcmp(name, "caption") == 0 ||
       strcmp(name, "shortcaption") == 0)) {
    return accept(lexer, CONTEXT_COMMAND_NAME);
  }
  if (valid_symbols[SUBENTRY_NAME] && strcmp(name, "subentry") == 0) {
    return accept(lexer, SUBENTRY_NAME);
  }
  if (valid_symbols[TAB] && strcmp(name, "tab") == 0) {
    return accept(lexer, TAB);
  }
  if (valid_symbols[LINE_COMMAND_NAME] && is_line_command(name)) {
    return accept(lexer, LINE_COMMAND_NAME);
  }
  if (valid_symbols[BRACE_COMMAND_NAME] &&
      in_table(BRACE_COMMANDS, TABLE_COUNT(BRACE_COMMANDS), name)) {
    lexer->mark_end(lexer);
    scanner->after_accent =
        in_table(LETTER_ACCENTS, TABLE_COUNT(LETTER_ACCENTS), name);
    lexer->result_symbol = BRACE_COMMAND_NAME;
    return true;
  }

  // read_name stops at the first character that can't be part of a name, so
  // `@html@c` opens a raw block, as in texi2any.
  return scan_block_start(scanner, lexer, valid_symbols, name);
}
