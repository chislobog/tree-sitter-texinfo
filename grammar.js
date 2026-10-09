/**
 * @file Texinfo grammar for tree-sitter
 * @author Janko Lisica <janko.lisica@gmail.com>
 * @license MIT
 */

/// <reference types="tree-sitter-cli/dsl" />
// @ts-check

// Sectioning commands, grouped by level as in texi2any.
const HEADINGS = {
  part_heading: ['part'],
  top_heading: ['top'],
  chapter_heading: ['chapter', 'unnumbered', 'appendix', 'centerchap'],
  section_heading: ['section', 'unnumberedsec', 'appendixsec', 'appendixsection'],
  subsection_heading: ['subsection', 'unnumberedsubsec', 'appendixsubsec'],
  subsubsection_heading: ['subsubsection', 'unnumberedsubsubsec', 'appendixsubsubsec'],
};

// Sections by level, as in texi2any: each contains the lower levels and ends
// at a sectioning command of its own or a higher level. src/scanner.c lists
// the sectioning commands by level too, in the same order, to tell which
// section a `@node` line starts.
const SECTIONS = [
  {name: 'part', heading: 'part_heading', contains: ['chapter', 'section', 'subsection', 'subsubsection']},
  {name: 'top', heading: 'top_heading', contains: ['chapter', 'section', 'subsection', 'subsubsection']},
  {name: 'chapter', heading: 'chapter_heading', contains: ['section', 'subsection', 'subsubsection']},
  {name: 'section', heading: 'section_heading', contains: ['subsection', 'subsubsection']},
  {name: 'subsection', heading: 'subsection_heading', contains: ['subsubsection']},
  {name: 'subsubsection', heading: 'subsubsection_heading', contains: []},
];

/**
 * A `@node` line, whose command is given as a rule.
 *
 * @param {GrammarSymbols<string>} $
 * @param {RuleOrLiteral} name
 */
const nodeLine = ($, name) => seq(
  field('name', name),
  optional(commaArguments(
    ['node', 'next', 'previous', 'up'],
    alias($._argument_line, $.argument),
    alias($._line, $.argument),
    ',',
  )),
  $._line_end,
);

// Headings that don't start a section, from chapter to subsubsection level.
const HEADING_COMMANDS = ['majorheading', 'chapheading', 'heading', 'subheading', 'subsubheading'];

// Commands whose argument is the rest of the line.
const LINE_COMMANDS = [
  // Structure and cross-referencing
  'printindex', 'include', 'insertcopying', 'contents', 'shortcontents',
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
  // Less common: two-letter index names, page layout, cross-reference names
  'cpindex', 'fnindex', 'kyindex', 'pgindex', 'tpindex', 'vrindex', 'bsixpaper',
  'clickstyle', 'fonttextsize', 'microtype', 'maketitle', 'verbatiminclude',
  'xrefname', 'evenfootingmarks', 'evenheadingmarks', 'everyfootingmarks',
  'everyheadingmarks', 'oddfootingmarks', 'oddheadingmarks',
];

// Built-in brace commands whose argument ends at a blank line, as in texi2any:
// all of them except @verb, the "context" commands @footnote, @caption,
// @shortcaption and @math, and the inline conditionals (@inlinefmt, ...),
// whose skipped arguments may contain blank lines. Any other command with
// braces, including calls to user-defined macros, may contain blank lines.
const BRACE_COMMANDS = [
  'AA', 'AE', 'DH', 'H', 'L', 'LaTeX', 'O', 'OE', 'TH', 'TeX', 'U', 'aa', 'ae', 'ampchar', 'anchor', 'arrow', 'asis', 'atchar', 'b',
  'backslashchar', 'bullet', 'cite', 'click', 'clicksequence', 'code', 'comma',
  'command', 'copyright', 'dfn', 'dh', 'dmn', 'dotaccent', 'dotless', 'dots',
  'emph', 'enddots', 'env', 'equiv', 'error', 'errormsg', 'euro',
  'exclamdown', 'expansion', 'file', 'geq', 'guillemetleft', 'guillemetright',
  'guillemotleft', 'guillemotright', 'guilsinglleft', 'guilsinglright',
  'hashchar', 'headitemfont', 'hyphenation', 'i', 'indicateurl',
  'kbd', 'key', 'l', 'lbracechar', 'leq', 'minus',
  'o', 'oe', 'ogonek', 'option', 'ordf', 'ordm', 'point', 'pounds',
  'print', 'questiondown', 'quotedblbase', 'quotedblleft',
  'quotedblright', 'quoteleft', 'quoteright', 'quotesinglbase', 'r',
  'rbracechar', 'registeredsymbol', 'result', 'ringaccent', 'samp',
  'sansserif', 'sc', 'seealso', 'seeentry', 'slanted', 'sortas', 'ss', 'strong',
  'sub', 'sup', 't', 'textdegree', 'th', 'tie', 'tieaccent', 'titlefont', 'today',
  'u', 'ubaraccent', 'udotaccent', 'v', 'value', 'var', 'w'];

