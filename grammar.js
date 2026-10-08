/**
 * @file Texinfo grammar for tree-sitter
 * @author Janko Lisica <janko.lisica@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Sectioning commands, grouped by level. Headings are flat siblings for now:
// they often sit inside conditionals like `@ifnottex`, which makes nesting
// sections fragile.
const HEADINGS = {
  part_heading: ['part'],
  top_heading: ['top'],
  chapter_heading: ['chapter', 'unnumbered', 'appendix', 'majorheading', 'chapheading', 'centerchap'],
  section_heading: ['section', 'unnumberedsec', 'appendixsec', 'appendixsection', 'heading'],
  subsection_heading: ['subsection', 'unnumberedsubsec', 'appendixsubsec', 'subheading'],
  subsubsection_heading: ['subsubsection', 'unnumberedsubsubsec', 'appendixsubsubsec', 'subsubheading'],
};

// Commands whose argument is the rest of the line.
const LINE_COMMANDS = [
  // Structure and cross-referencing
  'node', 'printindex', 'include', 'insertcopying', 'contents', 'shortcontents',
  'summarycontents', 'listoffloats', 'nodedescription',
  // Indices
  'cindex', 'findex', 'kindex', 'pindex', 'tindex', 'vindex', 'defindex', 'defcodeindex',
  'synindex', 'syncodeindex',
  // Header and title page
  'setfilename', 'settitle', 'title', 'subtitle', 'author', 'shorttitlepage', 'dircategory',
  'documentencoding', 'documentlanguage', 'setchapternewpage', 'headings', 'evenheading',
  'oddheading', 'everyheading', 'evenfooting', 'oddfooting', 'everyfooting',
  // Variables and macros
  'set', 'clear', 'alias', 'definfoenclose', 'unmacro',
  // Layout
  'page', 'need', 'sp', 'vskip', 'center', 'exdent', 'noindent', 'indent', 'finalout',
  'smallbook', 'afourpaper', 'afivepaper', 'afourlatex', 'afourwide', 'pagesizes',
  'paragraphindent', 'firstparagraphindent', 'exampleindent', 'footnotestyle',
  'frenchspacing', 'allowcodebreaks', 'kbdinputstyle', 'codequoteundirected',
  'codequotebacktick', 'deftypefnnewline', 'urefbreakstyle', 'xrefautomaticsectiontitle',
  'setcontentsaftertitlepage', 'setshortcontentsaftertitlepage', 'novalidate',
  'raisesections', 'lowersections',
  // Continuation lines of definition blocks
  'deffnx', 'defvrx', 'deftypefnx', 'deftypevrx', 'deftpx', 'defopx', 'deftypeopx', 'defcvx',
  'deftypecvx', 'defunx', 'defmacx', 'defspecx', 'defvarx', 'defoptx', 'deftypefunx',
  'deftypevarx', 'defmethodx', 'deftypemethodx', 'defivarx', 'deftypeivarx',
];

/**
 * A command keyword exposed as a `command_name` node.
 *
 * @param {GrammarSymbols<string>} $
 * @param {string[]} names
 */
const command = ($, names) => {
  const keywords = names.map((name) => '@' + name);
  return alias(keywords.length === 1 ? keywords[0] : choice(...keywords), $.command_name);
};

