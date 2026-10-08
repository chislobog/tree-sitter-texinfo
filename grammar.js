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
  'frenchspacing', 'allowcodebreaks', 'kbdinputstyle', 'codequoteundirective',
  'codequotebacktick', 'deftypefnnewline', 'urefbreakstyle', 'xrefautomaticsectiontitle',
  'setcontentsaftertitlepage', 'setshortcontentsaftertitlepage', 'novalidate',
  'raisesections', 'lowersections',
  // Continuation lines of definition blocks
  'deffnx', 'defvrx', 'deftypefnx', 'deftypevrx', 'deftpx', 'defopx', 'deftypeopx', 'defcvx',
  'deftypecvx', 'defunx', 'defmacx', 'defspecx', 'defvarx', 'defoptx', 'deftypefunx',
  'deftypevarx', 'defmethodx', 'deftypemethodx', 'defivarx', 'deftypeivarx',
];

// Blocks whose body is ordinary Texinfo. Raw blocks (`@verbatim`, `@ignore`,
// `@tex`, `@macro`, ...) need an external scanner and are not handled yet.
const BLOCKS = [
  // Display
  'example', 'smallexample', 'lisp', 'smalllisp', 'display', 'smalldisplay', 'format',
  'smallformat', 'quotation', 'smallquotation', 'indentedblock', 'smallindentedblock',
  'cartouche', 'flushleft', 'flushright', 'raggedright', 'group', 'float',
  // Document parts
  'titlepage', 'copying', 'documentdescription', 'direntry', 'menu', 'detailmenu',
  'nodedescriptionblock',
  // Conditionals
  'ifinfo', 'ifnotinfo', 'ifhtml', 'ifnothtml', 'iftex', 'ifnottex', 'ifplaintext',
  'ifnotplaintext', 'ifdocbook', 'ifnotdocbook', 'ifxml', 'ifnotxml', 'iflatex',
  'ifnotlatex', 'ifset', 'ifclear', 'ifcommanddefined', 'ifcommandnotdefined',
  // Definitions
  'deffn', 'defvr', 'deftypefn', 'deftypevr', 'deftp', 'defop', 'deftypeop', 'defcv',
  'deftypecv', 'defun', 'defmac', 'defspec', 'defvar', 'defopt', 'deftypefun',
  'deftypevar', 'defmethod', 'deftypemethod', 'defivar', 'deftypeivar', 'defblock',
];

// Blocks whose body is a sequence of `@item`s.
const LISTS = ['itemize', 'enumerate', 'table', 'ftable', 'vtable', 'multitable'];

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
    ),

    ...Object.fromEntries(Object.entries(HEADINGS).map(([name, commands]) => [
      name,
      $ => seq(
        field('command', command($, commands)),
        optional(field('title', $.line_argument)),
        $._newline,
      ),
    ])),

    line_command: $ => seq(
      field('name', command($, LINE_COMMANDS)),
      optional(field('argument', $.line_argument)),
      $._newline,
    ),

    line_argument: $ => repeat1($._inline),

    // The `@end` name isn't checked against the opening command yet; that
    // needs the external scanner.
    block: $ => seq(
      field('name', command($, BLOCKS)),
      optional(field('argument', $.line_argument)),
      $._newline,
      repeat($._content),
      field('end', $.block_end),
    ),

    list: $ => seq(
      field('name', command($, LISTS)),
      optional(field('argument', $.line_argument)),
      $._newline,
      repeat($._content),
      repeat($.item),
      field('end', $.block_end),
    ),

    // Includes the trailing newline, which is optional at end of file.
    block_end: _ => token(seq('@end', /[ \t]+/, /[a-zA-Z]+/, /[ \t]*(\r?\n)?/)),

    item: $ => prec.right(seq(
      field('name', command($, ['item', 'headitem'])),
      optional(field('argument', $.line_argument)),
      $._newline,
      repeat($.itemx),
      repeat($._content),
    )),

    itemx: $ => seq(
      field('name', command($, ['itemx'])),
      optional(field('argument', $.line_argument)),
      $._newline,
    ),

    // The final newline is optional so that a paragraph can end the file.
    paragraph: $ => prec.right(seq(
      $._line,
      repeat(seq($._newline, $._line)),
      optional($._newline),
    )),

    _line: $ => prec.right(repeat1($._inline)),

    _inline: $ => choice(
      $.text,
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    ),

    inline_command: $ => prec.right(seq(
      field('name', $.command_name),
      optional(seq(
        token.immediate('{'),
        optional(field('arguments', $.brace_arguments)),
        '}',
      )),
    )),

    brace_arguments: $ => repeat1(choice($._inline, $._newline)),

    brace_group: $ => seq('{', repeat(choice($._inline, $._newline)), '}'),

    command_name: _ => /@[a-zA-Z][a-zA-Z0-9]*/,

    // `@@`, `@{`, `@}`, accents like `@'`, spacing like `@*`, `@ ` and `@` at end of line.
    symbol_command: _ => /@[^a-zA-Z]/,

    text: _ => /[^@{}\s]([^@{}\r\n]*[^@{}\s])?/,

    _newline: _ => /\r?\n/,
  },
});
