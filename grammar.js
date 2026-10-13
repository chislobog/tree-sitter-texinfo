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

// Brace commands whose argument splits at commas, with the field name of each
// argument by position. As in texi2any, the last argument takes any further
// commas. These also end at a blank line, like the scanner's BRACE_COMMANDS.
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

/**
 * The end of an ordinary brace command's braces: its `}`, or a zero-width
 * `missing_brace` where texi2any closes it, at a blank line or the `@end` of
 * an enclosing block.
 *
 * @param {GrammarSymbols<string>} $
 */
const braceClose = ($) => choice(
  '}',
  alias($._brace_close_at_paragraph_end, $.missing_brace),
  alias($._brace_close_at_block_end, $.missing_brace),
);

/**
 * The end of braces that may hold blank lines: only the `@end` of an
 * enclosing block closes them if the `}` is missing.
 *
 * @param {GrammarSymbols<string>} $
 */
const longBraceClose = ($) => choice('}', alias($._brace_close_at_block_end, $.missing_brace));

/**
 * The content of a multitable cell, which starts right after its `@item` or
 * `@tab`. Its last paragraph may end in the middle of a line, before the next
 * `@tab`.
 *
 * @param {GrammarSymbols<string>} $
 */
const cell = ($) => [
  repeat($._content),
  optional(alias($._cell_paragraph, $.paragraph)),
];

