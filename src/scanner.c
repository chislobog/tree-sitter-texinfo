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
  NODE_BEFORE = DEFINITION_START + 8,
  LINE_COMMAND_NAME = NODE_BEFORE + 6,
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
  // The same in a line command's line, which can't go on to the next line.
  LINE_ACCENT_ARGUMENT,
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
  // A paragraph's newline when comment lines follow it, then a line that
  // isn't blank, zero-width before it: as in texi2any, they don't end the
  // paragraph.
  NEWLINE_BEFORE_COMMENTS,
  // A line command that doesn't end a paragraph, inside one.
  PARAGRAPH_LINE_COMMAND_NAME,
  // Zero-width, between two parts of a definition line's word with no space
  // between them, which make one word, as in texi2any.
  WORD_GLUE,
  // A run of dots in a menu node, but for the last one before whitespace,
  // which ends the node.
  MENU_NODE_DOTS,
  // Zero-width before a menu entry, where the innermost enclosing block
  // other than a conditional is a menu.
  MENU_ENTRY_START,
  // A block other than a conditional in a menu entry's description.
  DESCRIPTION_BLOCK_START,
  NO_PARAGRAPH_COMMAND_NAME,
  PARAGRAPH_RAW_BLOCK_START,
  PARAGRAPH_MACRO_START,
  PARAGRAPH_CONDITIONAL_START,
  PARAGRAPH_END_BEFORE_COMMAND,
  NO_PARAGRAPH_CONTEXT_NAME,
  NO_PARAGRAPH_SYMBOL,
  NO_PARAGRAPH_START,
  END_LINE_TEXT,
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
    // `@defblock` holds `@defline` lines and has no definition line of its
    // own, so it is an ordinary block.
    {"defblock", KIND_BLOCK},
    // Definitions; the layout numbers index DEFINITION_LAYOUTS in grammar.js
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

// The `@verb` delimiter after a `@verb{` at the end of a line, which has
// none: no character is negative.
#define NO_VERB_DELIMITER (-1)

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

// The outcome of a scan of the first stage (see the scan function), after
// which the second stage may only go on if nothing but blanks was read.
typedef enum { SCAN_NONE, SCAN_FAILED, SCAN_FOUND } ScanResult;

static inline void advance(TSLexer *lexer) { lexer->advance(lexer, false); }

static inline void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

