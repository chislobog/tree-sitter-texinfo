# TODO

Coverage of the [Texinfo manual](https://www.gnu.org/software/texinfo/manual/texinfo/texinfo.html)
(version 7.3). Command classes follow `tta/data/command_data.txt` in the Texinfo
sources, which is what `texi2any` itself uses.

## Done

- [x] `\input texinfo` first line
- [x] Comments: `@c`, `@comment`, including at end of line, and followed by
  any character that can't continue a name (`@c|`, `@comment@x`), which
  texi2any also reads as a comment (found by comparing arguments)
- [x] Line commands with the rest of the line as argument
- [x] Sectioning and heading commands, all levels
- [x] Paragraphs separated by blank lines
- [x] Brace commands, nested, with arguments spanning lines
- [x] Symbol commands: `@@`, `@{`, `@}`, `@*`, `@.`, `@ `, accents like `@'`
- [x] Bare brace groups
- [x] Blocks with optional line argument, nested
- [x] Lists: `@itemize`, `@enumerate`, `@table`, `@ftable`, `@vtable`, `@multitable`
  with `@item`, `@itemx`, `@headitem`
- [x] `@bye` and ignored trailing content

## External scanner

- [x] Match `@end NAME` against the open block
- [x] Raw blocks, body as plain text: `@verbatim`, `@ignore` (nested `@ignore`
  counted), `@macro`, `@rmacro`, `@linemacro`
- [x] Format-raw blocks, body as text for injections: `@tex`, `@latex`, `@html`,
  `@xml`, `@docbook`. texi2any still reads commands inside them; the tree
  keeps the body raw, for the injected language
- [x] Parse commands inside `@displaymath`, as texi2any does (it is a normal
  block now, not raw)
- [x] `@verb{xTEXTx}` with an arbitrary delimiter character
- [x] A stray `@end` that ends no open block is an `unmatched_end` node, to
  the end of its line, which editors should report
- [x] An unclosed brace command closes before a node, a section or `@bye`
  on the next line at the end of the line before it, which the scan reads
  to the command: decided at the command by its column, an edit before it
  on its line, then undone, left the section inside an unclosed `@math{`,
  as tree-sitter reused its subtree (found by `tree-sitter fuzz`, which the
  checks ran without failing on what it reports)
- [x] Keep the preformatted context through blocks nested in `@example`
  and the other preformatted blocks (`@group`, `@quotation`, even `@menu`),
  where a blank line doesn't close a brace command, as in texi2any
- [x] Command names as texi2any reads them, `[[:alnum:]][[:alnum:]-]*` of any
  length, so `@0` or a name longer than 32 characters is one (unknown)
  command, and `_` ends one: `@code_x` is `@code`, then `_x` (it continued
  one until the comparison of arguments showed `@c_x` is a comment)
- [x] No table of the file's macros in the scanner: it made every token
  after a definition carry the table in its state, so adding or renaming a
  macro reparsed the rest of the file (76 ms for gawk's manual, against
  0.3 ms for a word typed), and parsing gawk's manual with 128 macros took
  61 MB, not 43 MB. Macro calls are now commands like any other
  (Deliberate, below); the parser lost 787 states

## Commands and arguments

- [x] Blocks and line commands at the start of a line inside brace commands
  such as `@footnote` and `@caption` (found by the texi2any comparison), and
  headings such as `@heading` (found by comparing arguments; +12 parser
  states)
- [x] Paragraphs inside `@footnote`, `@caption` and `@shortcaption`, with
  blocks and line commands
- [x] In `@itemize` and `@enumerate`, the text after `@item` starts the item's
  first paragraph (in tables it is the item's line argument); `@noindent`
  and `@indent` start a paragraph too, as in texi2any
- [x] An unclosed `{` group ends at a blank line or its block's end, like a
  brace command, instead of breaking the rest of the file
- [x] Split brace arguments on commas into fields named by position for
  `@xref`, `@pxref`, `@ref`, `@inforef`, `@link`, `@uref`, `@url`, `@email`,
  `@image`, `@abbr`, `@acronym`, `@namedanchor`
- [x] `@node` arguments in fields (`node_line`: node, next, previous, up)
- [x] Split the inline conditionals' arguments into `format` (`@inlinefmt`)
  or `variable` (`@inlineifset`, `@inlineifclear`) and `text` (multi-line,
  like a conditional block) or, for `@inlineraw`, `format` and a raw
  `body`, injected in the language its format names. The first was
  `condition` for all of them until review (100 parser states)
- [x] `@inlinefmtifelse`'s three arguments: `format`, `then` and `else`,
  both texts spanning blank lines and blocks (+1,074 parser states)
- [x] `@float` arguments: `type` and `label` fields; the label is a tag and
  highlighted like a node name. Commands with named arguments (`@xref`)
  aren't allowed in the type
- [x] A block at the start of a line closes an ordinary brace command, as in
  texi2any (conditionals don't)
- [x] So does a list's next item at the start of a line (`@item`, `@itemx`,
  `@headitem`, `@tab` in a list or multitable), which also ends the
  paragraph, as in texi2any; an `@item` line swallowed by an unclosed
  `@code{` in a table moved the rest of the item out of it. texi2any also
  closes the brace at one in the middle of a line, which the manual
  doesn't allow
- [x] Accent commands without braces: `@'e`, `@"u`, and letter accents with
  a space: `@u o`, `@v c`; the character is in the command's `arguments`,
  a `brace_arguments` as with braces (its `argument`, a `text`, until
  review), as texi2any has either as the accent's argument
- [x] Each field holds one kind of node since review: `argument` is always
  a `line_argument`, which holds the raw text of `@vskip`, `@clickstyle`,
  `@unmacro` and of what follows a macro's name or parameters, and the
  ignored text after `@bye` on its line, as texi2any's line argument does
  (they were the `argument` themselves); an accent's argument is above
- [x] `@value{name}`: an inline command whose name is highlighted as a
  variable, like `@set`'s
- [x] `@alias` has `alias` and `target` fields; `@macro`, `@rmacro` and
  `@linemacro` are `macro_definition` nodes, whose bodies the injection
  query highlights as Texinfo
- [x] Tags for macros, aliases and `@set` variables, and `@value` as a
  reference
- [x] `@math{...}` body for TeX injection
- [x] Plain-text line arguments (`@include`, `@setfilename`, ...): texi2any
  parses commands in them too, so they stay a `line_argument`; file names
  are highlighted as paths
- [x] `@set NAME VALUE` and `@clear NAME` split into `variable` and `value`
  fields. As in texi2any, the value is raw text up to a comment; its
  commands are parsed where `@value` uses it
- [x] Form feeds and vertical tabs as spaces, as in texi2any. Unlike
  texi2any, a form feed in the middle of a line doesn't end the paragraph
- [x] Spaces between a built-in brace command and its brace (`@code {a}`),
  which texi2any accepts. Not for other commands, which may be macros or
  commands without braces (`@tab {b}`); not across a newline, which
  texi2any accepts with a warning
- [x] Definition lines: `@deffn`, `@deftypefn`, ..., their `x` variants and
  `@defline`/`@deftypeline` split into `category`, `class`, `type`, `entity`
  and `arguments` fields (braces group words)
- [x] `@` at the end of a definition line's arguments is a `line_continuation`
  (still a `symbol_command` before the arguments start)
- [x] A line ending in `@` right after a definition word, with the next
  line starting with no blank, continues the word, as in texi2any
  (`foo@` then `bar` is `foobar`); it was a space between two words. Its
  `@` is a `line_continuation` inside the `word`, and one before a word is
  in a `continuation` field, outside the word's (both were an anonymous
  `@\n` until review; outside any field, it costs 3,572 parser states)
- [x] A definition word glued to a command (`foo@var{x}`,
  `@var{$converter}->method` in texi2any's API reference) is one `word` node,
  as in texi2any; the scanner emits a zero-width glue between its parts.
  Only built-in brace commands with their braces glue to what follows:
  after another command (`@xref{a}b`) the next part starts a new word
- [x] Index entries with `@subentry` (a nested `subentry` node, as in
  texi2any), `@seeentry`, `@seealso`, `@sortas`
- [x] `@columnfractions`: an inline command in the multitable's argument,
  with its own `argument`, as texi2any has it as a line command there
- [x] Multitable rows: each `@item`/`@headitem` holds its first cell and a
  `tab` node per further cell, including rows inside conditionals
- [x] `@headings`, `@everyheading` and friends with `@|` separators: a line
  argument with `@|` symbol commands, as texi2any has it
- [x] `@alias NEW = EXISTING`, `@definfoenclose`: plain line arguments, as in
  texi2any
- [x] Missing line commands from texi2any's table: `defline`, `deftypeline`,
  `xrefname`, `clickstyle`, the two-letter index commands, ...
- [x] User-defined index commands (`@defindex op` makes `@opindex`): any
  `@...index` at the start of a line is an index line command
- [x] Missing blocks: `@documentinfo`, `@publication`
- [x] Fix typo: `codequoteundirective` should be `codequoteundirected`
- [x] An accent without braces at the end of a line command's line
  (`@chapter Caf@'`) has no argument, as in texi2any: it took the first
  character of the next line, and the heading the rest of that line (found
  by comparing arguments)
- [x] No-brace commands: `@refill`, `@thischapter`, `@thisfile`, ... parse as
  inline commands without arguments, as in texi2any

## Structure

Deliberate divergences from texi2any, which tolerates these with warnings:
block commands only start a line (texi2any also accepts `text @html` and
`@tex in tex @end tex` on one line), and `@end` only ends a block at the start
of a line.

- [x] An unclosed brace in a line command's argument (`@chapter A @code{x`)
  closes at the end of that line, as in texi2any, also in the commands with
  named arguments (`@xref{` in a heading, which had gone on to the next
  line) and in `@node` lines, where it had taken the section's heading and
  paired every later node with the heading after it (+103 parser states)
- [x] A block right after `@footnote{` (`@footnote{@quotation`)

- [x] `@itemx` after index entries or comments that follow an `@item`
  (make, texinfo; found with `compare-texi2any.py --structure`)
- [x] `@item` inside a conditional block inside a list, e.g. `@ifclear X`
  wrapping an `@item` in a `@table` (sed, gawk)
- [x] Menu entries in `@menu`, `@detailmenu` and `@direntry`: `menu_entry` with
  `label`, `node` and `description` fields; other text is a menu comment.
  An entry still being typed (`* Name` with no separator) has only a label
- [x] A menu entry's description may start on the line after the entry, as
  in texi2any; a bare `*` there is description text, not an entry
- [x] A menu node ending in a run of dots before whitespace keeps all but
  the last, as in texi2any: `* Label: foo... desc` is node `foo..` and
  description `desc`, not `foo` and `.. desc`; before `,` or a command it
  keeps them all
- [x] Sections nest by level (`part`, `top`, `chapter`, `section`,
  `subsection`, `subsubsection`), each with its `node` line and `heading`.
  A section inside a conditional ends at its `@end`; the Top node in
  `@ifnottex` and alternative headings per output format therefore don't
  contain the content after the conditional
- [x] `@anchor {old}`, with blanks before the brace, between `@node` and its
  section, as texi2any takes it: the node line still starts the section
- [x] `@majorheading`, `@chapheading`, `@heading`, `@subheading` and
  `@subsubheading` are `heading` nodes, not sections
- [x] Every command node names its command field `name` (headings used
  `command`, `@bye` had none)
- [x] Line commands and headings at end of file without a newline
- [x] Blocks still open at the end of the file end with a zero-width
  `missing_end` node instead of turning into an error
- [x] Unclosed brace commands end where texi2any ends them (a blank line for
  built-in commands other than `@footnote`, `@caption`, `@shortcaption` and
  `@math`; the enclosing block's `@end`; the end of the file), marked by a
  zero-width `missing_brace` node, so they don't break the rest of the file
- [x] A `@node`, a sectioning command or `@bye` closes the blocks left open
  before it, but not a conditional, and an unclosed brace command and its
  paragraph, as in texi2any ("@node seen before @end menu"). A `@menu` or a
  list still being typed had taken the rest of the file, its chapters read
  as text, and an `@example` had held the chapters after it (found by
  review: the comparison doesn't see where a block ends without its `@end`)
- [x] An unclosed brace command also ends at the `@end` of an enclosing block
  (`@code{` in an `@example` inside a `@quotation`, then `@end quotation`),
  which closes the blocks inside it, as in texi2any; the rest of the file
  stayed inside the brace
- [x] A brace argument crossing a conditional's boundary (e.g. `@footnote{`
  inside `@ifnottex`, closed after its `@end`, or TeX braces in Calc's and
  Gnus's `@iftex` blocks) can't be represented: the brace closes at the
  conditional's end, and the later `}` is an `unmatched_brace` node, as is
  any `}` that closes nothing, which texi2any reports and ignores. It was a
  one-character parse error, the last in the manuals. Five manuals still
  hold such recovery nodes, all where texi2any doesn't parse: TeX braces in
  Calc's, Gnus's and gawk's `@iftex` blocks (gawk's `@center @i{` takes a
  second line, as TeX allows), and a macro call in `texinfo-tex-test.texi`
  whose braces hold a blank line
- [x] TeX lines before `\input texinfo` (e.g. gawk's Italian manual): text,
  as in texi2any, which only recognises `\input` on the first line
- [x] The `@macro` line has `macro` and `parameter` fields, as texi2any
  reads it, rather than an argument parsed for commands; anything else on it
  but a comment is raw text. Tags and highlighting use the fields

## Differences from texi2any

From comparing with texi2any on its own test inputs (104 of 1,755 differ, in
their commands or the commands' arguments, all listed with their reasons in
`test/texi2any-differences.txt`; the manuals match but for three binutils
fragments meant for `@include`).

Gaps, where we give a parse error and texi2any recovers:

- [x] A block command on the last line without a newline (`@iftex` at the end
  of the file) is a block ending in `missing_end`
- [x] An unclosed brace group in a line argument, such as a `@multitable`
  prototype (`@multitable { a`), closes at the end of the line
- [x] Badly braced definition lines (`@deffn {cat name`) close their braces
  at the end of the line; a lone `@` at the end of the file is a symbol
- [x] Comments after `@bye` and `@definfoenclose`: the tree was right; the
  comparison script skipped them
- [x] Gaps found by listing every difference: blank lines inside braces in
  `@math` and inside brace commands in preformatted blocks; raw blocks
  other than `@verbatim` inside brace commands and cross-reference
  arguments; lone carriage returns in text; an empty menu entry name;
  `@footnote`, `@caption`, `@inlineraw`, the inline conditionals and
  `@verb` without braces
- [x] A lone carriage return at the start or end of a text (`\rHello`,
  `Hello\r@code{x}`, `Hello\r` at the end of the file) is a blank, as
  texi2any takes it, and so is the `\r` of a `\r\n`: no token starts with
  one, or it would take a lone one before it (`\r@code`)
- [x] A blank at the end of a file that leaves a block open
  (`@quotation`, a line `x`, then a last line of only a space) gave a
  `MISSING block_end` instead of `missing_end`
- [x] An unclosed `@verb` takes the rest of the file, as in texi2any, and
  ends with a `missing_brace`: it was an error from the start of its
  paragraph to the end of the file, outside the sections before it (9 of
  texi2any's test inputs). A `@verb{` at the end of a line has no
  delimiter: as in texi2any, its raw text ends at the first `}`
- [x] Parse errors on input that is being typed or that texi2any accepts:
  `\input texinfo` without a newline (a new file) or with text after it (a
  TeX `%` comment), text after `@bye` on its line, which texi2any ignores
  with a warning, and an empty node name (`* ::`), which was the node `:`.
  `@` before a character other than ASCII is a lone `@`, as in texi2any:
  the character was part of the symbol command (found by review)

Gaps where the tree differs from texi2any's and the Texinfo manual, found by
comparing arguments:

- [x] A raw block (`@ignore`, `@html`, ...) after a menu entry's description
  line, or right after the entry, is part of the description, which goes on
  after it: the Texinfo manual only ends one at an empty line or the next
  entry, as texi2any does (in gdb's, gas's and Semantic's manuals; 31
  parser states)
- [x] A menu entry inside a conditional in a menu (`@ifset` around some of
  the entries) was text in a paragraph: 235 entries in 18 manuals (ld,
  gas, Emacs, gdb, ...). The comparison couldn't see it, as it compares
  texi2any's expansion, where the conditional is gone. The scanner now
  allows an entry in a block whose innermost enclosing block other than a
  conditional is a menu (45 parser states)
- [x] Other blocks (`@example`, lists, definitions) are part of a menu
  entry's description too; the scanner gives a conditional its usual token
  there, which ends the description, as a conditional holds the next
  entries, which texi2any only sees after evaluating it (6 of texi2any's
  test inputs; the block rule's content, now a rule of its own, saved 158
  parser states)
- [x] A menu entry may start after spaces; the Texinfo manual and texi2any
  only start one with `* ` at the beginning of a line
  (`leading_space_before_menu_star.texi`). The star is an immediate token
  now, which blanks before it rule out
- [x] A comment line ended a paragraph, which texi2any goes on with after it
  (760 paragraphs in 156 of the manuals and test inputs; seen while
  comparing, as paragraphs aren't compared). Comment lines followed by a
  line that isn't blank now stay in the paragraph, or a multitable cell's
  (89 parser states)
- [x] texi2any also keeps index entries and the other line commands that
  don't close a paragraph (`@include`, `@author`, `@set`, ...) inside one;
  here they ended it. Now 4,100 of them in the manuals are inside their
  paragraph, 731 paragraphs fewer (1,501 parser states, 813 of them for
  `@set`, `@clear`, `@alias` and the commands with a raw argument, which
  have rules of their own, for 4 in the manuals; the budget went to 14,200).
  Since review, those rules are one hidden rule that every context's line
  commands share, and a `@subentry` may follow `@alias`'s argument, as in
  texi2any and other line commands: 942 states fewer, budget 13,500
- [x] Comment lines followed by a line that isn't blank stay in a menu
  entry's description too (4 manuals; 413 parser states)
- [x] Comment lines inside a paragraph in braces (`@footnote`) too: the
  token before them is zero-width now, so the scan of a brace that the
  newline may close can return it as well. Comment lines before a line
  holding only a command (`@item`) had been taken for ones before a blank
  line (53 files)
- [x] Paragraphs start and end where texi2any's do, found by comparing
  them: a command that starts no paragraph in texi2any (`@anchor`,
  `@noindent`, `@titlefont`, ...: NO_PARAGRAPH_COMMANDS in the scanner) is
  outside one where one would start, also before the text on its line, and
  inside an open one (12,910 of the manuals' 16,623 differences in
  paragraphs, most of them an `@anchor` line before a definition or an
  item; 90 parser states); a raw block but `@verbatim` (`@tex`, `@html`,
  `@ignore`) doesn't end a paragraph (1,611 more, most in Maxima's
  manuals; 53 parser states), nor do comment lines after one (ld's and
  Maxima's manuals; 43 parser states)
- [x] `@caption`, `@shortcaption` and `@titlefont` (the brace commands in
  texi2any's close_paragraph_commands) end a paragraph, also in the middle
  of a line, with a zero-width token before them, and start none; `@*`
  starts none; and in a footnote or caption, neither does a command that
  starts none (`@anchor`), at the start of a line (54 parser states for the
  captions, 18 for the footnotes, as alternatives of `_line_item`: in the
  footnote's own rule, they cost 176)
- [x] `@image` and `@namedanchor` start no paragraph either (29 files, 5 of
  them manuals). As keywords, they get a zero-width token from the scanner
  before them where content starts, so that they are read one way only,
  and are followed by their line's end, the paragraph the rest of the line
  starts or another of them, the same everywhere: 181 parser states,
  against 1,100 for the keyword as an alternative where content starts,
  whose arguments then had states for each context's followers; the
  budget went to 14,300
- [ ] Nor does `@image` inside a footnote's or caption's braces, at the
  start of a line, in texi2any, where it may be followed by the `}`
  (2 of texi2any's test inputs)
- [x] Macro definitions don't end a paragraph either, as in texi2any (6
  files; 88 parser states, the definition's own in a paragraph)
- [x] Nor do conditionals, which texi2any evaluates, so that the text of
  the branch it takes goes on with the paragraph around it: a conditional
  whose first line is text is a block inside the paragraph, at the start of
  one of its lines; one that starts with items, a section or a block stays
  part of the structure around the paragraph (60 parser states). The
  comparison can't see it, as texi2any's expansion has no conditionals
- [x] Text after a raw block's `@end` on that line, in a paragraph, is one
  text in the paragraph (texi2any reports it); it was an error that could
  reach far, found by the editing simulation (51 parser states). After a
  conditional's or a macro definition's, it stays a local error, which
  would cost another 100
- [x] Comment lines at the end of a paragraph, before a blank line or the
  end of the file, are in it, as in texi2any; they were after it (61
  files). In a paragraph, comment lines are no longer extras between
  counted newlines, which would have taken the blank line's newline too,
  but explicit, each with its line end (63 parser states)
- [x] The comparison compares paragraphs too: where they start and end
  among the commands, and their text outside commands. 148,650 of 148,912
  paragraphs match texi2any's; the other differences are listed, by the
  gaps above or as deliberate (a form feed is a blank here, an empty line
  to texi2any; a `#line` directive is text). Where texi2any's paragraphs
  differ for want of what another file defines (a line of commands it
  doesn't know is empty to it, an `@value` of a variable not set starts no
  paragraph), our side splits them as texi2any does or the difference is
  ignored

Deliberate, so the tree shows the source as it is written and edited:

- Macros, `@value`, `@alias`, `@linemacro` and conditionals aren't expanded or
  evaluated, so input whose structure only works after expansion (a macro
  that opens a brace the text after its call closes) has parse errors
- Macro calls aren't told apart from commands texi2any doesn't know, as
  that depends on the definitions before them, in the file or one it
  includes: a call's braces end at a blank line, which texi2any allows in
  them (only `texinfo-tex-test.texi` among the manuals has one); spaces
  before its brace make a brace group; a macro with one parameter called
  without braces (397 calls in Autoconf's, Calc's and the LaTeX2e
  manuals) or a `@linemacro` call (21 in Texinfo's) is a command followed
  by text; and a macro named like a built-in command (`@linemacro code`)
  is parsed as the built-in
- `@click` stays `@click`; texi2any outputs the command `@clickstyle` chose
- Files not in UTF-8 (`@documentencoding ISO-8859-1`, Shift-JIS) are for the
  editor to convert, as tree-sitter reads UTF-8; read as UTF-8, a Shift-JIS
  character holding the byte `@` starts a command
- Block commands and `@end` only count at the start of a line, as the Texinfo
  manual requires ("at the beginning of a line by itself"). texi2any's error
  recovery also accepts them in the middle of one, with a warning
  (`@group x @end group`, `@table` on an `@item` line); no manual among the
  examples does that
- So do line commands, which "occupy an entire line" in the Texinfo manual:
  one in the middle of a line (`text @sp 2`) is a command without argument
  followed by text, where texi2any takes the rest of the line as its
  argument, with a warning
- Content texi2any reports and drops is kept as written: `@tab` cells beyond
  the multitable's columns, `@tab` before the first `@item`, `@headitem` or
  `@itemx` in `@itemize`, `@item` outside a list, a recursive
  `@insertcopying`. Only an `@end` that ends no block is marked
  (`unmatched_end`)
- A brace on the line after its command (`@code` then `{a}`), which texi2any
  accepts with a warning that the command "must not be followed by new
  line": the brace starts a group
- Commands inside a malformed `@end` line (`@end @code{quotation}`) are part
  of the `unmatched_end` node, not parsed
- A DEL character is text. The Texinfo manual makes it a comment character
  for TeX, up to the end of the line, merging the next line into it; texi2any
  deletes that text before parsing, which a tree of the source as written
  can't show. No manual among the examples has one
- An `@itemx` or `@deffnx` after text, which texi2any reports, stays in the
  body as written; texi2any moves what comes before it out of the body. So
  does one after a paragraph holding only an `@anchor`, which texi2any
  accepts (Maxima's `trigtools.texi`)

## Queries

- [x] `queries/highlights.scm`, with tests in `test/highlight/`
- [x] Highlight gaps from overlapping patterns: `@example` contents, markup
  in heading titles, `@defvar` entities, `@emph`/`@var` arguments
- [x] `queries/injections.scm`: TeX/LaTeX, HTML, XML, DocBook raw blocks
- [x] Inject LaTeX into `@math` and `@displaymath`
- [x] `queries/folds.scm`
- [x] `queries/tags.scm`: nodes, anchors and documented functions and variables
  as definitions; cross-references and menu entries as references
- [x] Tag `@anchor {target}` and `@value {var}`, with blanks before the
  brace (as in `info-stnd.texi`)
- [x] Register queries in `tree-sitter.json` (highlights, injections and
  tags: it has no key for folds and textobjects)
- [x] Queries for several editors, favouring none: `queries/` uses the
  plainer capture name where editors differ (`.tsqueryrc.json`
  enforces them), and `editors/helix/queries/` and `editors/neovim/queries/`
  are generated from them with each editor's names, checked against its
  lists. Textobjects too. No indent queries, deliberately: Texinfo isn't
  indented by nesting, and editors keep the previous line's indentation
  without them
- [x] Textobjects: a definition's `@function.inside` included its `@deffnx`
  lines, and a table item's `@entry.inside` its `@itemx` lines, siblings of
  the body that a query can't start after. Now the body is a node, as in
  texi2any (`def_item`, `table_definition`): a `definition_body` or an
  `item_body`, in the `body` field, after the last `@deffnx` or `@itemx`
  line and the index entries, blank lines and blocks (a conditional holding
  an index entry) before it (+164 parser states, no slower)
- [x] Tests of the folds, textobjects and injections
  (`script/test-queries.py`, on `test/queries/`): `tree-sitter test` reads
  each test file of these queries as the query itself (0.26.11)
- [x] Textobjects: `.` skips anonymous nodes, so in `@xref{a, b, , d}` the
  empty argument's comma joined `b`'s `@parameter.around`. A comma is only
  taken between arguments in consecutive fields now, which the query lists;
  an argument followed by an empty one is alone, like the last, as no query
  can tell which comma is its own

## Project

- [x] `script/compare-texi2any.py`: compare trees with texi2any's
- [x] Run the comparison on the inputs of texi2any's own parser tests
  (`texinfo-tests/`): 1,446 of 1,755 matched exactly then
- [x] Triage the test inputs that differ: 1,579 of 1,755 then matched exactly.
  The remaining 176 are the macro and value suites (the script's simple
  macro expansion), invalid input that texi2any discards (unknown commands,
  misplaced `@tab` and `@headitem`, stray `@end`), `@click` (texi2any
  substitutes the `@clickstyle` symbol), and the divergences below
- [x] Mirror `@clickstyle`/`@click` and `@alias` in the comparison
- [x] Fuzz the manuals too (`tree-sitter fuzz`, random edits anywhere), not
  only texi2any's test inputs
- [x] CI time: no CI for Markdown-only changes; `nix flake check` runs the
  same checks locally before pushing, and CI keeps the Nix store between
  runs, so only the checks whose inputs changed run again. The bindings on
  Linux, macOS and Windows on every push
- [x] Each check's inputs are only the files it reads, so that a change to
  a query or a snapshot doesn't rerun the comparisons and the fuzzing; the
  fuzzing checks start from a fixed seed, as Nix caches their result; each
  check's command and the extraction of the manuals (now
  `script/extract-examples.py`, from a manifest in `flake.nix`, the same
  687 files) were bash that shellcheck never saw (found by review)
- [x] Standard tools instead of shell scripts: `nix flake check` runs every
  check (cached by Nix), `nix fmt` formats (treefmt-nix), pre-commit runs
  the git hooks (git-hooks.nix), all declared with flake-parts; the justfile only holds
  one-line aliases
- [x] Longer fuzzing locally: `just fuzz-deep` (`tree-sitter fuzz`) and
  `just fuzz-scanner` (libFuzzer with sanitizers; LLVM is 23, as LLVM 21's AddressSanitizer deadlocks at startup on macOS 27)
- [x] Test the checks themselves (`script/test-checks.py`): each must fail
  on inputs in `test/checks/` that it is meant to catch
- [x] Compare our tree of texi2any's own expansion of each file
  (`--macro-expand`) rather than imitate the expansion: 300 fewer lines in
  the script, and the differences it caused in the macro suites are gone
- [x] CI (`.github/workflows/ci.yml`): generated `src/` up to date, tests,
  fuzzing, parsing the manuals, and both texi2any comparisons, which must
  find exactly the differences listed in `test/texi2any-differences.txt`
- [x] README: supported features, editor setup, development with Nix
- [x] `CLAUDE.md` with project conventions
- [x] Expand `@linemacro` calls in the texi2any comparison
- [x] In `--structure` mode, texi2any's line commands come from its own
  command table, not from what our parser produced, so a line command we
  don't parse as one shows up
- [x] More example manuals: Emacs's manuals, glibc, gdb, binutils, bash,
  coreutils, autoconf, automake
- [x] Manuals by other authors, found with nix-locate: SBCL (with ASDF),
  Maxima (English and Japanese), GNU Parallel, Stow, LaTeX2e. They showed
  that a comma-separated argument (`@xref{`) didn't take a conditional
  block, so a brace left open before one broke the enclosing block; and
  `.txi` files are checked too
- [x] Reduce the parser's size: `src/parser.c` was 29 MB. The line and brace
  command keywords, now recognised by the scanner, made each parser state
  where a command can start carry about 200 entries; it went to 6 MB, with
  the same tree. Features added since have taken it back to 24 MB (13,600
  states), each with its cost measured against the budget
- [x] Tree snapshots of every manual and test input
  (`script/tree-snapshots.py`), and a simulation of typing mistakes in the
  manuals that checks error locality and incremental parsing
  (`script/check-edits.py`), both in CI
- [x] A long line parsed in time quadratic in its length (a line of 8,000
  `@code{x}` took 4.6 s, 35 ms now): the scanner asked for the column before
  every token, which tree-sitter counts from the start of the line. It asks
  only where the answer decides the token now, and the parse check times
  generated long lines too (`--stress`), as no manual has a line over a
  kilobyte (found by review)
- [x] Parse errors and parse speed of the manuals checked
  (`script/parse-examples.py`, timing each parse's CPU time); texi2any's
  test inputs and the manuals fuzzed with `tree-sitter fuzz`, from a corpus
  generated by the check
- [x] The generated Neovim queries run in Neovim, by a check: its regexes
  are Vim's, which no other check reads, and every pattern starting `^@`
  had made Neovim reject the highlights
- [x] The comparison reads texi2any's command names whole, as texi2any does:
  `@foo-barindex` (after `@defindex foo-bar`) was `@foo`, a false difference
- [x] Python source distributions include `src/scanner.c`: `setup.py`, as
  tree-sitter 0.26's template writes it, added it only while compiling, so a
  wheel built from the sdist failed to import
- [x] A check that the scanner's token enum and its tables of layouts and
  section levels agree with the grammar, and that its tables of names don't
  overlap (`script/check-scanner.py`), so the order in which the scanner
  tries them doesn't matter; and since review, that its names are texi2any's,
  in texi2any's classes (line, paragraph-ending, brace commands, the kinds
  of blocks, the preformatted ones and the conditionals), and that every
  sectioning command in the grammar is in its table. It found two
  obsolete commands among the line commands
  (`@setcontentsaftertitlepage`), which texi2any no longer knows
- [x] A budget for the parser's state count (`script/parser-size.py`, a
  flake check), raised only on purpose: features added 1-8% each, which
  the check against the previous push didn't flag, until the parser had
  twice the states it had once the scanner recognised the command names
- [x] The comparison checks each command's arguments too, field by field
  where the grammar splits them as texi2any does (cross-references, `@node`,
  `@float`, definition lines, menu entries) and as one elsewhere, by their
  letters and digits; menu entries are compared as commands. It found three
  bugs: `@c|` wasn't a comment, a heading wasn't one inside `@footnote`,
  and an accent at the end of a heading took the next line's first
  character; and the gaps above
- [x] The corpus tests' inputs are compared with texi2any too
  (`--corpus-inputs`, in the `compare-tests` check), as their trees are
  written by hand: six differ, each listed with its reason. It found that
  `@deffn Command foo@` and then `bar` names `foobar` in texi2any, which
  a test had as two words, and that texi2any takes the text after `@bye`
  on its line as its argument (now the `argument` field of `bye`)
- [x] The comparison counts where a block left open ends, as where
  texi2any closes one at a `@node`: it couldn't see a block that went on
  past it (no new difference: the grammar closes them as texi2any does
  now, but in the middle of a line). Letters of any script count in
  arguments, not only ASCII ones (no new difference either, in Maxima's
  Japanese manual among others)
- [x] texi2any and the manuals pinned apart from the tools (the `reference`
  input in `flake.nix`, a fixed nixpkgs revision): `nix flake update` for a
  newer tree-sitter or LLVM no longer brings a new texi2any and new manuals
  with it, which change many listed differences and snapshots at once
- Not published to npm, crates.io or PyPI, by choice: the names are left to
  a possibly more complete implementation. Editors build the grammar from
  the repository; releases, if any, are git tags