export default grammar({
  name: 'texinfo',

  // Spaces as texi2any has them: space, tab, vertical tab and form feed.
  // A carriage return is a blank, also before the `\n` of a line end, as no
  // token starts with one: one that did would take a lone `\r` before it.
  // A `}` that closes nothing may be anywhere, as texi2any reports and
  // ignores it.
  extras: $ => [/[ \t\v\f\r]/, $.comment, $.unmatched_brace],

  // A line command, blank line or block after an item's or a definition's
  // first line comes before the next `@itemx` or `@deffnx`, if one follows,
  // and starts the body otherwise: only the lines after it tell.
  conflicts: $ => [
    [$._body_start, $._between_lines],
  ],

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
    $._end_of_file,
    $._verb_open,
    $._verb_text,
    $._verb_close,
    $._brace_close_at_paragraph_end,
    $._brace_close_at_block_end,
    $._brace_close_at_line_end,
    $._missing_end,
    $._menu_start,
    ...DEFINITION_LAYOUTS.map((_, i) => $[`_definition_start_${i}`]),
    // `@node` followed by the sectioning command of this section.
    ...SECTIONS.map(({name}) => $[`_node_before_${name}`]),
    // Command names the scanner recognises from its tables: recognising them
    // with keywords in the grammar would make the parser several times larger.
    $._line_command_name,
    $._brace_command_name,
    $._multitable_start,
    $._tab,
    $._row_name,
    $._subentry_name,
    $._accent_argument,
    // An accent's argument in a line command's line, which can't go on to
    // the next line.
    $._line_accent_argument,
    $._float_start,
    $.unmatched_end,
    $._inline_raw_text,
    $._context_command_name,
    $._container_item_name,
    $._macro_start,
    $._math_name,
    $._paragraph_end_at_line_start,
    // A paragraph's newline before comment lines, which don't end it.
    $._newline_before_comments,
    // A line command that doesn't end a paragraph, inside one.
    $._paragraph_line_command_name,
    $._word_glue,
    // A run of dots in a menu node, but for the last one before whitespace,
    // which ends the node.
    $._menu_node_dots,
    // Zero-width before a menu entry in a block, such as a conditional,
    // inside a menu.
    $._menu_entry_start,
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

    // Listed before `command_name` so that a bare `@c` lexes as a comment. As
    // in texi2any, whatever follows the name is the comment's (`@c|`).
    comment: _ => /@c(omment)?([^a-zA-Z0-9\-\r\n][^\r\n]*)?/,

    // `\input texinfo` on the first line, for TeX.
    input_line: $ => seq(token(prec(1, /\\input[ \t\v\f]+[^ \t\n\r\v\f@]+/)), $._newline),

    // Everything after `@bye` is ignored.
    // The rest of the `@bye` line is still read, so a comment there counts.
    bye: $ => seq(
      field('name', alias('@bye', $.command_name)),
      $._line_end,
      optional(alias(/[\s\S]+/, $.ignored)),
    ),

    _content: $ => choice(
      $._body_start,
      alias($._definition_continuation, $.definition_line),
    ),

    // Content but an `@deffnx` line, which a definition's or an item's body
    // doesn't start with: one right after the first line, or after another,
    // is part of the head.
    _body_start: $ => choice(
      $._newline,
      // An `@end` that ends no open block, which texi2any reports and ignores.
      seq($.unmatched_end, $._line_end),
      $.paragraph,
      $.node_line,
      $.line_command,
      $.heading,
      $.block,
      $.definition_block,
      $.menu,
      $.list,
      $.raw_block,
      $.macro_definition,
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

    line_command: $ => choice(
      seq(
        // The scanner recognises line commands by name (LINE_COMMANDS in
        // src/scanner.c), including user-defined index commands.
        field('name', alias($._line_command_name, $.command_name)),
        optional(field('argument', $.line_argument)),
        optional($.subentry),
        $._line_end,
      ),
      // `@set NAME VALUE` and `@clear NAME`. As in texi2any, the value is raw
      // text; commands in it are only parsed where `@value{NAME}` uses it.
      seq(
        field('name', command($, ['set', 'clear'])),
        optional(seq(
          field('variable', alias($._word_text, $.text)),
          optional(field('value', alias($._set_value, $.raw_text))),
        )),
        $._line_end,
      ),
      // Commands whose argument texi2any keeps as raw text: all of the line
      // for `@vskip`, up to a comment for `@clickstyle` and `@unmacro`.
      seq(
        field('name', command($, ['vskip'])),
        optional(field('argument', alias(/[^\r\n]+/, $.raw_text))),
        $._line_end,
      ),
      seq(
        field('name', command($, ['clickstyle', 'unmacro'])),
        optional(field('argument', alias($._set_value, $.raw_text))),
        $._line_end,
      ),
      // `@alias NEW = EXISTING`; the spaces are optional. Anything else on the
      // line, which texi2any reports, is left as an argument.
      seq(
        field('name', command($, ['alias'])),
        optional(seq(
          field('alias', alias($._alias_name, $.text)),
          optional(seq(
            token(prec(1, '=')),
            optional(field('target', alias($._alias_name, $.text))),
          )),
        )),
        optional(field('argument', $.line_argument)),
        $._line_end,
      ),
    ),

    // Line commands inside a paragraph, which they don't end, as in texi2any:
    // index entries, `@include`, `@settitle`, ... The scanner gives the others
    // their usual token, as they end it, and so do `@set`, `@clear`, `@alias`
    // and the commands with a raw argument here (4 in the manuals). Comment
    // lines after one, the paragraph's, start with its newline.
    _paragraph_line_commands: $ => prec.right(repeat1(choice(
      alias($._paragraph_line_command, $.line_command),
      prec.right(seq(
        alias($._paragraph_line_command_before_comments, $.line_command),
        repeat1($._newline),
      )),
    ))),

    _paragraph_line_command: $ => seq(
      field('name', alias($._paragraph_line_command_name, $.command_name)),
      optional(field('argument', $.line_argument)),
      optional($.subentry),
      $._line_end,
    ),

    _paragraph_line_command_before_comments: $ => seq(
      field('name', alias($._paragraph_line_command_name, $.command_name)),
      optional(field('argument', $.line_argument)),
      optional($.subentry),
      $._newline_before_comments,
    ),

    // Takes precedence over the longer text of an argument, which may only
    // follow the names.
    _alias_name: _ => token(prec(1, /[^@{} \t\n\r\v\f=]+/)),

    // `@subentry` in an index entry: the rest of the line, which may hold the
    // next level's subentry, as in texi2any.
    subentry: $ => seq(
      field('name', alias($._subentry_name, $.command_name)),
      optional(field('argument', $.line_argument)),
      optional($.subentry),
    ),

    // Like texi2any, a line command's argument ends at the end of its line,
    // closing any brace command left open in it.
    line_argument: $ => repeat1($._line_argument_inline),

    _line_argument_inline: $ => choice(
      $.text,
      alias($._line_argument_command, $.inline_command),
      $.symbol_command,
      alias($._line_brace_group, $.brace_group),
    ),

    // A brace group in a line argument, like a multitable's column prototype,
    // closes at the end of the line if left open.
    _line_brace_group: $ => seq(
      '{',
      repeat($._line_argument_inline),
      choice('}', alias($._brace_close_at_line_end, $.missing_brace)),
    ),

    _line_argument_command: $ => choice(
      prec.right(seq(
        field('name', alias($._brace_command_name, $.command_name)),
        optional(choice(
          seq('{', $._line_brace_arguments),
          field('argument', alias($._line_accent_argument, $.text)),
        )),
      )),
      prec.right(seq(
        field('name', $.command_name),
        optional(seq(token.immediate('{'), $._line_brace_arguments)),
      )),
      $._argument_command,
      $._verb_command,
    ),

    // `@node NAME, NEXT, PREVIOUS, UP` not followed by a sectioning command;
    // otherwise it is the `node` of a section.
    node_line: $ => nodeLine($, alias('@node', $.command_name)),

    block: $ => seq(
      choice(
        seq(
          field('name', alias($._block_start, $.command_name)),
          optional(field('argument', $.line_argument)),
        ),
        // `@float TYPE, LABEL`: the label is a cross-reference target. The
        // type is a word such as `Figure` or `Th@'eor@`eme`; allowing the
        // commands with named arguments in it would cost several hundred
        // parser states.
        seq(
          field('name', alias($._float_start, $.command_name)),
          optional(commaArguments(
            ['type', 'label'],
            alias(repeat1(choice(
              alias($._argument_text, $.text),
              alias($._float_type_command, $.inline_command),
              $.symbol_command,
              $.brace_group,
            )), $.argument),
            alias($.line_argument, $.argument),
            ',',
          )),
        ),
      ),
      $._line_end,
      // Items and multitable rows, for conditionals that wrap some of a
      // list's items, menu entries, for those that wrap a menu's, and
      // sections, for conditionals that wrap whole sections.
      repeat(choice(
        $._content,
        $.item,
        alias($._container_item, $.item),
        alias($._menu_entry_in_block, $.menu_entry),
        alias($._row, $.item),
        $._section,
      )),
      field('end', $._block_end),
    ),

    // `@menu`, `@detailmenu` and `@direntry`: menu entries, and other content
    // as menu comments.
    menu: $ => seq(
      field('name', alias($._menu_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._line_end,
      repeat(choice($._content, $.menu_entry)),
      field('end', $._block_end),
    ),

    // `* Node::  Description` or `* Label: Node.  Description`. As in texi2any,
    // a `.` only ends the node when whitespace follows it.
    menu_entry: $ => seq($._menu_star, $._menu_entry_rest),

    // In a block, such as a conditional, the scanner only allows an entry
    // where the innermost block other than a conditional is a menu.
    _menu_entry_in_block: $ => seq($._menu_entry_start, $._menu_star, $._menu_entry_rest),

    _menu_entry_rest: $ => choice(
      seq(
        field('node', alias($._menu_name, $.menu_node)),
        '::',
        $._menu_entry_end,
      ),
      // An entry still being typed: no separator yet.
      seq(optional(field('label', alias($._menu_name, $.menu_label))), $._line_end),
      // Without a separator, the node runs to the end of the line. The
      // label may be empty, as texi2any accepts with a warning.
      seq(
        optional(field('label', alias($._menu_name, $.menu_label))),
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

    // The description ends with its own line end, like a paragraph, so the
    // next line decides whether it continues.
    _menu_entry_end: $ => choice($._line_end, field('description', $.menu_description)),

    // `*` and whitespace at the start of a line in a menu; it takes precedence
    // over text there, which would otherwise match the whole line. Right
    // after the line end before it: after blanks, a `*` is text, as in
    // texi2any.
    _menu_star: _ => token.immediate(prec(2, /\*[ \t\v\f]+/)),

    _menu_name: $ => repeat1(choice(
      alias($._menu_name_text, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    )),

    _menu_name_text: _ => /[^@{}: \t\n\r\v\f]([^@{}:\n]*[^@{}: \t\n\r\v\f])?/,

    menu_node: $ => repeat1(choice(
      alias($._menu_node_text, $.text),
      alias($._menu_node_dots, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    )),

    // Stops before `,` and before a `.` that is followed by whitespace; may
    // contain spaces, but doesn't start or end with one.
    _menu_node_text: _ => /([^@{}., \t\n\r\v\f]|\.+[^@{}., \t\n\r\v\f])(([^@{}.,\n]|\.+[^@{}., \t\n\r\v\f])*([^@{}., \t\n\r\v\f]|\.+[^@{}., \t\n\r\v\f]))?/,

    // The rest of the line, and following lines up to a blank line or the
    // next entry, as in the Texinfo manual and texi2any, raw blocks such as
    // `@ignore` among them. Not the other blocks: a conditional holds the
    // next entries, which texi2any only sees after evaluating it.
    menu_description: $ => choice(
      seq(
        // As in texi2any, the description may start on the next line.
        choice(
          $._line,
          prec(1, seq($._newline, optional($._description_blocks), $._line)),
        ),
        repeat(prec(1, seq($._newline, optional($._description_blocks), $._line))),
        choice($._line_end, seq($._newline, $._description_blocks)),
      ),
      // Or only blocks, which a line after them would go on with.
      prec.right(1, seq($._newline, $._description_blocks)),
    ),

    _description_blocks: $ => prec.right(repeat1(seq($.raw_block, $._line_end))),

    // `@deffn` and friends: the definition line, then the body.
    // The body starts after the last `@deffnx` line, as an item's does.
    definition_block: $ => seq(
      alias($._definition_opener, $.definition_line),
      repeat(prec.dynamic(1, seq(
        repeat($._between_lines),
        alias($._definition_continuation, $.definition_line),
      ))),
      optional(field('body', alias($._definition_body, $.definition_body))),
      field('end', $._block_end),
    ),

    _definition_body: $ => seq($._body_start, repeat($._content)),

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
    _line_block: $ => choice(
      $.block,
      $.definition_block,
      $.list,
      $.raw_block,
      $.macro_definition,
    ),

    // Headings too, which texi2any allows in a footnote or caption.
    _line_item: $ => choice(
      $._line_block,
      $.line_command,
      $.heading,
      alias($._definition_continuation, $.definition_line),
    ),

    // A word of a definition line: braces group several words into one.
    // A definition line is a line command: braces left open close at its end.
    _definition_word: $ => choice(
      // A line ending in `@` may continue before any word. Anonymous, so a
      // query for the word's field (`entity: (_)`) only finds the word.
      seq(alias($.line_continuation, '@\n'), $._definition_word),
      $._definition_word_part,
      // Parts with no space between them make one word, as in texi2any
      // (`foo@var{x}`); the scanner glues them.
      alias($._glued_word, $.word),
    ),

    // Glue may only follow a complete part, so that right after a command's
    // name a `{` is its argument, not a glued brace group: text, a brace
    // group, a symbol, or a built-in brace command with its braces or accent
    // argument. Other commands, whose braces are optional too, only end a
    // word: a part glued after them starts the next one.
    _glued_word: $ => seq(
      repeat1(seq($._glueable_word_part, $._word_glue)),
      $._definition_word_part,
    ),

    _glueable_word_part: $ => choice(
      alias($._word_text, $.text),
      alias($._definition_brace_group, $.brace_group),
      alias($._glueable_command, $.inline_command),
      $.symbol_command,
    ),

    _glueable_command: $ => seq(
      field('name', alias($._brace_command_name, $.command_name)),
      choice(
        seq('{', $._line_brace_arguments),
        field('argument', alias($._line_accent_argument, $.text)),
      ),
    ),

    _definition_word_part: $ => choice(
      alias($._word_text, $.text),
      alias($._definition_brace_group, $.brace_group),
      alias($._line_argument_command, $.inline_command),
      $.symbol_command,
    ),

    _word_text: _ => /[^@{} \t\n\r\v\f]+/,

    // Raw text up to the end of the line or a comment, as in texi2any. A
    // comment lexes as the longer match at `@c note`, and `@cfoo` as the
    // longer value piece.
    _set_value: _ => repeat1(choice(/[^@\r\n]+/, /@[a-zA-Z0-9][a-zA-Z0-9-]*/, /@[^a-zA-Z0-9\r\n]/)),

    definition_arguments: $ => prec.right(repeat1(choice(
      $.text,
      alias($._line_argument_command, $.inline_command),
      $.symbol_command,
      alias($._definition_brace_group, $.brace_group),
      $.line_continuation,
    ))),

    // A brace group in a definition line, which continues over a line ending
    // in `@`, as the line itself does.
    _definition_brace_group: $ => seq(
      '{',
      repeat(choice($._line_argument_inline, $.line_continuation)),
      choice('}', alias($._brace_close_at_line_end, $.missing_brace)),
    ),

    // `@` at the end of a definition line continues it. Elsewhere `@` at the
    // end of a line is a symbol command (a space).
    line_continuation: _ => token(prec(1, /@\r?\n/)),

    list: $ => choice(
      seq(
        field('name', alias($._list_start, $.command_name)),
        optional(field('argument', $.line_argument)),
        $._line_end,
        repeat($._content),
        repeat(choice($.item, alias($._container_item, $.item))),
        field('end', $._block_end),
      ),
      // `@multitable`: each `@item` or `@headitem` is a row, holding its first
      // cell's content and a `tab` for each further cell, as in texi2any.
      seq(
        field('name', alias($._multitable_start, $.command_name)),
        optional(field('argument', choice(
          $.line_argument,
          alias($._column_fractions, $.line_argument),
        ))),
        $._line_end,
        repeat($._content),
        repeat(alias($._row, $.item)),
        field('end', $._block_end),
      ),
    ),

    // `@columnfractions .3 .7`: as in texi2any, a command in the multitable's
    // argument, whose own argument is the rest of the line.
    _column_fractions: $ => alias($._column_fractions_command, $.inline_command),

    _column_fractions_command: $ => seq(
      field('name', command($, ['columnfractions'])),
      optional(field('argument', $.line_argument)),
    ),

    _row: $ => prec.right(seq(
      // The scanner recognises `@item` and `@headitem` as a row where the
      // innermost open block, other than conditionals, is a multitable.
      field('name', alias($._row_name, $.command_name)),
      ...cell($),
      repeat($.tab),
    )),

    tab: $ => prec.right(seq(
      field('name', alias($._tab, $.command_name)),
      ...cell($),
    )),

    // A newline after the paragraph's text always belongs to the paragraph.
    _cell_paragraph: $ => prec.right(seq(
      $._line,
      repeat(prec(1, seq(
        choice($._newline, $._comment_lines),
        optional($._paragraph_line_commands),
        $._line,
      ))),
    )),

    // `@verbatim`, `@ignore`, `@macro`, `@tex`, `@html`, ...: the body is
    // plain text.
    raw_block: $ => seq(
      field('name', alias($._raw_block_start, $.command_name)),
      optional(field('argument', $.line_argument)),
      $._line_end,
      optional(field('body', $.raw_text)),
      field('end', $._block_end),
    ),

    // `@macro NAME {PARAM, ...}`, also `@rmacro` and `@linemacro`: like a raw
    // block, but the body is Texinfo with `\param\` placeholders, parsed
    // where the macro is used. texi2any keeps the line as raw text: the
    // parameters are what is between the first `{` and `}`, split at commas.
    // Only a command, such as a comment, may follow; anything else, which
    // texi2any reports, is left as raw text.
    macro_definition: $ => seq(
      field('name', alias($._macro_start, $.command_name)),
      optional(seq(
        field('macro', alias($._word_text, $.text)),
        optional(choice($._macro_parameters, $._macro_line_rest)),
      )),
      $._line_end,
      optional(field('body', $.raw_text)),
      field('end', $._block_end),
    ),

    // A rule of its own, which lets the parser share its states: inline, it
    // costs six times as many.
    _macro_parameters: $ => seq(
      '{',
      optional(field('parameter', alias($._macro_parameter, $.text))),
      repeat(seq(',', optional(field('parameter', alias($._macro_parameter, $.text))))),
      // Without the `}` while the line is being typed.
      optional(seq('}', optional($._macro_line_rest))),
    ),

    // A macro parameter, spaces inside included (texi2any reports them).
    _macro_parameter: _ => /[^,}\s]([^,}\r\n]*[^,}\s])?/,
    // What follows a macro's name or parameters other than a comment.
    _macro_line_rest: $ => field('argument', alias(/[^@{\s][^\r\n]*/, $.raw_text)),

    // A block still open at the end of the file ends with a zero-width
    // `missing_end`, so it keeps its structure.
    _block_end: $ => choice($.block_end, alias($._missing_end, $.missing_end)),

    // As in texi2any, the body starts after the last `@itemx` line; an
    // `@itemx` after the body's text, which texi2any reports, stays in it.
    // Where both would do, the `@itemx` is the head's.
    item: $ => prec.right(seq(
      field('name', command($, ['item', 'headitem'])),
      optional(field('argument', $.line_argument)),
      $._line_end,
      repeat(prec.dynamic(1, seq(repeat($._between_lines), $.itemx))),
      optional(field('body', alias($._item_body, $.item_body))),
    )),

    // What may come between an item's or a definition's first line and its
    // next `@itemx` or `@deffnx` line, besides comments: index entries, blank
    // lines, and blocks such as a conditional holding an index entry.
    _between_lines: $ => choice($.line_command, $._newline, $.block),

    _item_body: $ => prec.right(seq($._body_start, repeat(choice($.itemx, $._content)))),

    // `@item` in `@itemize` or `@enumerate`: as in texi2any, the text after it
    // starts the item's first paragraph, which may continue on the next lines.
    _container_item: $ => prec.right(seq(
      field('name', alias($._container_item_name, $.command_name)),
      optional(field('body', alias($._container_item_body, $.item_body))),
    )),

    _container_item_body: $ => prec.right(repeat1($._content)),

    itemx: $ => seq(
      field('name', command($, ['itemx'])),
      optional(field('argument', $.line_argument)),
      $._line_end,
    ),

    paragraph: $ => seq(
      $._line,
      // A newline followed by more inline content continues the paragraph,
      // also past comment lines and the line commands that don't end it, as
      // in texi2any.
      repeat(prec(1, seq(
        choice($._newline, $._comment_lines),
        optional($._paragraph_line_commands),
        $._line,
      ))),
      // Or zero-width at the start of a line that ends it (blank, an `@end`
      // or a block), when a line command inside a brace command took the
      // newline before it. Comment lines and line commands before a line that
      // ends it, but not a blank one, stay in it.
      choice(
        $._line_end,
        $._paragraph_end_at_line_start,
        $._comment_lines,
        seq(choice($._newline, $._comment_lines), $._paragraph_line_commands),
      ),
    ),

    // The newline before comment lines that a line other than a blank one
    // follows, then the comment lines' newlines. texi2any also keeps them in a
    // menu entry's description, which here would cost 400 parser states, and
    // in a paragraph inside braces, where the scanner doesn't look past a
    // newline that may close the braces.
    _comment_lines: $ => prec.right(seq($._newline_before_comments, repeat1($._newline))),

    // A line of a paragraph or of a last argument.
    _line: $ => prec.right(repeat1($._inline)),

    _inline: $ => choice(
      $.text,
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    ),

    inline_command: $ => choice(
      // `@math{...}`: as in texi2any, nothing in it ends at a blank line,
      // neither a brace group nor a brace command.
      prec.right(seq(
        field('name', alias($._math_name, $.command_name)),
        optional(seq(
          '{',
          optional(field('arguments', alias($._math_content, $.brace_arguments))),
          longBraceClose($),
        )),
      )),
      prec.right(seq(
        // Built-in brace commands, recognised by the scanner (BRACE_COMMANDS
        // in src/scanner.c): their argument ends at a blank line.
        field('name', alias($._brace_command_name, $.command_name)),
        optional(choice(
          seq(
            '{',
            optional(field('arguments', $.brace_arguments)),
            braceClose($),
          ),
          // An accent command's argument without braces: `@'e`, `@u o`.
          field('argument', alias($._accent_argument, $.text)),
        )),
      )),
      // Any other command: a macro call, or a command texi2any doesn't know.
      // The grammar can't tell them apart: which names are macros depends on
      // the definitions before, in this file or another. Like a built-in
      // brace command, it ends at a blank line, so a mistyped name with an
      // open brace only breaks its own paragraph; so does a macro call whose
      // arguments hold a blank line, which texi2any allows. A macro with one
      // parameter called without braces, or a `@linemacro` call, is a
      // command followed by text, as its arguments depend on the definition.
      prec.right(seq(
        field('name', $.command_name),
        optional(seq(
          token.immediate('{'),
          optional(field('arguments', $.brace_arguments)),
          braceClose($),
        )),
      )),
      $._argument_command,
      $._verb_command,
    ),

    _math_content: $ => repeat1(choice(
      $.text,
      $.symbol_command,
      $._newline,
      alias($._math_group, $.brace_group),
      alias($._math_command, $.inline_command),
    )),

    _math_group: $ => seq(
      '{',
      optional($._math_content),
      longBraceClose($),
    ),

    _math_command: $ => choice(
      prec.right(seq(
        field('name', alias($._brace_command_name, $.command_name)),
        optional(choice(
          seq('{', $._math_brace_arguments),
          field('argument', alias($._accent_argument, $.text)),
        )),
      )),
      prec.right(seq(
        field('name', $.command_name),
        optional(seq(token.immediate('{'), $._math_brace_arguments)),
      )),
    ),

    _math_brace_arguments: $ => seq(
      optional(field('arguments', alias($._math_content, $.brace_arguments))),
      longBraceClose($),
    ),

    // A built-in brace command or accent in a float's type, like one in a
    // line argument: a rule of its own, as sharing that one costs 34 parser
    // states.
    _float_type_command: $ => prec.right(seq(
      field('name', alias($._brace_command_name, $.command_name)),
      optional(choice(
        seq('{', $._line_brace_arguments),
        field('argument', alias($._line_accent_argument, $.text)),
      )),
    )),

    // Paragraphs, blocks and line commands: what texi2any allows there.
    _context_body: $ => choice(
      seq(
        repeat1(choice($._newline, $.paragraph, $._line_item)),
        optional(alias($._cell_paragraph, $.paragraph)),
      ),
      alias($._cell_paragraph, $.paragraph),
    ),

    // An inline conditional's output format or flag name.
    _condition: $ => alias(repeat1(alias($._argument_text, $.text)), $.argument),

    _line_brace_arguments: $ => seq(
      optional(field('arguments', alias(
        repeat1($._line_argument_inline),
        $.brace_arguments,
      ))),
      choice('}', alias($._brace_close_at_line_end, $.missing_brace)),
    ),

    // Commands whose arguments split at commas into named fields.
    _argument_command: $ => choice(
      // `@footnote`, `@caption`, `@shortcaption`: like texi2any, the text is
      // paragraphs and blocks, and its last paragraph may end at the `}`.
      prec.right(seq(
        field('name', alias($._context_command_name, $.command_name)),
        optional(seq(
          '{',
          optional(field('arguments', alias($._context_body, $.brace_arguments))),
          longBraceClose($),
        )),
      )),
      // `@inlinefmt{FORMAT, TEXT}`, `@inlineifset{FLAG, TEXT}`: like a
      // conditional block, the text may hold blank lines and blocks.
      prec.right(seq(
        field('name', command($, ['inlinefmt', 'inlineifset', 'inlineifclear'])),
        optional(seq(
          '{',
          optional(field('condition', $._condition)),
          optional(seq(',', optional(field('text', alias($._long_arguments, $.argument))))),
          longBraceClose($),
        )),
      )),
      // `@inlinefmtifelse{FORMAT, THEN, ELSE}`: both texts may hold blank
      // lines and blocks; the first ends at a comma, the last takes the rest.
      prec.right(seq(
        field('name', command($, ['inlinefmtifelse'])),
        optional(seq(
          '{',
          optional(field('condition', $._condition)),
          optional(seq(
            ',',
            optional(field('then', alias($._long_comma_arguments, $.argument))),
            optional(seq(',', optional(field('else', alias($._long_arguments, $.argument))))),
          )),
          longBraceClose($),
        )),
      )),
      // `@inlineraw{FORMAT, TEXT}`: the text is raw, up to the matching brace.
      prec.right(seq(
        field('name', command($, ['inlineraw'])),
        optional(seq(
          '{',
          optional(field('condition', $._condition)),
          optional(seq(',', optional(field('body', alias($._inline_raw_text, $.raw_text))))),
          braceClose($),
        )),
      )),
      ...Object.entries(ARGUMENT_COMMANDS).map(([names, commands]) => prec.right(seq(
        field('name', command($, commands)),
        optional(seq(
          '{',
          commaArguments(names.split(' '), $.argument, alias($._last_argument, $.argument), ','),
          braceClose($),
        )),
      ))),
    ),

    // `@verb{xTEXTx}`: the scanner reads the delimiter `x` and the raw text.
    _verb_command: $ => prec.right(seq(
      field('name', alias('@verb', $.command_name)),
      optional(seq(
        token.immediate('{'),
        $._verb_open,
        optional(field('body', alias($._verb_text, $.raw_text))),
        $._verb_close,
        '}',
      )),
    )),

    // Lines separated by single newlines: like texi2any, an ordinary brace
    // command ends at a blank line, so an unclosed brace only breaks its own
    // paragraph. Blocks and line commands are recognised at the start of a
    // line, but a line command not right after the `{`.
    // Elsewhere the scanner closes the command before a blank line, but in a
    // preformatted block it may continue past one, as in texi2any.
    brace_arguments: $ => $._brace_content,

    _brace_content: $ => choice(
      seq(
        choice($._brace_first_line, seq($._newlines, $._brace_line)),
        repeat(seq($._newlines, $._brace_line)),
        optional($._newlines),
      ),
      $._newlines,
    ),

    _newlines: $ => repeat1($._newline),

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
    // A block may stand on lines of its own, as in other braces: a raw block
    // such as `@ignore`, which texi2any keeps in the argument, or a
    // conditional, which it evaluates first.
    // Lines may be separated by comment lines, which leave two newlines; the
    // scanner closes the command before a blank line.
    argument: $ => choice(
      seq(
        choice($._argument_line, seq($._newlines, $._argument_line_item)),
        repeat(seq($._newlines, $._argument_line_item)),
        optional($._newlines),
      ),
      $._newlines,
    ),

    _argument_line_item: $ => choice($._argument_line, $._line_block),

    // The last argument, which may contain commas.
    _last_argument: $ => seq(
      choice($._line, seq($._newlines, $._last_argument_line)),
      repeat(seq($._newlines, $._last_argument_line)),
      optional($._newlines),
    ),

    _last_argument_line: $ => choice($._line, $._line_block),

    _argument_line: $ => prec.right(repeat1(choice(
      alias($._argument_text, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
    ))),

    // Text in an argument other than the last, where a comma separates.
    _argument_text: _ => /[^@{}, \t\n\r\v\f]([^@{},\n]*[^@{}, \t\n\r\v\f])?/,

    // For brace commands other than the scanner's BRACE_COMMANDS, which may contain blank
    // lines.
    _long_arguments: $ => choice(
      // A block may also start right after the `{`.
      seq(repeat1($._line_block), optional($._long_lines)),
      $._long_lines,
    ),

    // Like `_long_arguments`, but ending at a comma.
    _long_comma_arguments: $ => choice(
      seq(repeat1($._line_block), optional($._long_comma_lines)),
      $._long_comma_lines,
    ),

    _long_comma_lines: $ => repeat1(choice(
      alias($._argument_text, $.text),
      $.inline_command,
      $.symbol_command,
      $.brace_group,
      $._newline,
      seq($._newline, repeat1($._line_item)),
    )),

    _long_lines: $ => repeat1(choice(
      $._inline,
      $._newline,
      seq($._newline, repeat1($._line_item)),
    )),

    // Like a built-in brace command, an unclosed group ends at a blank line or
    // the end of its block, so it only breaks its own paragraph.
    // Its lines may hold blocks, as a brace command's do: a conditional,
    // which closes no brace, stays inside it.
    brace_group: $ => seq(
      '{',
      optional($._brace_content),
      braceClose($),
    ),

    // A `}` where none closes a brace: after a brace command closed at a
    // blank line or at its block's end (as one crossing a conditional's end
    // is), or a mistake. Where a `}` closes one, that one wins.
    unmatched_brace: _ => token(prec(-1, '}')),

    // As in texi2any, a name starts with a letter or digit and may contain `-`
    // (user-defined index commands such as `@foo-barindex`), but not `_`:
    // `@code_x` is `@code` followed by `_x`, and `@c_x` a comment.
    command_name: _ => /@[a-zA-Z0-9][a-zA-Z0-9-]*/,

    // `@@`, `@{`, `@}`, accents like `@'`, spacing like `@*`, `@ ` and `@` at end of line.
    // `@` at the end of a line, like at the end of the file, is a lone `@`:
    // the newline stays outside it, so the next line still starts a line (an
    // `@end` there ends its block). Elsewhere a following command name makes
    // the longer match.
    symbol_command: _ => /@[^a-zA-Z0-9\r\n]?/,

    // Only ASCII spaces count, as in texi2any: a text may start with a
    // no-break space. A carriage return not before a newline is part of it.
    text: _ => /[^@{} \t\n\r\v\f]([^@{}\n]*[^@{} \t\n\r\v\f])?/,

    // The `\r` of a `\r\n` is a blank before it (`extras`).
    _newline: _ => /\n/,

    // A newline, or the end of a file that doesn't end in one.
    _line_end: $ => choice($._newline, $._end_of_file),
  },
});
