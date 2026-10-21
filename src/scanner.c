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
  // order of SECTIONS in grammar.js, then one followed by `@appendix`.
  NODE_BEFORE = DEFINITION_START + 8,
  // A sectioning command but `@part` and `@top`, by its level, from 2
  // (LEVEL).
  SECTION_COMMAND = NODE_BEFORE + 7,
  LINE_COMMAND_NAME = SECTION_COMMAND + 5,
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
  PREFORMATTED_START,
  // `@item`, `@itemx`, `@headitem` or `@tab` where it means nothing, an
  // extra.
  MISPLACED_COMMAND,
  // An `@end` of no block, or without a name, in a paragraph, which it
  // doesn't end.
  PARAGRAPH_UNMATCHED_END,
  // `@itemx` in a table, or `@tab` in a multitable, right after its head,
  // and the line end of a blank or comment line before it.
  LEADING_ITEM_COMMAND,
  LEADING_NEWLINE,
  ERROR_SENTINEL,
};

// The kinds of blocks, as texi2any's categories of blocks make them.
typedef enum {
  KIND_BLOCK,
  // `@example`, `@display`, ...: preformatted, as is every block inside
  // them, where a blank line doesn't end a brace command.
  KIND_PREFORMATTED,
  // `@ifset`, `@iftex`, ...: texi2any evaluates them before parsing.
  KIND_CONDITIONAL,
  // `@table`, `@ftable`, `@vtable`: a list whose `@item`s take a line
  // argument.
  KIND_ITEM_LINE,
  // `@itemize`, `@enumerate`: a list where the text after `@item` is a
  // paragraph.
  KIND_ITEM_CONTAINER,
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

// Kinds follow texi2any's categories (`tta/data/command_data.txt` in the
// Texinfo sources), but for `@defblock`, which holds `@defline` lines and has
// no definition line of its own, an ordinary block, and the macro
// definitions, raw but `macro_definition` nodes. The layout numbers index
// DEFINITION_LAYOUTS in grammar.js. Sorted, for a binary search
// (script/check-scanner.py checks it).
static const Block BLOCKS[] = {
    {"cartouche", KIND_BLOCK},
    {"copying", KIND_BLOCK},
    {"defblock", KIND_BLOCK},
    {"defcv", KIND_DEFINITION + 2},
    {"deffn", KIND_DEFINITION},
    {"defivar", KIND_DEFINITION + 6},
    {"defmac", KIND_DEFINITION + 4},
    {"defmethod", KIND_DEFINITION + 6},
    {"defop", KIND_DEFINITION + 2},
    {"defopt", KIND_DEFINITION + 4},
    {"defspec", KIND_DEFINITION + 4},
    {"deftp", KIND_DEFINITION},
    {"deftypecv", KIND_DEFINITION + 3},
    {"deftypefn", KIND_DEFINITION + 1},
    {"deftypefun", KIND_DEFINITION + 5},
    {"deftypeivar", KIND_DEFINITION + 7},
    {"deftypemethod", KIND_DEFINITION + 7},
    {"deftypeop", KIND_DEFINITION + 3},
    {"deftypevar", KIND_DEFINITION + 5},
    {"deftypevr", KIND_DEFINITION + 1},
    {"defun", KIND_DEFINITION + 4},
    {"defvar", KIND_DEFINITION + 4},
    {"defvr", KIND_DEFINITION},
    {"detailmenu", KIND_MENU},
    {"direntry", KIND_MENU},
    {"display", KIND_PREFORMATTED},
    // texi2any parses commands inside, so not raw.
    {"displaymath", KIND_BLOCK},
    {"docbook", KIND_RAW},
    {"documentdescription", KIND_BLOCK},
    {"documentinfo", KIND_BLOCK},
    {"enumerate", KIND_ITEM_CONTAINER},
    {"example", KIND_PREFORMATTED},
    {"float", KIND_FLOAT},
    {"flushleft", KIND_BLOCK},
    {"flushright", KIND_BLOCK},
    {"format", KIND_PREFORMATTED},
    {"ftable", KIND_ITEM_LINE},
    {"group", KIND_BLOCK},
    {"html", KIND_RAW},
    {"ifclear", KIND_CONDITIONAL},
    {"ifcommanddefined", KIND_CONDITIONAL},
    {"ifcommandnotdefined", KIND_CONDITIONAL},
    {"ifdocbook", KIND_CONDITIONAL},
    {"ifhtml", KIND_CONDITIONAL},
    {"ifinfo", KIND_CONDITIONAL},
    {"iflatex", KIND_CONDITIONAL},
    {"ifnotdocbook", KIND_CONDITIONAL},
    {"ifnothtml", KIND_CONDITIONAL},
    {"ifnotinfo", KIND_CONDITIONAL},
    {"ifnotlatex", KIND_CONDITIONAL},
    {"ifnotplaintext", KIND_CONDITIONAL},
    {"ifnottex", KIND_CONDITIONAL},
    {"ifnotxml", KIND_CONDITIONAL},
    {"ifplaintext", KIND_CONDITIONAL},
    {"ifset", KIND_CONDITIONAL},
    {"iftex", KIND_CONDITIONAL},
    {"ifxml", KIND_CONDITIONAL},
    {"ignore", KIND_RAW},
    {"indentedblock", KIND_BLOCK},
    {"itemize", KIND_ITEM_CONTAINER},
    {"latex", KIND_RAW},
    {"linemacro", KIND_MACRO},
    {"lisp", KIND_PREFORMATTED},
    {"macro", KIND_MACRO},
    {"menu", KIND_MENU},
    {"multitable", KIND_MULTITABLE},
    {"nodedescriptionblock", KIND_BLOCK},
    {"publication", KIND_BLOCK},
    {"quotation", KIND_BLOCK},
    {"raggedright", KIND_BLOCK},
    {"rmacro", KIND_MACRO},
    {"smalldisplay", KIND_PREFORMATTED},
    {"smallexample", KIND_PREFORMATTED},
    {"smallformat", KIND_PREFORMATTED},
    {"smallindentedblock", KIND_BLOCK},
    {"smalllisp", KIND_PREFORMATTED},
    {"smallquotation", KIND_BLOCK},
    {"table", KIND_ITEM_LINE},
    {"tex", KIND_RAW},
    {"titlepage", KIND_BLOCK},
    {"verbatim", KIND_RAW},
    {"vtable", KIND_ITEM_LINE},
    {"xml", KIND_RAW},
};

#define BLOCK_COUNT (sizeof(BLOCKS) / sizeof(BLOCKS[0]))

// The block stack holds indices into BLOCKS as bytes.
typedef char block_index_fits_a_byte[BLOCK_COUNT <= 256 ? 1 : -1];

// Longer than any built-in name ("nodedescriptionblock" is the longest
// block name): a longer one can only be a user-defined index command
// (read_name).
#define MAX_NAME 32

// The `@verb` delimiter after a `@verb{` at the end of a line, which has
// none: no lookahead is, not even the -1 of a byte that isn't UTF-8, which
// may be a delimiter.
#define NO_VERB_DELIMITER INT32_MIN

// Deepest nesting of macro definitions tracked inside a single macro's
// body; nested raw blocks of the block's own name are counted to any depth.
#define MAX_RAW_DEPTH 64

// Serialized state: the `@verb` delimiter, the flags after an accent and a
// brace close and the count of `@raisesections`, then the block stack,
// which takes the rest of tree-sitter's buffer.
#define STATE_HEADER (sizeof(int32_t) + 3)
#define MAX_DEPTH (TREE_SITTER_SERIALIZATION_BUFFER_SIZE - STATE_HEADER)

typedef struct {
  // Indices into BLOCKS, innermost last.
  uint8_t stack[MAX_DEPTH];
  unsigned depth;
  // Delimiter of the `@verb` being scanned, from its opening to its closing.
  int32_t verb_delimiter;
  // Whether the last token was the name of an accent command.
  bool after_accent;
  // Whether the last token was a zero-width close of a brace left open.
  bool after_brace_close;
  // The `@raisesections` so far less the `@lowersections`, texi2any's
  // sections_level_modifier, which moves the sections after them.
  int8_t section_shift;
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

// Whether the lookahead is a blank or a NUL, which texi2any drops, as the
// grammar's `extras` do; not the end of the file, where the lookahead is 0
// too.
static inline bool at_blank(TSLexer *lexer) {
  return is_blank(lexer->lookahead) ||
         (lexer->lookahead == 0 && !lexer->eof(lexer));
}

static void skip_blanks(TSLexer *lexer) {
  while (at_blank(lexer)) advance(lexer);
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
  // Whether a form feed was among the blanks skipped.
  bool form_feed;
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
  while (at_blank(lexer) || (returns && lexer->lookahead == '\r')) {
    if (lexer->lookahead == '\f') start->form_feed = true;
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
 * a comment). Returns the number of characters read, or 0 if there is no
 * name or it is too long to be a built-in name; `name` holds it otherwise.
 * A longer name ending in `index` may still be a user-defined index command
 * (`@defindex`), which only that end tells: `name` is then `-index`, which
 * no other name can be.
 */
static unsigned read_name(TSLexer *lexer, char *name) {
  static const char INDEX[] = "index";
  const unsigned tail_size = sizeof(INDEX) - 1;
  unsigned length = 0;
  // The last characters, the one read as the `length`th at `length` modulo
  // their count.
  char tail[sizeof(INDEX) - 1];
  if (!is_alpha(lexer->lookahead) && !is_digit(lexer->lookahead)) return 0;
  while (is_name_char(lexer->lookahead)) {
    if (length < MAX_NAME) name[length] = (char)lexer->lookahead;
    tail[length % tail_size] = (char)lexer->lookahead;
    length++;
    advance(lexer);
  }
  if (length < MAX_NAME) {
    name[length] = '\0';
    return length;
  }
  name[MAX_NAME] = '\0';
  for (unsigned i = 0; i < tail_size; i++) {
    if (tail[(length + i) % tail_size] != INDEX[i]) return 0;
  }
  memcpy(name, "-index", sizeof("-index"));
  return length;
}

// Whether two names are the same, comparing the first characters before
// calling strcmp, which most names compared differ in.
static inline bool same_name(const char *a, const char *b) {
  return a[0] == b[0] && strcmp(a, b) == 0;
}

/**
 * The index of `name` in `table`, of `count` entries of `size` bytes, each
 * starting with its name, sorted by strcmp; or -1.
 */
static int search(const void *table, size_t size, size_t count,
                  const char *name) {
  size_t low = 0;
  size_t high = count;
  while (low < high) {
    size_t middle = low + (high - low) / 2;
    const char *entry =
        *(const char *const *)((const char *)table + middle * size);
    // The first characters first, which most names compared differ in.
    int order = (unsigned char)entry[0] - (unsigned char)name[0];
    if (order == 0) order = strcmp(entry, name);
    if (order == 0) return (int)middle;
    if (order < 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  return -1;
}

static int find_block(const char *name) {
  return search(BLOCKS, sizeof(BLOCKS[0]), BLOCK_COUNT, name);
}

/**
 * After `@end`, reads the separating blanks and the block name. Returns false
 * if there is no name.
 */
static bool read_end_name(TSLexer *lexer, char *name) {
  if (!at_blank(lexer)) return false;
  skip_blanks(lexer);
  return read_name(lexer, name) > 0;
}

// What the scanner knows of a command other than a block, as flags.
enum {
  // Its argument is the rest of the line: texi2any's line commands.
  LINE = 1 << 0,
  // Ends a paragraph, as texi2any's close_paragraph_commands: a line
  // command (the others may be inside one), or one with braces, also in the
  // middle of a line, which starts none.
  ENDS_PARAGRAPH = 1 << 1,
  // A built-in brace command whose argument ends at a blank line, as in
  // texi2any: all of them except @verb, the context commands and @math, and
  // the inline conditionals (@inlinefmt, ...), whose skipped arguments may
  // contain blank lines. Only these may have blanks before their brace. Any
  // other command, a macro call or a command texi2any doesn't know, also
  // ends at a blank line (in grammar.js), although in texi2any a macro
  // call's arguments may contain one.
  BRACE = 1 << 2,
  // An accent texi2any also accepts without braces: `@u o`, as `@'e`.
  ACCENT = 1 << 3,
  // Starts no paragraph, as texi2any's no_paragraph_commands, but the line
  // and block commands and those the grammar recognises otherwise: where one
  // would start, it is outside it. One that is a brace command too is one
  // where a paragraph is open.
  NO_PARAGRAPH = 1 << 4,
  // `@footnote`, `@caption`, `@shortcaption`, whose text is paragraphs.
  CONTEXT = 1 << 5,
  // `@c` and `@comment`.
  COMMENT = 1 << 6,
  // `@item`, `@itemx`, `@headitem`, `@tab`.
  ITEM = 1 << 7,
  // `@node` and `@bye`, which, like the sectioning commands, close the
  // brace commands and blocks left open before them, as in texi2any ("@node
  // seen before @end menu").
  ROOT = 1 << 8,
  // A sectioning command, with its level (LEVEL).
  SECTION = 1 << 9,
  // A keyword of a line's command in the grammar, which the scanner gives no
  // token (`@set`, `@item` in a table, `@heading`), and which starts no
  // paragraph. Definition lines (`@deffnx`, `@defline`), which start with
  // `def`, aren't listed.
  KEYWORD = 1 << 10,
};

// A sectioning command's level, in its flags: the index of its section in
// SECTIONS in grammar.js (part, top, chapter, section, subsection,
// subsubsection), or 6 for `@appendix`, a chapter whose node has a token of
// its own, as a part tells it from its other chapters.
#define LEVEL(level) ((level) << 12)

typedef struct {
  const char *name;
  uint16_t flags;
} Command;

// The commands but the blocks that the scanner tells apart, from texi2any's
// tables (script/check-scanner.py checks them). Sorted, for a binary search.
static const Command COMMANDS[] = {
    {"AA", BRACE},
    {"AE", BRACE},
    {"DH", BRACE},
    {"H", BRACE | ACCENT},
    {"L", BRACE},
    {"LaTeX", BRACE},
    {"O", BRACE},
    {"OE", BRACE},
    {"TH", BRACE},
    {"TeX", BRACE},
    {"U", BRACE},
    {"aa", BRACE},
    {"ae", BRACE},
    {"afivepaper", LINE},
    {"afourlatex", LINE},
    {"afourpaper", LINE},
    {"afourwide", LINE},
    {"alias", KEYWORD},
    {"allowcodebreaks", LINE},
    {"ampchar", BRACE},
    {"anchor", BRACE | NO_PARAGRAPH},
    {"appendix", SECTION | LEVEL(6)},
    {"appendixsec", SECTION | LEVEL(3)},
    {"appendixsection", SECTION | LEVEL(3)},
    {"appendixsubsec", SECTION | LEVEL(4)},
    {"appendixsubsubsec", SECTION | LEVEL(5)},
    {"arrow", BRACE},
    {"asis", BRACE},
    {"atchar", BRACE},
    {"author", LINE},
    {"b", BRACE},
    {"backslashchar", BRACE},
    {"bsixpaper", LINE},
    {"bullet", BRACE},
    {"bye", ROOT | KEYWORD},
    {"c", COMMENT},
    {"caption", ENDS_PARAGRAPH | CONTEXT},
    {"center", LINE | ENDS_PARAGRAPH},
    {"centerchap", SECTION | LEVEL(2)},
    {"chapheading", KEYWORD},
    {"chapter", SECTION | LEVEL(2)},
    {"cindex", LINE},
    {"cite", BRACE},
    {"clear", KEYWORD},
    {"click", BRACE},
    {"clicksequence", BRACE},
    {"clickstyle", KEYWORD},
    {"code", BRACE},
    {"codequotebacktick", LINE},
    {"codequoteundirected", LINE},
    {"columnfractions", KEYWORD},
    {"comma", BRACE},
    {"command", BRACE},
    {"comment", COMMENT},
    {"contents", LINE | ENDS_PARAGRAPH},
    {"copyright", BRACE},
    {"cpindex", LINE},
    {"defcodeindex", LINE},
    {"defindex", LINE},
    {"definfoenclose", LINE},
    {"deftypefnnewline", LINE},
    {"dfn", BRACE},
    {"dh", BRACE},
    {"dircategory", LINE | ENDS_PARAGRAPH},
    {"dmn", BRACE},
    {"documentencoding", LINE},
    {"documentlanguage", LINE},
    {"dotaccent", BRACE | ACCENT},
    {"dotless", BRACE | ACCENT},
    {"dots", BRACE},
    {"emph", BRACE},
    {"enddots", BRACE},
    {"env", BRACE},
    {"equiv", BRACE},
    {"error", BRACE},
    {"errormsg", BRACE | NO_PARAGRAPH},
    {"euro", BRACE},
    {"evenfooting", LINE},
    {"evenfootingmarks", LINE},
    {"evenheading", LINE},
    {"evenheadingmarks", LINE},
    {"everyfooting", LINE},
    {"everyfootingmarks", LINE},
    {"everyheading", LINE},
    {"everyheadingmarks", LINE},
    {"exampleindent", LINE},
    {"exclamdown", BRACE},
    {"exdent", LINE | ENDS_PARAGRAPH},
    {"expansion", BRACE},
    {"file", BRACE},
    {"finalout", LINE},
    {"findex", LINE},
    {"firstparagraphindent", LINE},
    {"fnindex", LINE},
    {"fonttextsize", LINE},
    {"footnote", CONTEXT},
    {"footnotestyle", LINE},
    {"frenchspacing", LINE},
    {"geq", BRACE},
    {"guillemetleft", BRACE},
    {"guillemetright", BRACE},
    {"guillemotleft", BRACE},
    {"guillemotright", BRACE},
    {"guilsinglleft", BRACE},
    {"guilsinglright", BRACE},
    {"hashchar", BRACE},
    {"heading", KEYWORD},
    {"headings", LINE},
    {"headitem", ITEM | KEYWORD},
    {"headitemfont", BRACE},
    {"hyphenation", BRACE | NO_PARAGRAPH},
    {"i", BRACE},
    {"include", LINE},
    {"indent", NO_PARAGRAPH},
    {"indicateurl", BRACE},
    {"insertcopying", LINE | ENDS_PARAGRAPH},
    {"item", ITEM | KEYWORD},
    {"itemx", ITEM | KEYWORD},
    {"kbd", BRACE},
    {"kbdinputstyle", LINE},
    {"key", BRACE},
    {"kindex", LINE},
    {"kyindex", LINE},
    {"l", BRACE},
    {"lbracechar", BRACE},
    {"leq", BRACE},
    {"listoffloats", LINE | ENDS_PARAGRAPH},
    {"lowersections", LINE},
    {"majorheading", KEYWORD},
    {"maketitle", LINE | ENDS_PARAGRAPH},
    {"microtype", LINE},
    {"minus", BRACE},
    {"need", LINE | ENDS_PARAGRAPH},
    {"node", ROOT},
    {"nodedescription", LINE | ENDS_PARAGRAPH | KEYWORD},
    {"noindent", NO_PARAGRAPH},
    {"novalidate", LINE},
    {"o", BRACE},
    {"oddfooting", LINE},
    {"oddfootingmarks", LINE},
    {"oddheading", LINE},
    {"oddheadingmarks", LINE},
    {"oe", BRACE},
    {"ogonek", BRACE | ACCENT},
    {"option", BRACE},
    {"ordf", BRACE},
    {"ordm", BRACE},
    {"page", LINE | ENDS_PARAGRAPH},
    {"pagesizes", LINE},
    {"paragraphindent", LINE},
    {"part", SECTION | LEVEL(0)},
    {"pgindex", LINE},
    {"pindex", LINE},
    {"point", BRACE},
    {"pounds", BRACE},
    {"print", BRACE},
    {"printindex", LINE | ENDS_PARAGRAPH},
    {"questiondown", BRACE},
    {"quotedblbase", BRACE},
    {"quotedblleft", BRACE},
    {"quotedblright", BRACE},
    {"quoteleft", BRACE},
    {"quoteright", BRACE},
    {"quotesinglbase", BRACE},
    {"r", BRACE},
    {"raisesections", LINE},
    {"rbracechar", BRACE},
    {"refill", NO_PARAGRAPH},
    {"registeredsymbol", BRACE},
    {"result", BRACE},
    {"ringaccent", BRACE | ACCENT},
    {"samp", BRACE},
    {"sansserif", BRACE},
    {"sc", BRACE},
    {"section", SECTION | LEVEL(3)},
    {"seealso", BRACE},
    {"seeentry", BRACE},
    {"set", KEYWORD},
    {"setchapternewpage", LINE},
    {"setfilename", LINE | ENDS_PARAGRAPH},
    {"settitle", LINE},
    {"shortcaption", ENDS_PARAGRAPH | CONTEXT},
    {"shortcontents", LINE | ENDS_PARAGRAPH},
    {"shorttitlepage", LINE},
    {"slanted", BRACE},
    {"smallbook", LINE},
    {"sortas", BRACE},
    {"sp", LINE | ENDS_PARAGRAPH},
    {"ss", BRACE},
    {"strong", BRACE},
    {"sub", BRACE},
    {"subheading", KEYWORD},
    {"subsection", SECTION | LEVEL(4)},
    {"subsubheading", KEYWORD},
    {"subsubsection", SECTION | LEVEL(5)},
    {"subtitle", LINE},
    {"summarycontents", LINE | ENDS_PARAGRAPH},
    {"sup", BRACE},
    {"syncodeindex", LINE},
    {"synindex", LINE},
    {"t", BRACE},
    {"tab", ITEM},
    {"textdegree", BRACE},
    {"th", BRACE},
    {"tie", BRACE},
    {"tieaccent", BRACE | ACCENT},
    {"tindex", LINE},
    {"title", LINE},
    {"titlefont", ENDS_PARAGRAPH | BRACE | NO_PARAGRAPH},
    {"today", BRACE},
    {"top", SECTION | LEVEL(1)},
    {"tpindex", LINE},
    {"u", BRACE | ACCENT},
    {"ubaraccent", BRACE | ACCENT},
    {"udotaccent", BRACE | ACCENT},
    {"unmacro", KEYWORD},
    {"unnumbered", SECTION | LEVEL(2)},
    {"unnumberedsec", SECTION | LEVEL(3)},
    {"unnumberedsubsec", SECTION | LEVEL(4)},
    {"unnumberedsubsubsec", SECTION | LEVEL(5)},
    {"urefbreakstyle", LINE},
    {"v", BRACE | ACCENT},
    {"value", BRACE},
    {"var", BRACE},
    {"verbatiminclude", LINE | ENDS_PARAGRAPH},
    {"vindex", LINE},
    {"vrindex", LINE},
    {"vskip", KEYWORD},
    {"w", BRACE},
    {"xrefautomaticsectiontitle", LINE},
    {"xrefname", LINE | ENDS_PARAGRAPH},
};

#define COMMAND_COUNT (sizeof(COMMANDS) / sizeof(COMMANDS[0]))

// A command's name as read, with what the tables say of it.
typedef struct {
  char text[MAX_NAME + 1];
  // The characters read, which a name too long for `text` has more of.
  unsigned length;
  uint16_t flags;
  // Its index in BLOCKS, or -1.
  int block;
} Name;

/**
 * Reads a command's name (read_name) and looks it up, in COMMANDS, then in
 * BLOCKS. Returns false if there is none. One that isn't in them but ends in
 * `index` is a line command: a user-defined index command (`@defindex op`
 * makes `@opindex`), which texi2any treats as an index entry.
 */
static bool read_command(TSLexer *lexer, Name *name) {
  name->length = read_name(lexer, name->text);
  if (name->length == 0) return false;
  name->flags = 0;
  name->block = -1;
  int index = search(COMMANDS, sizeof(COMMANDS[0]), COMMAND_COUNT, name->text);
  if (index >= 0) {
    name->flags = COMMANDS[index].flags;
    return true;
  }
  name->block = find_block(name->text);
  size_t length = strlen(name->text);
  if (name->block < 0 && length > 5 &&
      memcmp(name->text + length - 5, "index", 5) == 0) {
    name->flags = LINE;
  }
  return true;
}

// The block that `name` opens, or NULL.
static inline const Block *opened_block(const Name *name) {
  return name->block >= 0 ? &BLOCKS[name->block] : NULL;
}

// A line command that ends a paragraph.
static inline bool ends_paragraph(const Name *name) {
  return (name->flags & (LINE | ENDS_PARAGRAPH)) == (LINE | ENDS_PARAGRAPH);
}

// `@node`, a sectioning command or `@bye`.
static inline bool is_root_command(const Name *name) {
  return name->flags & (ROOT | SECTION);
}

/**
 * The level of the sectioning command `name` (LEVEL), or -1 if it is none,
 * moved by `shift`, the `@raisesections` before it less the
 * `@lowersections`, as in texi2any: no higher than a chapter nor lower
 * than a subsubsection, `@appendix` staying one at the chapter's level, and
 * `@part` and `@top` not at all.
 */
static int section_level(const Name *name, int shift) {
  if (!(name->flags & SECTION)) return -1;
  int level = name->flags >> 12;
  if (level < 2 || shift == 0) return level;
  // texi2any's level, from the chapter's 1 to the subsubsection's 4.
  int moved = (level == 6 ? 1 : level - 1) - shift;
  if (moved <= 1) return level == 6 ? 6 : 2;
  return moved >= 4 ? 5 : moved + 1;
}

// Counts `@raisesections` and `@lowersections` into `shift`.
static void count_section_shift(int8_t *shift, const Name *name) {
  if (!(name->flags & LINE)) return;
  if (same_name(name->text, "raisesections") && *shift < INT8_MAX) (*shift)++;
  if (same_name(name->text, "lowersections") && *shift > INT8_MIN) (*shift)--;
}

/**
 * Accepts the token of a `@node` line before the sectioning command `name`,
 * if it is one, moved by `shift`: the node token for that section's level.
 * Not before a part, which no node is associated with (the Texinfo manual),
 * as the grammar knows.
 */
static bool accept_node_before(TSLexer *lexer, const bool *valid_symbols,
                               const Name *name, int shift) {
  int level = section_level(name, shift);
  if (level <= 0) return false;
  enum TokenType token = NODE_BEFORE + level;
  if (!valid_symbols[token]) return false;
  lexer->result_symbol = (TSSymbol)token;
  return true;
}

/**
 * Scans `@node` when the next line, after blank and comment lines, the
 * node's description (`@nodedescription`, `@nodedescriptionblock`),
 * `@anchor` lines, line commands such as index entries, and lines of text,
 * is a sectioning command: emits the node token for that section's level,
 * so the node line starts the section, as texi2any associates them. A block,
 * an `@end` or an item between them leaves the node on its own: skipping one
 * would mean matching its `@end`, past raw text that may hold anything. The
 * `@node` has already been read; otherwise it is left to the grammar as a
 * plain node line.
 */
static bool scan_node_before_section(const Scanner *scanner, TSLexer *lexer,
                                     const bool *valid_symbols) {
  // With the `@raisesections` and `@lowersections` between them.
  int8_t shift = scanner->section_shift;
  lexer->mark_end(lexer);
  skip_to_next_line(lexer);
  for (;;) {
    skip_blanks(lexer);
    if (lexer->eof(lexer)) return false;
    if (lexer->lookahead == '\n' || lexer->lookahead == '\r') {
      skip_to_next_line(lexer);
      continue;
    }
    // A line of text, which may start with a symbol command.
    if (lexer->lookahead != '@') {
      skip_to_next_line(lexer);
      continue;
    }
    advance(lexer);
    Name name;
    if (!read_command(lexer, &name)) {
      skip_to_next_line(lexer);
      continue;
    }

    // A comment, whatever follows its name, as in texi2any, or a line
    // command, such as an index entry (`@cindex`, `@need 5000`) or the
    // node's description.
    if (name.flags & (COMMENT | LINE)) {
      count_section_shift(&shift, &name);
      skip_to_next_line(lexer);
      continue;
    }

    if (same_name(name.text, "nodedescriptionblock")) {
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
        if (!read_command(lexer, &name)) continue;
        if (is_root_command(&name)) {
          return accept_node_before(lexer, valid_symbols, &name, shift);
        }
        char end_name[MAX_NAME + 1];
        if (same_name(name.text, "end") && read_end_name(lexer, end_name) &&
            same_name(end_name, "nodedescriptionblock")) {
          break;
        }
      }
      skip_to_next_line(lexer);
      continue;
    }

    if (is_root_command(&name)) {
      return accept_node_before(lexer, valid_symbols, &name, shift);
    }
    // Nor a definition line (`@deffnx`, `@defline`), whose names but the
    // blocks' and index commands' start with `def`.
    if (name.block >= 0 || (name.flags & ITEM) || same_name(name.text, "end") ||
        strncmp(name.text, "def", 3) == 0) {
      return false;
    }
    // Text that starts with a command, such as `@anchor{Old name}`, which
    // keeps an old node name working.
    skip_to_next_line(lexer);
  }
}

/** The innermost open block; there must be one. */
static inline const Block *innermost_block(const Scanner *scanner) {
  return &BLOCKS[scanner->stack[scanner->depth - 1]];
}

/** Pops the innermost block, whose `@end NAME` has just been read. */
static bool pop_block(Scanner *scanner, TSLexer *lexer) {
  scanner->depth--;
  return accept(lexer, BLOCK_END);
}

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
    while (at_blank(lexer) || lexer->lookahead == '\r') {
      advance(lexer);
    }
    if (lexer->lookahead != '@') break;
    advance(lexer);
    Name name;
    if (!read_command(lexer, &name) || !(name.flags & COMMENT)) return true;
    skip_to_next_line(lexer);
  }
  return !lexer->eof(lexer) && lexer->lookahead != '\n';
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
  Name name;
  if (!read_command(lexer, &name)) return true;
  return name.block < 0 && !same_name(name.text, "end") &&
         !(name.flags & (COMMENT | ITEM | ROOT | SECTION | LINE));
}

/**
 * After the name of a block at the start of the line after a paragraph's,
 * which ends the paragraph or goes on with it: reads a conditional's first
 * line, as the conditional's own scan does (scan_block_start), so that an
 * edit there makes tree-sitter scan the paragraph's line end again too.
 * Otherwise it reused the paragraph, ended, where the conditional now
 * starts with text and goes on with it.
 */
static void read_conditional_line(TSLexer *lexer, const Name *name) {
  const Block *block = opened_block(name);
  if (block != NULL && block->kind == KIND_CONDITIONAL) {
    starts_with_text(lexer);
  }
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
  while (at_blank(lexer) || lexer->lookahead == '\r') {
    skip(lexer);
  }
  if (accent && is_accent_argument(lexer)) {
    advance(lexer);
    return accept(lexer, ACCENT_ARGUMENT);
  }
  if (lexer->lookahead != '@') return false;
  advance(lexer);
  Name name;
  if (!read_command(lexer, &name)) return false;
  if (!(name.flags & COMMENT)) {
    read_conditional_line(lexer, &name);
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
  while (at_blank(lexer)) skip(lexer);
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
  while (at_blank(lexer) || lexer->lookahead == '\r') skip(lexer);
  if (lexer->lookahead == '\n') {
    skip(lexer);
    while (at_blank(lexer)) skip(lexer);
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
  while (at_blank(lexer)) skip(lexer);
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
    if (block->kind != KIND_CONDITIONAL) return block;
  }
  return NULL;
}

// The kind of the innermost open block other than conditionals, or
// KIND_BLOCK.
static BlockKind innermost_kind(const Scanner *scanner) {
  const Block *block = innermost_structure(scanner);
  return block != NULL ? block->kind : KIND_BLOCK;
}

// Inside a preformatted block, where a blank line doesn't end a brace
// command. Every block nested in one, such as `@group` or `@quotation`, keeps
// it preformatted, as in texi2any.
static bool in_preformatted(const Scanner *scanner) {
  for (unsigned i = 0; i < scanner->depth; i++) {
    if (BLOCKS[scanner->stack[i]].kind == KIND_PREFORMATTED) return true;
  }
  return false;
}

/**
 * Whether the command `name`, whose token is `token`, starts a paragraph
 * where one may start: a brace command, `@math`, `@footnote`, or a command
 * the scanner gives no token, but a block, a line's command, a comment or a
 * command that starts none (`@image`).
 */
static bool starts_paragraph(const Name *name, int token) {
  if (token == BRACE_COMMAND_NAME || token == MATH_NAME ||
      token == CONTEXT_COMMAND_NAME) {
    return true;
  }
  return token < 0 && name->block < 0 &&
         !(name->flags & (ROOT | SECTION | COMMENT | KEYWORD)) &&
         !same_name(name->text, "end") && strncmp(name->text, "def", 3) != 0 &&
         !same_name(name->text, "image") &&
         !same_name(name->text, "namedanchor");
}

// A symbol command's character after its `@` (the grammar's
// `symbol_command`).
static bool is_symbol_char(int32_t c) {
  return c != 0 && strchr("\t !\"&'*,-./:=?@\\^`{|}~", (int)c) != NULL;
}

// Whether a form feed among the blanks skipped ends a paragraph: in the
// middle of its line, as in texi2any, where it is an empty line. Not at the
// start of a line, where it is a blank, nor in a preformatted block or in
// braces, where it is text.
static bool form_feed_ends_paragraph(const Scanner *scanner, LineStart *start,
                                     const bool *valid_symbols) {
  return start->form_feed && valid_symbols[PARAGRAPH_END_BEFORE_COMMAND] &&
         !in_preformatted(scanner) && !starts_line(start);
}

// Whether `name` starts a list's next item or a multitable's next cell,
// which ends a paragraph and an ordinary brace command left open before it,
// as in texi2any: `@item` in any list, `@itemx` in a table, `@headitem` and
// `@tab` in a multitable. texi2any reports the others as outside of a table
// or list or not meaningful in it.
static bool starts_item(const Scanner *scanner, const Name *name) {
  if (!(name->flags & ITEM)) return false;
  bool itemx = same_name(name->text, "itemx");
  switch (innermost_kind(scanner)) {
    case KIND_MULTITABLE:
      return !itemx;
    case KIND_ITEM_LINE:
      return itemx || same_name(name->text, "item");
    case KIND_ITEM_CONTAINER:
      return same_name(name->text, "item");
    default:
      return false;
  }
}

/**
 * Whether an item command that starts an item or a cell in its list
 * (starts_item) means nothing where it is, right after the list's head,
 * where the grammar takes it: an `@itemx` or `@tab` before any item, as
 * texi2any reports ("@itemx should not begin @table", "@tab before
 * @item"). Not later, where a macro call may have made an item.
 */
static bool leading_item_command(const Scanner *scanner, const Name *name) {
  return (same_name(name->text, "itemx") || same_name(name->text, "tab")) &&
         starts_item(scanner, name);
}

/**
 * Scans the line end of a blank or comment line right after a list's head
 * when more of them, then an `@itemx` or `@tab` that means nothing there
 * (leading_item_command), follow: a token of its own, which the next lines
 * decide, as the grammar takes such a command only right after the head.
 */
static bool scan_leading_newline(const Scanner *scanner, TSLexer *lexer) {
  advance(lexer);
  lexer->mark_end(lexer);
  lexer->result_symbol = LEADING_NEWLINE;
  for (;;) {
    while (at_blank(lexer) || lexer->lookahead == '\r') advance(lexer);
    if (lexer->lookahead == '\n') {
      advance(lexer);
      continue;
    }
    if (lexer->lookahead != '@') return false;
    advance(lexer);
    Name name;
    if (!read_command(lexer, &name)) return false;
    if (name.flags & COMMENT) {
      skip_to_next_line(lexer);
      continue;
    }
    return leading_item_command(scanner, &name);
  }
}

/**
 * Scans the rest of the line after the `@end` of a block in a paragraph, up
 * to a comment, as one text: texi2any reports it, keeping it in the `@end`
 * line. A comment right after the blanks fails the scan, as the second stage
 * would at it: the internal lexer takes it.
 */
static ScanResult scan_end_line_text(TSLexer *lexer) {
  while (at_blank(lexer)) skip(lexer);
  bool found = false;
  for (;;) {
    int32_t c = lexer->lookahead;
    if (lexer->eof(lexer) || c == '\n' || c == '\r') break;
    if (c == '@') {
      // The text ends before a comment.
      lexer->mark_end(lexer);
      advance(lexer);
      Name name;
      if (read_command(lexer, &name) && (name.flags & COMMENT)) {
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
 * Scans a block opener such as `@example` at the start of a line. The `@` has
 * already been consumed.
 */
static bool scan_block_start(Scanner *scanner, TSLexer *lexer,
                             const bool *valid_symbols, const Name *name) {
  const Block *block = opened_block(name);
  if (block == NULL) return false;

  // The opener's token for each kind of block, in BlockKind's order.
  static const enum TokenType START_TOKENS[] = {
      BLOCK_START,      BLOCK_START, BLOCK_START,     LIST_START,  LIST_START,
      MULTITABLE_START, FLOAT_START, RAW_BLOCK_START, MACRO_START, MENU_START};
  BlockKind kind = block->kind;
  enum TokenType token = kind >= KIND_DEFINITION
                             ? DEFINITION_START + (kind - KIND_DEFINITION)
                             : START_TOKENS[kind];
  // In a menu entry's description, which goes on past a block, but not past
  // a conditional, which holds the next entries.
  if ((kind == KIND_BLOCK || kind == KIND_PREFORMATTED) &&
      valid_symbols[DESCRIPTION_BLOCK_START]) {
    token = DESCRIPTION_BLOCK_START;
  }
  // At the start of a paragraph's next line, which goes on past a raw block
  // but `@verbatim`, the one of them in texi2any's close_paragraph_commands.
  if (kind == KIND_RAW && valid_symbols[PARAGRAPH_RAW_BLOCK_START] &&
      !same_name(name->text, "verbatim")) {
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
  if (kind == KIND_CONDITIONAL && valid_symbols[PARAGRAPH_CONDITIONAL_START] &&
      starts_with_text(lexer)) {
    token = PARAGRAPH_CONDITIONAL_START;
  }
  if (!valid_symbols[token]) return false;
  if (scanner->depth >= MAX_DEPTH) return false;

  scanner->stack[scanner->depth++] = (uint8_t)name->block;
  lexer->result_symbol = (TSSymbol)token;
  return true;
}

// Whether `block` (an index in BLOCKS) is the innermost open block.
static bool is_innermost(const Scanner *scanner, int block) {
  return scanner->depth > 0 && scanner->stack[scanner->depth - 1] == block;
}

// Whether `block` (an index in BLOCKS) is open, other than the innermost
// block.
static bool encloses(const Scanner *scanner, int block) {
  for (unsigned i = scanner->depth > 0 ? scanner->depth - 1 : 0; i > 0; i--) {
    if (scanner->stack[i - 1] == block) return true;
  }
  return false;
}

// Accepts the rest of an `@end` line that ends no open block, as `token`.
static bool accept_unmatched_end(TSLexer *lexer, enum TokenType token) {
  while (!lexer->eof(lexer) && lexer->lookahead != '\n' &&
         lexer->lookahead != '\r') {
    advance(lexer);
  }
  return accept(lexer, token);
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
  int block = read_end_name(lexer, end_name) ? find_block(end_name) : -1;
  // An `@end` of no block, or without a name, which texi2any warns of
  // ("unknown @end", "@end missing argument") and ignores: it closes no
  // block, nor the paragraph or the brace command it is in, so that typing
  // the name of an `@end` changes nothing around it.
  if (block < 0) {
    if (valid_symbols[PARAGRAPH_UNMATCHED_END]) {
      return accept_unmatched_end(lexer, PARAGRAPH_UNMATCHED_END);
    }
    return valid_symbols[UNMATCHED_END] &&
           accept_unmatched_end(lexer, UNMATCHED_END);
  }
  bool matches = is_innermost(scanner, block);
  // A brace command still open at the start of the line closes first, with
  // a zero-width token before the `@end`, which is then read again; also at
  // the `@end` of an enclosing block, which closes the blocks inside it.
  if ((matches || encloses(scanner, block)) && brace_at_line_start &&
      valid_symbols[BRACE_CLOSE_AT_BLOCK_END]) {
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
  // texi2any, with a zero-width end each; the `@end` is then read again. An
  // `@end` of a block that isn't open closes the open blocks too, as in
  // texi2any, before it is reported as unmatched; but not conditionals,
  // which texi2any has evaluated.
  if (valid_symbols[MISSING_END] && scanner->depth > 0 &&
      (encloses(scanner, block) ||
       innermost_block(scanner)->kind != KIND_CONDITIONAL)) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }
  return valid_symbols[UNMATCHED_END] &&
         accept_unmatched_end(lexer, UNMATCHED_END);
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

  // Names expected by nested `@end`s, innermost last: a macro definition's
  // nested macros' names; in another raw block, its own, which only needs
  // counting.
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
          const char *expected = nested_depth > 0 && block->kind == KIND_MACRO
                                     ? nested[nested_depth - 1]
                                     : block->name;
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
        } else if (at_blank(lexer) || lexer->lookahead == '\n' ||
                   lexer->lookahead == '\r' || lexer->lookahead == '@') {
          // `texi2any` requires a separator after a nested opener.
          if (block->kind == KIND_MACRO) {
            int index = find_block(name);
            if (index >= 0 && BLOCKS[index].kind == KIND_MACRO &&
                nested_depth < MAX_RAW_DEPTH) {
              nested[nested_depth++] = BLOCKS[index].name;
            }
          } else if (strcmp(name, block->name) == 0) {
            // KIND_RAW, the only other kind with a raw body.
            nested_depth++;
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
static bool closes_brace(const Name *name) {
  const Block *block = opened_block(name);
  if (block == NULL || block->kind == KIND_CONDITIONAL) return false;
  return block->kind != KIND_RAW || same_name(name->text, "verbatim");
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
    while (at_blank(lexer)) skip(lexer);
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

  if (lexer->lookahead != '@') return false;
  advance(lexer);
  Name name;
  if (!read_command(lexer, &name)) return false;

  // Comment lines inside a paragraph, which don't close the brace there.
  // Before a blank line, which closes an ordinary brace command after them,
  // they are left in its argument; in other braces (`@footnote`), they stay
  // in the paragraph, as in texi2any.
  if (name.flags & COMMENT) {
    if (!valid_symbols[NEWLINE_BEFORE_COMMENTS]) return false;
    skip_to_next_line(lexer);
    if (paragraph_end && !more_comment_lines(lexer)) return false;
    lexer->result_symbol = NEWLINE_BEFORE_COMMENTS;
    return true;
  }

  if (same_name(name.text, "end")) {
    char end_name[MAX_NAME + 1];
    // The end of the innermost block, or of an enclosing one, which closes
    // the blocks inside it, as in texi2any.
    if (!block_end || !read_end_name(lexer, end_name)) return false;
    int block = find_block(end_name);
    if (block < 0 ||
        (!is_innermost(scanner, block) && !encloses(scanner, block))) {
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
  if (is_root_command(&name)) {
    lexer->result_symbol =
        paragraph_end ? BRACE_CLOSE_AT_PARAGRAPH_END : BRACE_CLOSE_AT_BLOCK_END;
    return paragraph_end || block_end;
  }

  if (!paragraph_end || !closes_brace(&name)) {
    read_conditional_line(lexer, &name);
    return false;
  }
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
  buffer[size++] = (char)scanner->after_brace_close;
  buffer[size++] = (char)scanner->section_shift;
  memcpy(buffer + size, scanner->stack, scanner->depth);
  return size + scanner->depth;
}

void tree_sitter_texinfo_external_scanner_deserialize(void *payload,
                                                      const char *buffer,
                                                      unsigned length) {
  Scanner *scanner = payload;
  scanner->verb_delimiter = 0;
  scanner->after_accent = false;
  scanner->after_brace_close = false;
  scanner->section_shift = 0;
  scanner->depth = 0;
  if (length < STATE_HEADER) return;
  unsigned size = 0;
  memcpy(&scanner->verb_delimiter, buffer, sizeof(int32_t));
  size += sizeof(int32_t);
  scanner->after_accent = buffer[size++];
  scanner->after_brace_close = buffer[size++];
  scanner->section_shift = (int8_t)buffer[size++];
  scanner->depth = length - size;
  if (scanner->depth > MAX_DEPTH) scanner->depth = MAX_DEPTH;
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
    Name name;
    if (read_command(lexer, &name) && (name.flags & COMMENT)) {
      return SCAN_FAILED;
    }
  }
  return SCAN_FOUND;
}

/**
 * The token of the command `name` where the grammar allows it, other than a
 * block's, or -1. Of the classes of commands (COMMANDS), at most one of a
 * line command, a brace command, a context command and an item applies to a
 * name, and the names compared here are of none (script/check-scanner.py
 * checks that): only the context tells `@item`'s two apart, and a command
 * that starts no paragraph is a brace command where one is open.
 */
static int command_token(const Scanner *scanner, const bool *valid_symbols,
                         const Name *name) {
  // A brace command that ends a paragraph, also in the middle of a line, and
  // starts none: zero-width before it where a paragraph may end, which it
  // then ends, or its name where a paragraph would start, also at the start
  // of a paragraph's next line.
  if ((name->flags & (ENDS_PARAGRAPH | LINE)) == ENDS_PARAGRAPH) {
    if (valid_symbols[PARAGRAPH_END_BEFORE_COMMAND]) {
      return PARAGRAPH_END_BEFORE_COMMAND;
    }
    enum TokenType token = name->flags & CONTEXT ? NO_PARAGRAPH_CONTEXT_NAME
                                                 : NO_PARAGRAPH_COMMAND_NAME;
    if (valid_symbols[token]) return token;
  }
  // `@image` and `@namedanchor`, keywords in the grammar, where a paragraph
  // would start, which they start none: zero-width before them. In an open
  // paragraph, they are in it, as `@anchor` is.
  if (valid_symbols[NO_PARAGRAPH_START] &&
      !valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] &&
      (same_name(name->text, "image") ||
       same_name(name->text, "namedanchor"))) {
    return NO_PARAGRAPH_START;
  }
  if (name->flags & ITEM) {
    bool item = same_name(name->text, "item");
    if (valid_symbols[ROW_NAME] &&
        (item || same_name(name->text, "headitem")) &&
        innermost_kind(scanner) == KIND_MULTITABLE) {
      return ROW_NAME;
    }
    if (valid_symbols[CONTAINER_ITEM_NAME] && item &&
        innermost_kind(scanner) == KIND_ITEM_CONTAINER) {
      return CONTAINER_ITEM_NAME;
    }
    if (valid_symbols[TAB] && same_name(name->text, "tab")) return TAB;
  }
  if (valid_symbols[MATH_NAME] && same_name(name->text, "math")) {
    return MATH_NAME;
  }
  if (valid_symbols[CONTEXT_COMMAND_NAME] && (name->flags & CONTEXT)) {
    return CONTEXT_COMMAND_NAME;
  }
  if (valid_symbols[SUBENTRY_NAME] && same_name(name->text, "subentry")) {
    return SUBENTRY_NAME;
  }
  if (name->flags & LINE) {
    if (valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] && !ends_paragraph(name)) {
      return PARAGRAPH_LINE_COMMAND_NAME;
    }
    if (valid_symbols[LINE_COMMAND_NAME]) return LINE_COMMAND_NAME;
  }
  // Where a paragraph's next line may go on with it (line commands inside it
  // are valid), a paragraph is open, which a command that starts none is in.
  if (valid_symbols[NO_PARAGRAPH_COMMAND_NAME] &&
      !valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] &&
      (name->flags & NO_PARAGRAPH)) {
    return NO_PARAGRAPH_COMMAND_NAME;
  }
  if (valid_symbols[BRACE_COMMAND_NAME] && (name->flags & BRACE)) {
    return BRACE_COMMAND_NAME;
  }
  return -1;
}

/**
 * Whether the scan is in raw text that the grammar's own tokens read, such
 * as `@set`'s value or what follows `@bye`, where no external token is valid
 * but the end of the file and the extras. Wherever a command may be, some
 * other is: the name of a brace command, or, after a reduction that reused
 * a paragraph, an item's or a block's end. Not a test of one of them, which
 * would choose differently before the reduction and after it.
 */
static bool in_raw_text(const bool *valid_symbols) {
  for (int token = 0; token < ERROR_SENTINEL; token++) {
    if (valid_symbols[token] && token != END_OF_FILE &&
        token != MISPLACED_COMMAND) {
      return false;
    }
  }
  return true;
}

/**
 * Scans in two stages. First the tokens that only some contexts allow: an
 * accent's argument, `@verb`'s parts, the glue in a definition word, raw
 * text, and the zero-width ends of a brace, paragraph or block left open.
 * Then a command at `@`, by its name. The first stage only skips blanks
 * before giving up, so the second sees the same text; a scan of it that has
 * read further fails the whole scan, as a scan can't go back.
 * `after_brace_close` is whether the last token was a zero-width close of a
 * brace.
 */
static bool scan(Scanner *scanner, TSLexer *lexer, const bool *valid_symbols,
                 bool after_brace_close) {
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
      if (!at_blank(lexer)) return false;
      lexer->result_symbol = MENU_ENTRY_START;
      return true;
    }
  }

  // Valid only in a menu node, after the entry's `:`, never at column 0, so
  // skipping blanks here changes nothing below.
  if (valid_symbols[MENU_NODE_DOTS]) {
    while (at_blank(lexer)) skip(lexer);
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
    while (at_blank(lexer)) skip(lexer);
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
  LineStart start = {lexer, 0, -1, false};
  bool paragraph_end = valid_symbols[PARAGRAPH_END_AT_LINE_START];
  bool brace_close = valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] ||
                     valid_symbols[BRACE_CLOSE_AT_BLOCK_END] ||
                     valid_symbols[BRACE_CLOSE_AT_LINE_END];

  // Right after a brace closed at a blank line there, the paragraph ends
  // too; at an `@end` or a block, it does so when the name has been read.
  // Only a brace closed there ends a paragraph's line at the start of the
  // next: elsewhere, at the end of each of its lines, the column isn't
  // asked, which tree-sitter counts by reading the line again.
  if (paragraph_end) {
    lexer->mark_end(lexer);
    skip_counted(&start, false);
    if ((lexer->lookahead == '\n' || lexer->lookahead == '\r') &&
        after_brace_close && starts_line(&start)) {
      lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
      return true;
    }
  }

  // Inside braces, where the newline may close one instead, the scan of a
  // missing brace looks for comment lines too.
  if (valid_symbols[NEWLINE_BEFORE_COMMENTS] && !brace_close) {
    skip_counted(&start, true);
    if (form_feed_ends_paragraph(scanner, &start, valid_symbols)) {
      return accept(lexer, PARAGRAPH_END_BEFORE_COMMAND);
    }
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

  // Zero-width, after the blanks (and before what follows the form feed).
  if (form_feed_ends_paragraph(scanner, &start, valid_symbols)) {
    return accept(lexer, PARAGRAPH_END_BEFORE_COMMAND);
  }

  // In a preformatted block, a paragraph is preformatted text, which a
  // zero-width token starts: before text, here after the blanks, or a
  // command that starts a paragraph, before its `@`. Not after a newline
  // that may continue an open one, as the line commands in a paragraph may.
  bool preformatted = valid_symbols[PREFORMATTED_START] &&
                      !valid_symbols[PARAGRAPH_LINE_COMMAND_NAME] &&
                      in_preformatted(scanner);
  // A `{` in the middle of the line follows a command that starts none,
  // such as `@caption`, whose braces it may be: only one at the start of a
  // line starts it, which any edit that puts something before it touches.
  if (preformatted && lexer->lookahead != '@' && lexer->lookahead != '\n' &&
      lexer->lookahead != '\r' && lexer->lookahead != '}' &&
      !lexer->eof(lexer) && (lexer->lookahead != '{' || starts_line(&start))) {
    return accept(lexer, PREFORMATTED_START);
  }

  // A blank or comment line's end right after a list's head.
  if (valid_symbols[LEADING_NEWLINE] && lexer->lookahead == '\n') {
    return scan_leading_newline(scanner, lexer);
  }
  if (lexer->lookahead != '@') return false;
  // Zero-width tokens end here; every other token marks its own end.
  lexer->mark_end(lexer);
  advance(lexer);
  start.consumed++;

  // A symbol command, or an `@` alone at the end of a line or the file, but
  // `@*`, which starts no paragraph.
  int32_t after = lexer->lookahead;
  if (preformatted && after != '*' &&
      (is_symbol_char(after) || after == '\n' || after == '\r' ||
       lexer->eof(lexer))) {
    lexer->result_symbol = PREFORMATTED_START;
    return true;
  }

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

  Name name;
  if (!read_command(lexer, &name)) return false;
  start.consumed += name.length;

  if (same_name(name.text, "end")) {
    bool line_start = (brace_close || paragraph_end) && starts_line(&start);
    return scan_end(scanner, lexer, valid_symbols, brace_close && line_start,
                    paragraph_end && line_start);
  }
  // A node, a section or `@bye` closes the open blocks, all but a
  // conditional, which texi2any has evaluated, and which a section may be
  // inside; and the paragraph and brace command before them.
  bool root = is_root_command(&name);
  bool item = starts_item(scanner, &name);
  if (paragraph_end && (name.block >= 0 || item || root) &&
      starts_line(&start)) {
    lexer->result_symbol = PARAGRAPH_END_AT_LINE_START;
    return true;
  }
  // A block other than a conditional, or a raw block texi2any keeps inside,
  // closes an ordinary brace command at the start of its line too, and so
  // does a list's next item. A node or section closes any brace command.
  if (valid_symbols[BRACE_CLOSE_AT_PARAGRAPH_END] &&
      (closes_brace(&name) || item || root) && starts_line(&start)) {
    lexer->result_symbol = BRACE_CLOSE_AT_PARAGRAPH_END;
    return true;
  }
  if (valid_symbols[BRACE_CLOSE_AT_BLOCK_END] && root && starts_line(&start)) {
    lexer->result_symbol = BRACE_CLOSE_AT_BLOCK_END;
    return true;
  }
  if (root && valid_symbols[MISSING_END] && scanner->depth > 0 &&
      innermost_block(scanner)->kind != KIND_CONDITIONAL) {
    scanner->depth--;
    lexer->result_symbol = MISSING_END;
    return true;
  }

  // An item command where it means nothing, which texi2any reports and
  // drops: an extra, so that the rest of the line goes on as it would
  // without it. Only where a command may be, not in raw text such as
  // `@set`'s value.
  if (valid_symbols[MISPLACED_COMMAND] && (name.flags & ITEM) && !item &&
      !in_raw_text(valid_symbols)) {
    return accept(lexer, MISPLACED_COMMAND);
  }
  // And one before a list's first item, right after its head.
  if (valid_symbols[LEADING_ITEM_COMMAND] &&
      leading_item_command(scanner, &name)) {
    return accept(lexer, LEADING_ITEM_COMMAND);
  }

  if ((name.flags & ROOT) && same_name(name.text, "node")) {
    return scan_node_before_section(scanner, lexer, valid_symbols);
  }
  int level = section_level(&name, scanner->section_shift);
  if (level >= 2 && valid_symbols[SECTION_COMMAND + level - 2]) {
    return accept(lexer, SECTION_COMMAND + level - 2);
  }

  int token = command_token(scanner, valid_symbols, &name);
  if (preformatted && starts_paragraph(&name, token)) {
    lexer->result_symbol = PREFORMATTED_START;
    return true;
  }
  if (token == PARAGRAPH_END_BEFORE_COMMAND || token == NO_PARAGRAPH_START) {
    // Zero-width, before the `@`: the command is read again after it.
    lexer->result_symbol = (TSSymbol)token;
    return true;
  }
  if (token >= 0) {
    count_section_shift(&scanner->section_shift, &name);
    if (token == BRACE_COMMAND_NAME) {
      scanner->after_accent = name.flags & ACCENT;
    }
    return accept(lexer, (enum TokenType)token);
  }

  // read_name stops at the first character that can't be part of a name, so
  // `@html@c` opens a raw block, as in texi2any.
  return scan_block_start(scanner, lexer, valid_symbols, &name);
}

bool tree_sitter_texinfo_external_scanner_scan(void *payload, TSLexer *lexer,
                                               const bool *valid_symbols) {
  Scanner *scanner = payload;

  // During error recovery every symbol is valid; let the internal lexer
  // handle it rather than pushing or popping blocks speculatively.
  if (valid_symbols[ERROR_SENTINEL]) return false;

  // tree-sitter restores the state saved after the last external token
  // before each scan, so this is only set right after a brace's close.
  bool after_brace_close = scanner->after_brace_close;
  scanner->after_brace_close = false;
  if (!scan(scanner, lexer, valid_symbols, after_brace_close)) return false;
  scanner->after_brace_close =
      lexer->result_symbol == BRACE_CLOSE_AT_PARAGRAPH_END ||
      lexer->result_symbol == BRACE_CLOSE_AT_BLOCK_END ||
      lexer->result_symbol == BRACE_CLOSE_AT_LINE_END;
  return true;
}