export default grammar({
  name: 'texinfo',

  extras: $ => [/[ \t]/, $.comment],

  // Block openers are recognised by the scanner, which keeps a stack of open
  // blocks so that `@end NAME` must match. The block names live in
  // src/scanner.c. Openers are only valid at the start of a line because
  // every line-level rule ends in `_line_end`.
  externals: $ => [
    $._block_start,
    $._list_start,
    $._raw_block_start,
    $.raw_text,
    $.block_end,
    $._eof,
    $._verb_open,
    $._verb_text,
    $._verb_close,
    $._error_sentinel,
  ],

  rules: {
    source_file: $ => seq(
      optional($.input_line),
      repeat($._content),
      optional($.bye),
    ),

    // Listed before `command_name` so that a bare `@c` lexes as a comment.
    comment: _ => /@c(omment)?([ \t][^\r\n]*)?/,

    // `\input texinfo` on the first line, for TeX.
    input_line: $ => seq(token(prec(1, /\\input[ \t]+[^\s@]+/)), $._newline),

    // Everything after `@bye` is ignored.
    bye: $ => seq(alias('@bye', $.command_name), optional(alias(/[\s\S]+/, $.ignored))),

    _content: $ => choice(
      $._newline,
      $.paragraph,
      $.line_command,
      ...Object.keys(HEADINGS).map((name) => $[name]),
      $.block,
      $.list,
      $.raw_block,
    ),

    ...Object.fromEntries(Object.entries(HEADINGS).map(([name, commands]) => [
      name,
      $ => seq(
        field('command', command($, commands)),
        optional(field('title', $.line_argument)),
        $._line_end,
      ),
    ])),

    line_command: $ => seq(
      field('name', command($, LINE_COMMANDS)),
      optional(field('argument', $.line_argument)),
      $._line_end,
    ),

    line_argument: $ => repeat1($._inline),

    block: $ => seq(
      field('name', alias($._block_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      // Items, for conditionals that wrap some of a list's items.
      repeat(choice($._content, $.item)),
      field('end', $.block_end),
    ),

    list: $ => seq(
      field('name', alias($._list_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      repeat($._content),
      repeat($.item),
      field('end', $.block_end),
    ),

    // `@verbatim`, `@ignore`, `@macro`, `@tex`, `@html`, ...: the body is
    // plain text.
    raw_block: $ => seq(
      field('name', alias($._raw_block_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      optional(field('body', $.raw_text)),
      field('end', $.block_end),
    ),

    item: $ => prec.right(seq(
      field('name', command($, ['item', 'headitem'])),
      optional(field('argument', $.line_argument)),
      $._line_end,
      // texi2any allows index entries and comments before an `@itemx`.
      repeat(choice($.itemx, $._content)),
    )),

    itemx: $ => seq(
      field('name', command($, ['itemx'])),
      optional(field('argument', $.line_argument)),
      $._line_end,
    ),

    paragraph: $ => seq(
      $._line,
      // A newline followed by more inline content continues the paragraph.
      repeat(prec(1, seq($._newline, $._line))),
      $._line_end,
    ),

    _line: $ => prec.right(repeat1($._inline)),

    _inline: $ => choice(
      $.text,
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    ),

    inline_command: $ => choice(
      prec.right(seq(
        field('name', $.command_name),
        optional(seq(
          token.immediate('{'),
          optional(field('arguments', $.brace_arguments)),
          '}',
        )),
      )),
      // `@verb{xTEXTx}`: the scanner reads the delimiter `x` and the raw text.
      seq(
        field('name', alias('@verb', $.command_name)),
        token.immediate('{'),
        $._verb_open,
        optional(field('body', alias($._verb_text, $.raw_text))),
        $._verb_close,
        '}',
      ),
    ),

    // Like texi2any, blocks and line commands are recognised at the start of a
    // line inside any brace command, as `@footnote` and `@caption` need.
    brace_arguments: $ => repeat1(choice(
      $._inline,
      $._newline,
      // A line command ends with its own newline, so line-level items can
      // follow each other directly.
      seq($._newline, repeat1(choice($.block, $.list, $.raw_block, $.line_command))),
    )),

    brace_group: $ => seq('{', repeat(choice($._inline, $._newline)), '}'),

    command_name: _ => /@[a-zA-Z][a-zA-Z0-9]*/,

    // `@@`, `@{`, `@}`, accents like `@'`, spacing like `@*`, `@ ` and `@` at end of line.
    symbol_command: _ => /@[^a-zA-Z]/,

    text: _ => /[^@{}\s]([^@{}\r\n]*[^@{}\s])?/,

    _newline: _ => /\r?\n/,

    // A newline, or the end of a file that doesn't end in one.
    _line_end: $ => choice($._newline, $._eof),
  },
});