// Brace commands whose argument splits at commas, with the field name of each
// argument by position. As in texi2any, the last argument takes any further
// commas. These also end at a blank line, like BRACE_COMMANDS.
const ARGUMENT_COMMANDS = {
  'node label title file manual': ['xref', 'pxref', 'ref'],
  'node label file': ['inforef', 'link'],
  'url text replacement': ['uref', 'url'],
  'address text': ['email'],
  'file width height alt extension': ['image'],
  'abbreviation meaning': ['abbr', 'acronym'],
  'anchor label': ['namedanchor'],
};

/**
 * Arguments separated by commas, each in the field named by its position;
 * empty arguments are left out. The last argument may contain commas.
 *
 * @param {string[]} names
 * @param {RuleOrLiteral} argument
 * @param {RuleOrLiteral} lastArgument
 * @param {RuleOrLiteral} separator
 */
const commaArguments = (names, argument, lastArgument, separator) => {
  const argumentAt = (i) => field(names[i], i === names.length - 1 ? lastArgument : argument);
  /** @type {RuleOrLiteral[]} */
  let rest = [];
  for (let i = names.length - 1; i >= 1; i--) {
    rest = [optional(seq(separator, optional(argumentAt(i)), ...rest))];
  }
  return seq(optional(argumentAt(0)), ...rest);
};

// The layouts of definition lines: the words before the arguments, as fields,
// and the line commands with that layout. The block commands for each layout
// (`@deffn`, ...) are in src/scanner.c, which numbers them in this order.
const DEFINITION_LAYOUTS = [
  {fields: ['category', 'entity'], lines: ['deffnx', 'defvrx', 'deftpx', 'defline']},
  {fields: ['category', 'type', 'entity'], lines: ['deftypefnx', 'deftypevrx', 'deftypeline']},
  {fields: ['category', 'class', 'entity'], lines: ['defopx', 'defcvx']},
  {fields: ['category', 'class', 'type', 'entity'], lines: ['deftypeopx', 'deftypecvx']},
  {fields: ['entity'], lines: ['defunx', 'defmacx', 'defspecx', 'defvarx', 'defoptx']},
  {fields: ['type', 'entity'], lines: ['deftypefunx', 'deftypevarx']},
  {fields: ['class', 'entity'], lines: ['defmethodx', 'defivarx']},
  {fields: ['class', 'type', 'entity'], lines: ['deftypemethodx', 'deftypeivarx']},
];

/**
 * The words of a definition line, each in its field, then its arguments.
 * Trailing words may be missing.
 *
 * @param {GrammarSymbols<string>} $
 * @param {string[]} fields
 */