// Ends the token here, as `token`.
static inline bool accept(TSLexer *lexer, enum TokenType token) {
  lexer->mark_end(lexer);
  lexer->result_symbol = (TSSymbol)token;
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

/**
 * Whether a scan started at the start of its line, before any blanks. Asked
 * only where the answer decides the token: tree-sitter counts the column
 * from the start of the line on every call, which made a long line's parse
 * quadratic when it was asked before each token. `consumed` counts the
 * characters read since the start (blanks, carriage returns, `@` and a
 * name, each one byte), so the start's column is the current one less it.
 */
typedef struct {
  TSLexer *lexer;
  unsigned consumed;
  int8_t at_start;  // -1 until asked
} LineStart;

static bool starts_line(LineStart *start) {
  if (start->at_start < 0) {
    start->at_start = start->lexer->get_column(start->lexer) == start->consumed;
  }
  return start->at_start;
}

// Skips the blanks, and carriage returns if `returns`, counting them.
static void skip_counted(LineStart *start, bool returns) {
  TSLexer *lexer = start->lexer;
  while (is_blank(lexer->lookahead) || (returns && lexer->lookahead == '\r')) {
    skip(lexer);
    start->consumed++;
  }
}

// A character of a command or block name after its first.
static inline bool is_name_char(int32_t c) {
  return is_alpha(c) || is_digit(c) || c == '-';
}

/**
 * Reads a command or block name whole, as texi2any does: a letter or digit
 * followed by letters, digits and `-`, of any length (`_` ends it: `@c_x` is
 * a comment). Returns false if
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
    // Structure
    "include",
    // Indices
    "cindex", "findex", "kindex", "pindex", "tindex", "vindex", "defindex",
    "defcodeindex", "synindex", "syncodeindex",
    // Header and title page
    "settitle", "title", "subtitle", "author", "shorttitlepage",
    "documentencoding", "documentlanguage", "setchapternewpage", "headings",
    "evenheading", "oddheading", "everyheading", "evenfooting", "oddfooting",
    "everyfooting",
    // Variables and macros
    "definfoenclose",
    // Layout
    "finalout", "smallbook", "afourpaper", "afivepaper", "afourlatex",
    "afourwide", "pagesizes", "paragraphindent", "firstparagraphindent",
    "exampleindent", "footnotestyle", "frenchspacing", "allowcodebreaks",
    "kbdinputstyle", "codequoteundirected", "codequotebacktick",
    "deftypefnnewline", "urefbreakstyle", "xrefautomaticsectiontitle",
    "novalidate", "raisesections", "lowersections",
    // Less common: two-letter index names, page layout
    "cpindex", "fnindex", "kyindex", "pgindex", "tpindex", "vrindex",
    "bsixpaper", "fonttextsize", "microtype", "evenfootingmarks",
    "evenheadingmarks", "everyfootingmarks", "everyheadingmarks",
    "oddfootingmarks", "oddheadingmarks"};

// Line commands that end a paragraph, as texi2any's close_paragraph_commands:
// the others may be inside one.
static const char *const PARAGRAPH_ENDING_LINE_COMMANDS[] = {
    // Structure and cross-referencing
    "printindex", "insertcopying", "contents", "shortcontents",
    "summarycontents", "listoffloats", "nodedescription", "verbatiminclude",
    "xrefname",
    // Header and title page
    "setfilename", "dircategory", "maketitle",
    // Layout
    "page", "need", "sp", "center", "exdent"};

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
static bool ends_paragraph(const char *name) {
  return in_table(PARAGRAPH_ENDING_LINE_COMMANDS,
                  TABLE_COUNT(PARAGRAPH_ENDING_LINE_COMMANDS), name);
}

static bool is_line_command(const char *name) {
  size_t length = strlen(name);
  return in_table(LINE_COMMANDS, TABLE_COUNT(LINE_COMMANDS), name) ||
         ends_paragraph(name) ||
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

// `@node`, the sectioning commands and `@bye`, which close the brace
// commands and blocks left open before them, as in texi2any ("@node seen
// before @end menu").
static bool is_root_command(const char *name) {
  if (strcmp(name, "node") == 0 || strcmp(name, "bye") == 0) return true;
  for (unsigned i = 0; i < SECTIONING_COUNT; i++) {
    if (strcmp(SECTIONING[i].name, name) == 0) return true;
  }
  return false;
}

/**
 * Accepts the token of a `@node` line before the sectioning command `name`,
 * if it is one: the node token for that section's level.
 */
static bool accept_node_before(TSLexer *lexer, const bool *valid_symbols,
                               const char *name) {
  for (unsigned i = 0; i < SECTIONING_COUNT; i++) {
    if (strcmp(SECTIONING[i].name, name) == 0) {
      enum TokenType token = NODE_BEFORE + SECTIONING[i].level;
      if (!valid_symbols[token]) return false;
      lexer->result_symbol = (TSSymbol)token;
      return true;
    }
  }
  return false;
}

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

    // A comment, whatever follows its name, as in texi2any.
    if (strcmp(name, "c") == 0 || strcmp(name, "comment") == 0) {
      skip_to_next_line(lexer);
      continue;
    }
    if (strcmp(name, "nodedescription") == 0) {
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
      // Skip to the line after its `@end`. Left open, it closes at the next
      // node, section or `@bye`, as in texi2any, where this scan stops too:
      // read to the end of the file, it made a parse of many such nodes
      // quadratic. A section there is still the node's.
      for (;;) {
        skip_to_next_line(lexer);
        if (lexer->eof(lexer)) return false;
        skip_blanks(lexer);
        if (lexer->lookahead != '@') continue;
        advance(lexer);
        if (!read_name(lexer, name)) continue;
        if (is_root_command(name)) {
          return accept_node_before(lexer, valid_symbols, name);
        }
        char end_name[MAX_NAME + 1];
        if (strcmp(name, "end") == 0 && read_end_name(lexer, end_name) &&
            strcmp(end_name, "nodedescriptionblock") == 0) {
          break;
        }
      }
      skip_to_next_line(lexer);
      continue;
    }

    return accept_node_before(lexer, valid_symbols, name);
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

// Commands that don't start a paragraph, as in texi2any (its
// `%no_paragraph_commands`, but the line and block commands, and those the
// grammar recognises otherwise): where one would start, they are outside it.
// Some are in BRACE_COMMANDS, which they are where a paragraph is open.
static const char *const NO_PARAGRAPH_COMMANDS[] = {
    "anchor",   "errormsg", "hyphenation", "indent",
    "noindent", "refill",   "titlefont"};

// The brace commands that end a paragraph, also in the middle of a line,
// as in texi2any (its close_paragraph_commands), and start none: the
// context commands `@caption` and `@shortcaption`, and `@titlefont`.
static const char *const PARAGRAPH_ENDING_BRACE_COMMANDS[] = {
    "caption", "shortcaption", "titlefont"};

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
 * Whether, after a comment line already read, more comment lines and then a
 * line that isn't blank follow, which goes on with the paragraph or ends it.
 */
static bool more_comment_lines(TSLexer *lexer) {
  for (;;) {
    while (is_blank(lexer->lookahead) || lexer->lookahead == '\r') {
      advance(lexer);
    }
    if (lexer->lookahead != '@') break;
    advance(lexer);
    char name[MAX_NAME + 1];
    if (!read_name(lexer, name) ||
        (strcmp(name, "c") != 0 && strcmp(name, "comment") != 0)) {
      return true;
    }
    skip_to_next_line(lexer);
  }
  return !lexer->eof(lexer) && lexer->lookahead != '\n';
}

/**
 * Whether the lookahead, after any blanks, may be the argument of an accent
 * command without braces: not `@`, a brace, a line end or the end of the
 * file.
 */
static bool is_accent_argument(TSLexer *lexer) {
  int32_t c = lexer->lookahead;
  return !lexer->eof(lexer) && c != '@' && c != '{' && c != '}' && c != '\n' &&
         c != '\r';
}

/**
 * Scans the zero-width token before a paragraph's newline when comment lines
 * follow it: as in texi2any, they don't end the paragraph, also before a
 * blank line or the end of the file. After an accent at the end of the line
 * (`accent`), the next line's first character may be its argument instead.
 * The line end is skipped, so that the argument's token starts after it.
 */
static bool scan_newline_before_comments(TSLexer *lexer, bool accent) {
  lexer->mark_end(lexer);
  skip(lexer);
  while (is_blank(lexer->lookahead) || lexer->lookahead == '\r') {
    skip(lexer);
  }
  if (accent && is_accent_argument(lexer)) {
    advance(lexer);
    return accept(lexer, ACCENT_ARGUMENT);
  }
  if (lexer->lookahead != '@') return false;
  advance(lexer);
  char name[MAX_NAME + 1];
  if (!read_name(lexer, name) ||
      (strcmp(name, "c") != 0 && strcmp(name, "comment") != 0)) {
    return false;
  }
  lexer->result_symbol = NEWLINE_BEFORE_COMMENTS;
  return true;
}

/**
 * Scans the argument of an accent command without braces: one character
 * after any blanks and at most one newline, as in texi2any, but none in a
 * line command's line, which the newline ends. Not `@`, a brace or a blank
 * line. Only blanks are read on the accent's line: at its end, sets
 * `next_line` if the argument may be on the next line, which the scans that
 * read past the line end decide, as the tokens that end there do.
 */
static bool scan_accent_argument(TSLexer *lexer, const bool *valid_symbols,
                                 bool *next_line) {
  bool across_lines = valid_symbols[ACCENT_ARGUMENT];
  while (is_blank(lexer->lookahead)) skip(lexer);
  if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
    *next_line = across_lines;
    return false;
  }
  if (!is_accent_argument(lexer)) return false;
  advance(lexer);
  return accept(lexer, across_lines ? ACCENT_ARGUMENT : LINE_ACCENT_ARGUMENT);
}