const definitionFields = ($, fields) => {
  let rest = optional(field('arguments', $.definition_arguments));
  for (const name of [...fields].reverse()) {
    rest = optional(seq(field(name, $._definition_word), rest));
  }
  return rest;
};

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
    $._brace_close_at_paragraph_end,
    $._brace_close_at_block_end,
    $._missing_end,
    $._menu_start,
    ...DEFINITION_LAYOUTS.map((_, i) => $[`_definition_start_${i}`]),
    // `@node` followed by the sectioning command of this section.
    ...SECTIONS.map(({name}) => $[`_node_before_${name}`]),
    $._error_sentinel,
  ],

  rules: {
    source_file: $ => seq(
      optional($.input_line),
      repeat(choice($._content, $._section)),
      optional($.bye),
    ),

    _section: $ => choice(...SECTIONS.map(({name}) => $[name])),

    ...Object.fromEntries(SECTIONS.map(({name, heading, contains}) => [
      name,
      $ => prec.right(seq(
        // Blank and comment lines, the node's description and `@anchor` lines
        // (old names of the node) may separate the node from its heading. The
        // scanner only starts a section with the node if nothing else does.
        optional(seq(
          field('node', alias($[`_node_line_before_${name}`], $.node_line)),
          repeat(choice(
            $._newline,
            alias($._node_description, $.line_command),
            $.block,
            $.paragraph,
          )),
        )),
        field('heading', $[heading]),
        repeat(choice($._content, ...contains.map((section) => $[section]))),
      )),
    ])),

    // `@nodedescription` after a `@node`; `@nodedescriptionblock` is a block.
    _node_description: $ => seq(
      field('name', alias('@nodedescription', $.command_name)),
      optional(field('argument', $.line_argument)),
      $._line_end,
    ),

    ...Object.fromEntries(SECTIONS.map(({name}) => [
      `_node_line_before_${name}`,
      $ => nodeLine($, alias($[`_node_before_${name}`], $.command_name)),
    ])),

    // Listed before `command_name` so that a bare `@c` lexes as a comment.
    comment: _ => /@c(omment)?([ \t][^\r\n]*)?/,

    // `\input texinfo` on the first line, for TeX.
    input_line: $ => seq(token(prec(1, /\\input[ \t]+[^\s@]+/)), $._newline),

    // Everything after `@bye` is ignored.
    // The rest of the `@bye` line is still read, so a comment there counts.
    bye: $ => seq(
      field('name', alias('@bye', $.command_name)),
      $._line_end,
      optional(alias(/[\s\S]+/, $.ignored)),
    ),

    _content: $ => choice(
      $._newline,
      $.paragraph,
      $.node_line,
      alias($._definition_continuation, $.definition_line),
      $.line_command,
      $.heading,
      $.block,
      $.definition_block,
      $.menu,
      $.list,
      $.raw_block,
    ),

    ...Object.fromEntries(Object.entries(HEADINGS).map(([name, commands]) => [
      name,
      $ => seq(
        field('name', command($, commands)),
        optional(field('title', $.line_argument)),
        $._line_end,
      ),
    ])),

    heading: $ => seq(
      field('name', command($, HEADING_COMMANDS)),
      optional(field('title', $.line_argument)),
      $._line_end,
    ),

    line_command: $ => seq(
      field('name', choice(
        command($, LINE_COMMANDS),
        // Index commands, including user-defined ones (`@defindex op` makes
        // `@opindex`), which texi2any treats as index entries at the start
        // of a line. Takes precedence over an inline command there; in the
        // middle of a line only inline commands are valid.
        alias(token(prec(1, /@[a-zA-Z]+index/)), $.command_name),
      )),
      optional(field('argument', $.line_argument)),
      $._line_end,
    ),

    line_argument: $ => repeat1($._inline),

    // `@node NAME, NEXT, PREVIOUS, UP` not followed by a sectioning command;
    // otherwise it is the `node` of a section.
    node_line: $ => nodeLine($, alias('@node', $.command_name)),

    block: $ => seq(
      field('name', alias($._block_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      // Items, for conditionals that wrap some of a list's items.
      // Items, for conditionals that wrap some of a list's items, and
      // sections, for conditionals that wrap whole sections.
      repeat(choice($._content, $.item, $._section)),
      field('end', $._block_end),
    ),

    // `@menu`, `@detailmenu` and `@direntry`: menu entries, and other content
    // as menu comments.
    menu: $ => seq(
      field('name', alias($._menu_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      repeat(choice($._content, $.menu_entry)),
      field('end', $._block_end),
    ),

    // `* Node::  Description` or `* Label: Node.  Description`. As in texi2any,
    // a `.` only ends the node when whitespace follows it.
    menu_entry: $ => seq(
      $._menu_star,
      choice(
        seq(
          field('node', alias($._menu_name, $.menu_node)),
          '::',
          $._menu_entry_end,
        ),
        // An entry still being typed: no separator yet.
        seq(optional(field('label', alias($._menu_name, $.menu_label))), $._line_end),
        // Without a separator, the node runs to the end of the line.
        seq(
          field('label', alias($._menu_name, $.menu_label)),
          ':',
          choice(
            $._line_end,
            seq(
              field('node', $.menu_node),
              choice($._line_end, seq(choice('.', ','), $._menu_entry_end)),
            ),
          ),
        ),
      ),
    ),

    // The description ends with its own line end, like a paragraph, so the
    // next line decides whether it continues.
    _menu_entry_end: $ => choice($._line_end, field('description', $.menu_description)),

    // `*` and whitespace at the start of a line in a menu; it takes precedence
    // over text there, which would otherwise match the whole line.
    _menu_star: _ => token(prec(2, /\*[ \t]+/)),

    _menu_name: $ => repeat1(choice(
      alias($._menu_name_text, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    )),

    _menu_name_text: _ => /[^@{}:\s]([^@{}:\r\n]*[^@{}:\s])?/,

    menu_node: $ => repeat1(choice(
      alias($._menu_node_text, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    )),

    // Stops before `,` and before a `.` that is followed by whitespace; may
    // contain spaces, but doesn't start or end with one.
    _menu_node_text: _ => /([^@{}.,\s]|\.+[^@{}.,\s])(([^@{}.,\r\n]|\.+[^@{}.,\s])*([^@{}.,\s]|\.+[^@{}.,\s]))?/,

    // The rest of the line, and following lines up to a blank line or the
    // next entry.
    menu_description: $ => seq(
      $._line,
      repeat(prec(1, seq($._newline, $._line))),
      $._line_end,
    ),

    // `@deffn` and friends: the definition line, then the body.
    definition_block: $ => seq(
      alias($._definition_opener, $.definition_line),
      repeat($._content),
      field('end', $._block_end),
    ),

    _definition_opener: $ => choice(...DEFINITION_LAYOUTS.map(({fields}, i) => seq(
      field('name', alias($[`_definition_start_${i}`], $.command_name)),
      definitionFields($, fields),
      $._line_end,
    ))),

    // `@deffnx` and friends, and `@defline` in `@defblock`.
    _definition_continuation: $ => choice(...DEFINITION_LAYOUTS.map(({fields, lines}) => seq(
      field('name', command($, lines)),
      definitionFields($, fields),
      $._line_end,
    ))),

    // Blocks and line commands recognised at the start of a line inside
    // braces.
    _line_block: $ => choice($.block, $.definition_block, $.list, $.raw_block),

    _line_item: $ => choice(
      $._line_block,
      $.line_command,
      alias($._definition_continuation, $.definition_line),
    ),

    // A word of a definition line: braces group several words into one.
    _definition_word: $ => choice(
      alias($._word_text, $.text),
      $.brace_group,
      $.inline_command,
      $.symbol_command,
    ),

    _word_text: _ => /[^@{}\s]+/,

    definition_arguments: $ => prec.right(repeat1(choice($._inline, $.line_continuation))),

    // `@` at the end of a definition line continues it. Elsewhere `@` at the
    // end of a line is a symbol command (a space).
    line_continuation: _ => token(prec(1, /@\r?\n/)),

    list: $ => seq(
      field('name', alias($._list_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      repeat($._content),
      repeat($.item),
      field('end', $._block_end),
    ),

    // `@verbatim`, `@ignore`, `@macro`, `@tex`, `@html`, ...: the body is
    // plain text.
    raw_block: $ => seq(
      field('name', alias($._raw_block_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._newline,
      optional(field('body', $.raw_text)),
      field('end', $._block_end),
    ),

    // A block still open at the end of the file ends with a zero-width
    // `missing_end`, so it keeps its structure.
    _block_end: $ => choice($.block_end, alias($._missing_end, $.missing_end)),

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
        field('name', command($, BRACE_COMMANDS)),
        optional(seq(
          token.immediate('{'),
          optional(field('arguments', $.brace_arguments)),
          choice(
            '}',
            alias($._brace_close_at_paragraph_end, $.missing_brace),
            alias($._brace_close_at_block_end, $.missing_brace),
          ),
        )),
      )),
      prec.right(seq(
        field('name', $.command_name),
        optional(seq(
          token.immediate('{'),
          optional(field('arguments', alias($._long_arguments, $.brace_arguments))),
          choice('}', alias($._brace_close_at_block_end, $.missing_brace)),
        )),
      )),
      ...Object.entries(ARGUMENT_COMMANDS).map(([names, commands]) => prec.right(seq(
        field('name', command($, commands)),
        optional(seq(
          token.immediate('{'),
          commaArguments(names.split(' '), $.argument, alias($._last_argument, $.argument), ','),
          choice(
            '}',
            alias($._brace_close_at_paragraph_end, $.missing_brace),
            alias($._brace_close_at_block_end, $.missing_brace),
          ),
        )),
      ))),
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

    // Lines separated by single newlines: like texi2any, an ordinary brace
    // command ends at a blank line, so an unclosed brace only breaks its own
    // paragraph. Blocks and line commands are recognised at the start of a
    // line, but a line command not right after the `{`.
    brace_arguments: $ => choice(
      seq(
        choice($._brace_first_line, seq($._newline, $._brace_line)),
        repeat(seq($._newline, $._brace_line)),
        optional($._newline),
      ),
      $._newline,
    ),

    _brace_first_line: $ => prec.right(choice(
      seq(repeat1($._line_block), repeat($._inline)),
      repeat1($._inline),
    )),

    // A line command ends with its own newline, so line-level items can follow
    // each other directly.
    _brace_line: $ => prec.right(choice(
      seq(repeat1($._line_item), repeat($._inline)),
      repeat1($._inline),
    )),

    // One argument of an ARGUMENT_COMMANDS command: lines without commas,
    // separated by single newlines.
    // An argument that is only a line break is kept, empty, as in texi2any.
    argument: $ => choice(
      seq(
        optional($._newline),
        $._argument_line,
        repeat(seq($._newline, $._argument_line)),
        optional($._newline),
      ),
      $._newline,
    ),

    // The last argument, which may contain commas.
    _last_argument: $ => seq(
      optional($._newline),
      $._line,
      repeat(seq($._newline, $._line)),
      optional($._newline),
    ),

    _argument_line: $ => prec.right(repeat1(choice(
      alias($._argument_text, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    ))),

    // Text in an argument other than the last, where a comma separates.
    _argument_text: _ => /[^@{},\s]([^@{},\r\n]*[^@{},\s])?/,

    // For brace commands other than BRACE_COMMANDS, which may contain blank
    // lines.
    _long_arguments: $ => choice(
      // A block may also start right after the `{`.
      seq(repeat1($._line_block), optional($._long_lines)),
      $._long_lines,
    ),

    _long_lines: $ => repeat1(choice(
      $._inline,
      $._newline,
      seq($._newline, repeat1($._line_item)),
    )),

    brace_group: $ => seq('{', repeat(choice($._inline, $._newline)), '}'),

    // As in texi2any, names may contain `-` (e.g. user-defined macros).
    command_name: _ => /@[a-zA-Z][a-zA-Z0-9-]*/,

    // `@@`, `@{`, `@}`, accents like `@'`, spacing like `@*`, `@ ` and `@` at end of line.
    symbol_command: _ => /@[^a-zA-Z]/,

    text: _ => /[^@{}\s]([^@{}\r\n]*[^@{}\s])?/,

    _newline: _ => /\r?\n/,

    // A newline, or the end of a file that doesn't end in one.
    _line_end: $ => choice($._newline, $._eof),
  },
});