/**
 * Scans an accent's argument on the next line, where no token ends the line:
 * as `scan_newline_before_comments` does, where it may.
 */
static bool scan_next_line_accent_argument(TSLexer *lexer) {
  while (is_blank(lexer->lookahead) || lexer->lookahead == '\r') skip(lexer);
  if (lexer->lookahead == '\n') {
    skip(lexer);
    while (is_blank(lexer->lookahead)) skip(lexer);
  }
  if (!is_accent_argument(lexer)) return false;
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
 * Scans the rest of the line after the `@end` of a block in a paragraph, up
 * to a comment, as one text: texi2any reports it, keeping it in the `@end`
 * line. A comment right after the blanks fails the scan, as the second stage
 * would at it: the internal lexer takes it.
 */
static ScanResult scan_end_line_text(TSLexer *lexer) {
  while (is_blank(lexer->lookahead)) skip(lexer);
  bool found = false;
  for (;;) {
    int32_t c = lexer->lookahead;
    if (lexer->eof(lexer) || c == '\n' || c == '\r') break;
    if (c == '@') {
      // The text ends before a comment.
      lexer->mark_end(lexer);
      advance(lexer);
      char name[MAX_NAME + 1];
      if (read_name(lexer, name) &&
          (strcmp(name, "c") == 0 || strcmp(name, "comment") == 0)) {
        if (!found) return SCAN_FAILED;
        break;
      }
      found = true;
      lexer->mark_end(lexer);
      continue;
    }
    advance(lexer);
    found = true;
    if (!is_blank(c)) lexer->mark_end(lexer);
  }
  lexer->result_symbol = END_LINE_TEXT;
  return found ? SCAN_FOUND : SCAN_NONE;
}

/**
 * Whether the line after the current one starts with text: not a blank line,
 * an `@end`, an item, a node, a section, a block, a line command or a
 * comment.
 */
static bool starts_with_text(TSLexer *lexer) {
  skip_to_next_line(lexer);
  skip_blanks(lexer);
  int32_t c = lexer->lookahead;
  if (lexer->eof(lexer) || c == '\n' || c == '\r') return false;
  if (c != '@') return true;
  advance(lexer);
  char name[MAX_NAME + 1];
  if (!read_name(lexer, name)) return true;
  return strcmp(name, "end") != 0 && strcmp(name, "c") != 0 &&
         strcmp(name, "comment") != 0 && strcmp(name, "item") != 0 &&
         strcmp(name, "itemx") != 0 && strcmp(name, "headitem") != 0 &&
         strcmp(name, "tab") != 0 && find_block(name) < 0 &&
         !is_root_command(name) && !is_line_command(name);
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
  // In a menu entry's description, which goes on past a block, but not past
  // a conditional, which holds the next entries.
  if (kind == KIND_BLOCK && valid_symbols[DESCRIPTION_BLOCK_START] &&
      !is_conditional(name)) {
    token = DESCRIPTION_BLOCK_START;
  }
  // At the start of a paragraph's next line, which goes on past a raw block
  // but `@verbatim`, the one of them in texi2any's close_paragraph_commands.
  if (kind == KIND_RAW && valid_symbols[PARAGRAPH_RAW_BLOCK_START] &&
      strcmp(name, "verbatim") != 0) {
    token = PARAGRAPH_RAW_BLOCK_START;
  }
  // Nor past a macro definition.
  if (kind == KIND_MACRO && valid_symbols[PARAGRAPH_MACRO_START]) {
    token = PARAGRAPH_MACRO_START;
  }
  lexer->mark_end(lexer);
  // Nor past a conditional, which texi2any evaluates, so that the paragraph
  // goes on with the branch it takes: where the conditional starts with
  // text. One that starts with items, a section or a block is part of the
  // structure around the paragraph. Its first line is read here, so that an
  // edit there makes tree-sitter scan again.
  if (kind == KIND_BLOCK && is_conditional(name) &&
      valid_symbols[PARAGRAPH_CONDITIONAL_START] && starts_with_text(lexer)) {
    token = PARAGRAPH_CONDITIONAL_START;
  }
  if (!valid_symbols[token]) return false;
  if (scanner->depth >= MAX_DEPTH) return false;

  scanner->stack[scanner->depth++] = (uint8_t)index;
  lexer->result_symbol = (TSSymbol)token;
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
static bool scan_raw(Scanner *scanner, TSLexer *lexer,
                     const bool *valid_symbols) {
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
              // An empty body: the grammar offers its end where it offers
              // raw text, but check, as every other token does.
              return valid_symbols[BLOCK_END] && pop_block(scanner, lexer);
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
  // Unclosed at the end of the file, as texi2any leaves it.
  if (lexer->eof(lexer)) {
    if (!valid_symbols[BRACE_CLOSE_AT_BLOCK_END]) return false;
    scanner->verb_delimiter = 0;
    lexer->result_symbol = BRACE_CLOSE_AT_BLOCK_END;
    return true;
  }
  if (valid_symbols[VERB_OPEN]) {
    // A line end is no delimiter: as in texi2any, which reports it, the raw
    // text then ends at the first `}`, and the opening is zero-width.
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      scanner->verb_delimiter = NO_VERB_DELIMITER;
      lexer->mark_end(lexer);
      lexer->result_symbol = VERB_OPEN;
      return true;
    }
    scanner->verb_delimiter = lexer->lookahead;
    advance(lexer);
    return accept(lexer, VERB_OPEN);
  }

  bool has_text = false;
  while (!lexer->eof(lexer)) {
    if (scanner->verb_delimiter == NO_VERB_DELIMITER &&
        lexer->lookahead == '}') {
      lexer->mark_end(lexer);
      if (!has_text) scanner->verb_delimiter = 0;
      lexer->result_symbol = has_text ? VERB_TEXT : VERB_CLOSE;
      return true;
    }
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

  // Unclosed at the end of the file, which closes it next.
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

/**
 * Closes an unclosed brace command before the end of the current line if
 * texi2any would close it there: the next line is blank or opens a block
 * other than a conditional (ordinary brace commands only), or it is the `@end`
 * of an open block, a node, a section or `@bye`, or the file ends. The lexer is
 * at the line end, after any blanks.
 */
static bool scan_missing_brace(Scanner *scanner, TSLexer *lexer,
                               const bool *valid_symbols, bool accent) {
  bool paragraph_end = valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END];
  bool block_end = valid_symbols[BRACE_CLOSE_AT_BLOCK_END];

  // The line end is skipped, so that an accent's argument after it starts
  // after it (see scan_newline_before_comments).
  lexer->mark_end(lexer);
  if (lexer->lookahead == '\r') {
    skip(lexer);
    if (lexer->lookahead != '\n') {
      // Lone carriage returns are blanks, at the end of the file too.
      while (lexer->lookahead == '\r') skip(lexer);
      if (!lexer->eof(lexer)) return false;
    }
  }
  if (!lexer->eof(lexer)) {
    if (lexer->lookahead != '\n') return false;
    skip(lexer);
    while (is_blank(lexer->lookahead)) skip(lexer);
  }
  if (accent && is_accent_argument(lexer)) {
    advance(lexer);
    return accept(lexer, ACCENT_ARGUMENT);
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

  // Comment lines inside a paragraph, which don't close the brace there.
  // Before a blank line, which closes an ordinary brace command after them,
  // they are left in its argument; in other braces (`@footnote`), they stay
  // in the paragraph, as in texi2any.
  if (strcmp(name, "c") == 0 || strcmp(name, "comment") == 0) {
    if (!valid_symbols[NEWLINE_BEFORE_COMMENTS]) return false;
    skip_to_next_line(lexer);
    if (paragraph_end && !more_comment_lines(lexer)) return false;
    lexer->result_symbol = NEWLINE_BEFORE_COMMENTS;
    return true;
  }

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

  // A node, a section or `@bye` closes any brace command. Decided here, at
  // the end of the line before it, which this scan reads to it, so that an
  // edit before the command on its line makes tree-sitter scan again: at the
  // command, only the column would tell, and the subtree after such an edit
  // would be reused (in `@math{`, which a blank line doesn't close).
  if (is_root_command(name)) {
    lexer->result_symbol =
        paragraph_end ? BRACE_CLOSE_AT_PARAGRAPH_END : BRACE_CLOSE_AT_BLOCK_END;
    return paragraph_end || block_end;
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
 * brace group, but not a comment. A line ending in `@` right after the part
 * continues the word if the next line starts with it, as in texi2any; with
 * a blank before the `@` or after the line end, it is a space. Only the next
 * characters are read, never the column, so incremental parsing re-scans it
 * when they change. Once past the `@`, no glue fails the scan, as the second
 * stage would at the `@`: the internal lexer takes it.
 */
static ScanResult scan_word_glue(TSLexer *lexer) {
  int32_t c = lexer->lookahead;
  if (lexer->eof(lexer) || is_blank(c) || c == '\n' || c == '\r' || c == '}') {
    return SCAN_NONE;
  }
  lexer->mark_end(lexer);
  lexer->result_symbol = WORD_GLUE;
  if (c == '@') {
    advance(lexer);
    c = lexer->lookahead;
    if (c == '\r') {
      advance(lexer);
      c = lexer->lookahead;
      if (c != '\n') return SCAN_FAILED;
    }
    if (c == '\n') {
      advance(lexer);
      c = lexer->lookahead;
      if (lexer->eof(lexer) || is_blank(c) || c == '\n' || c == '\r' ||
          c == '}') {
        return SCAN_FAILED;
      }
      return SCAN_FOUND;
    }
    if (lexer->eof(lexer)) return SCAN_FAILED;
    char name[MAX_NAME + 1];
    if (read_name(lexer, name) &&
        (strcmp(name, "c") == 0 || strcmp(name, "comment") == 0)) {
      return SCAN_FAILED;
    }
  }
  return SCAN_FOUND;
}

/**
 * The token of the command `@name` where the grammar allows it, other than a
 * block's, or -1. At most one applies to a name, as the tables of names
 * don't overlap (script/check-scanner.py checks that), so their order
 * doesn't matter; only the context tells `@item`'s two apart.
 */
static int command_token(const Scanner *scanner, const bool *valid_symbols,
                         const char *name) {
  // A brace command that ends a paragraph, also in the middle of a line, and
  // starts none: zero-width before it where a paragraph may end, which it
  // then ends, or its name where a paragraph would start, also at the start
  // of a paragraph's next line.
  if (in_table(PARAGRAPH_ENDING_BRACE_COMMANDS,
               TABLE_COUNT(PARAGRAPH_ENDING_BRACE_COMMANDS), name)) {
    bool context =
        strcmp(name, "caption") == 0 || strcmp(name, "shortcaption") == 0;
    if (valid_symbols[PARAGRAPH_END_BEFORE_COMMAND]) {
      return PARAGRAPH_END_BEFORE_COMMAND;
    }
    if (context && valid_symbols[NO_PARAGRAPH_CONTEXT_NAME]) {
      return NO_PARAGRAPH_CONTEXT_NAME;
    }
    if (!context && valid_symbols[NO_PARAGRAPH_COMMAND_NAME]) {
      return NO_PARAGRAPH_COMMAND_NAME;
    }
  }
  // `@image` and `@namedanchor`, keywords in the grammar, where a paragraph
  // would start, which they start none: zero-width before them. In an open
  // paragraph, they are in it, as `@anchor` is.
  if (valid_symbols[NO_PARAGRAPH_START] &&
      !valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] &&
      (strcmp(name, "image") == 0 || strcmp(name, "namedanchor") == 0)) {
    return NO_PARAGRAPH_START;
  }
  bool item = strcmp(name, "item") == 0;
  if (valid_symbols[ROW_NAME] && (item || strcmp(name, "headitem") == 0) &&
      in_multitable(scanner)) {
    return ROW_NAME;
  }
  if (valid_symbols[CONTAINER_ITEM_NAME] && item &&
      in_item_container(scanner)) {
    return CONTAINER_ITEM_NAME;
  }
  if (valid_symbols[MATH_NAME] && strcmp(name, "math") == 0) return MATH_NAME;
  if (valid_symbols[CONTEXT_COMMAND_NAME] &&
      (strcmp(name, "footnote") == 0 || strcmp(name, "caption") == 0 ||
       strcmp(name, "shortcaption") == 0)) {
    return CONTEXT_COMMAND_NAME;
  }
  if (valid_symbols[SUBENTRY_NAME] && strcmp(name, "subentry") == 0) {
    return SUBENTRY_NAME;
  }
  if (valid_symbols[TAB] && strcmp(name, "tab") == 0) return TAB;
  if (valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] && is_line_command(name) &&
      !ends_paragraph(name)) {
    return PARAGRAPH_LINE_COMMAND_NAME;
  }
  if (valid_symbols[LINE_COMMAND_NAME] && is_line_command(name)) {
    return LINE_COMMAND_NAME;
  }
  // Where a paragraph's next line may go on with it (line commands inside it
  // are valid), a paragraph is open, which a command that starts none is in.
  if (valid_symbols[NO_PARAGRAPH_COMMAND_NAME] &&
      !valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] &&
      in_table(NO_PARAGRAPH_COMMANDS, TABLE_COUNT(NO_PARAGRAPH_COMMANDS),
               name)) {
    return NO_PARAGRAPH_COMMAND_NAME;
  }
  if (valid_symbols[BRACE_COMMAND_NAME] &&
      in_table(BRACE_COMMANDS, TABLE_COUNT(BRACE_COMMANDS), name)) {
    return BRACE_COMMAND_NAME;
  }
  return -1;
}

/**
 * Scans in two stages. First the tokens that only some contexts allow: an
 * accent's argument, `@verb`'s parts, the glue in a definition word, raw
 * text, and the zero-width ends of a brace, paragraph or block left open.
 * Then a command at `@`, by its name. The first stage only skips blanks
 * before giving up, so the second sees the same text; a scan of it that has
 * read further fails the whole scan, as a scan can't go back.
 */
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
  // Whether an accent at the end of its line may take the next line's first
  // character, which the scans of the line end below decide.
  bool accent_next_line = false;
  if (after_accent &&
      (valid_symbols[ACCENT_ARGUMENT] || valid_symbols[LINE_ACCENT_ARGUMENT]) &&
      scan_accent_argument(lexer, valid_symbols, &accent_next_line)) {
    return true;
  }

  if (valid_symbols[END_LINE_TEXT]) {
    ScanResult result = scan_end_line_text(lexer);
    if (result != SCAN_NONE) return result == SCAN_FOUND;
  }

  if (valid_symbols[VERB_OPEN] || valid_symbols[VERB_TEXT] ||
      valid_symbols[VERB_CLOSE]) {
    return scan_verb(scanner, lexer, valid_symbols);
  }

  if (valid_symbols[WORD_GLUE]) {
    ScanResult result = scan_word_glue(lexer);
    if (result != SCAN_NONE) return result == SCAN_FOUND;
  }

  // A menu entry, where the innermost block other than a conditional is a
  // menu: in a conditional in a menu too, which texi2any has evaluated. At
  // the start of the line, where the scan starts; the grammar then reads the
  // `*`. Elsewhere, a `*` at the start of a line is text.
  if (valid_symbols[MENU_ENTRY_START] && lexer->lookahead == '*') {
    const Block *block = innermost_structure(scanner);
    if (block != NULL && block->kind == KIND_MENU) {
      lexer->mark_end(lexer);
      advance(lexer);
      if (!is_blank(lexer->lookahead)) return false;
      lexer->result_symbol = MENU_ENTRY_START;
      return true;
    }
  }

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

  if (valid_symbols[RAW_TEXT]) return scan_raw(scanner, lexer, valid_symbols);

  // Only blanks are skipped before deciding, so on an empty body the other
  // tokens can still be scanned.
  if (valid_symbols[INLINE_RAW_TEXT]) {
    while (is_blank(lexer->lookahead)) skip(lexer);
    if (!lexer->eof(lexer) && lexer->lookahead != '}') {
      return scan_inline_raw(lexer);
    }
  }

  // The only column checked in the scanner: at the start of a line, where a
  // line command inside braces has taken the newline before it, a brace
  // closes and a paragraph ends as they would at the end of the previous
  // line. Nothing comes before them on their line, and an edit that puts
  // something there touches the newline right before it, which tree-sitter
  // re-scans.
  LineStart start = {lexer, 0, -1};
  bool paragraph_end = valid_symbols[PARAGRAPH_END_AT_LINE_START];
  bool brace_close = valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] ||
                     valid_symbols[BRACE_CLOSE_AT_BLOCK_END] ||
                     valid_symbols[BRACE_CLOSE_AT_LINE_END];

  // Right after a brace closed at a blank line there, the paragraph ends
  // too; at an `@end` or a block, it does so when the name has been read.
  if (paragraph_end) {
    lexer->mark_end(lexer);
    skip_counted(&start, false);
    if ((lexer->lookahead == '\n' || lexer->lookahead == '\r') &&
        starts_line(&start)) {
      lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
      return true;
    }
  }

  // Inside braces, where the newline may close one instead, the scan of a
  // missing brace looks for comment lines too.
  if (valid_symbols[NEWLINE_BEFORE_COMMENTS] && !brace_close) {
    skip_counted(&start, true);
    if (lexer->lookahead == '\n') {
      return scan_newline_before_comments(lexer, accent_next_line);
    }
  }

  // A brace still open at the start of a line closes on that line as it
  // would from the end of the previous one.
  if (brace_close) {
    skip_counted(&start, false);
    bool line_end = lexer->lookahead == '\n' || lexer->lookahead == '\r';
    if (line_end && valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] &&
        !in_preformatted(scanner) && starts_line(&start)) {
      // The line is blank: the paragraph has ended.
      return accept(lexer, BRACE_CLOSE_AT_PARAGRAPH_END);
    }
    if (lexer->eof(lexer) || line_end) {
      // In a line command's argument, a brace closes at the end of the line.
      if (valid_symbols[BRACE_CLOSE_AT_LINE_END]) {
        return accept(lexer, BRACE_CLOSE_AT_LINE_END);
      }
      return scan_missing_brace(scanner, lexer, valid_symbols,
                                accent_next_line);
    }
  }
  // At a line end, no other token can be scanned below.
  if (accent_next_line) return scan_next_line_accent_argument(lexer);

  // Leading blanks are left out of the token. The grammar only allows block
  // openers and `@end` at the start of a line; checking the column here
  // instead would break incremental parsing with tree-sitter before 0.27.1,
  // which doesn't record a column check in a scan that returns false, so the
  // internal lexer's `@end` would be kept after an edit that moves it to the
  // start of its line (CLAUDE.md).
  skip_counted(&start, false);

  bool missing_end = valid_symbols[MISSING_END] && scanner->depth > 0;
  if (valid_symbols[END_OF_FILE] || missing_end) {
    // A lone carriage return is a blank too (see `extras` in grammar.js).
    // Only a command can start past here, so skipping the `\r` of a line
    // end changes nothing.
    skip_counted(&start, true);
    if (lexer->eof(lexer)) {
      // At the end of the file, where the scan stopped, unless a paragraph
      // ends at the start of the line.
      if (paragraph_end && !starts_line(&start)) lexer->mark_end(lexer);
      // Blocks still open end first, also after a last line of blanks.
      if (missing_end) scanner->depth--;
      lexer->result_symbol = missing_end ? MISSING_END : END_OF_FILE;
      return true;
    }
  }

  if (lexer->lookahead != '@') return false;
  // Zero-width tokens end here; every other token marks its own end.
  lexer->mark_end(lexer);
  advance(lexer);
  start.consumed++;

  if (valid_symbols[BRACE_COMMAND_NAME] && is_symbol_accent(lexer->lookahead)) {
    advance(lexer);
    lexer->mark_end(lexer);
    scanner->after_accent = true;
    lexer->result_symbol = BRACE_COMMAND_NAME;
    return true;
  }

  // `@*` where a paragraph would start: like texi2any, it starts none. In an
  // open paragraph, it's a symbol command in it.
  if (lexer->lookahead == '*' && valid_symbols[NO_PARAGRAPH_SYMBOL] &&
      !valid_symbols[PARAGRAPH_LINE_COMMAND_NAME]) {
    advance(lexer);
    return accept(lexer, NO_PARAGRAPH_SYMBOL);
  }

  char name[MAX_NAME + 1];
  if (!read_name(lexer, name)) return false;
  start.consumed += (unsigned)strlen(name);

  if (strcmp(name, "end") == 0) {
    bool line_start = (brace_close || paragraph_end) && starts_line(&start);
    return scan_end(scanner, lexer, valid_symbols, brace_close && line_start,
                    paragraph_end && line_start);
  }
  // A node, a section or `@bye` closes the open blocks, all but a
  // conditional, which texi2any has evaluated, and which a section may be
  // inside; and the paragraph and brace command before them.
  bool closes_block = is_root_command(name) && scanner->depth > 0 &&
                      !is_conditional(innermost_block(scanner));
  if (paragraph_end &&
      (find_block(name) >= 0 || starts_item(scanner, name) ||
       is_root_command(name)) &&
      starts_line(&start)) {
    lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
    return true;
  }
  // A block other than a conditional, or a raw block texi2any keeps inside,
  // closes an ordinary brace command at the start of its line too, and so
  // does a list's next item. A node or section closes any brace command.
  if (valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] &&
      (closes_brace(name) || starts_item(scanner, name) ||
       is_root_command(name)) &&
      starts_line(&start)) {
    lexer->result_symbol = BRACE_CLOSE_AT_PARAGRAPH_END;
    return true;
  }
  if (valid_symbols[BRACE_CLOSE_AT_BLOCK_END] && is_root_command(name) &&
      starts_line(&start)) {
    lexer->result_symbol = BRACE_CLOSE_AT_BLOCK_END;
    return true;
  }
  if (closes_block && valid_symbols[MISSING_END]) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }

  if (strcmp(name, "node") == 0) {
    return scan_node_before_section(lexer, valid_symbols);
  }

  int token = command_token(scanner, valid_symbols, name);
  if (token == PARAGRAPH_END_BEFORE_COMMAND || token == NO_PARAGRAPH_START) {
    // Zero-width, before the `@`: the command is read again after it.
    lexer->result_symbol = (TSSymbol)token;
    return true;
  }
  if (token >= 0) {
    if (token == BRACE_COMMAND_NAME) {
      scanner->after_accent =
          in_table(LETTER_ACCENTS, TABLE_COUNT(LETTER_ACCENTS), name);
    }
    return accept(lexer, (enum TokenType)token);
  }

  // read_name stops at the first character that can't be part of a name, so
  // `@html@c` opens a raw block, as in texi2any.
  return scan_block_start(scanner, lexer, valid_symbols, name);
}
