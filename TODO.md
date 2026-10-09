# TODO

Coverage of the [Texinfo manual](https://www.gnu.org/software/texinfo/manual/texinfo/texinfo.html)
(version 7.3). Command classes follow `tta/data/command_data.txt` in the Texinfo
sources, which is what `texi2any` itself uses.

## Done

- [x] `\input texinfo` first line
- [x] Comments: `@c`, `@comment`, including at end of line
- [x] Line commands with the rest of the line as argument
- [x] Sectioning and heading commands, all levels (flat, not nested)
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
  `@xml`, `@docbook`. `@` still introduces commands inside them; not parsed yet
- [x] Parse commands inside `@displaymath`, as texi2any does (it is a normal
  block now, not raw)
- [x] `@verb{xTEXTx}` with an arbitrary delimiter character
- [x] A stray `@end` that ends no open block is an `unmatched_end` node, to
  the end of its line, which editors should report

## Commands and arguments

- [x] Blocks and line commands at the start of a line inside brace commands
  such as `@footnote` and `@caption` (found by the texi2any comparison)
- [x] Paragraphs inside `@footnote`, `@caption` and `@shortcaption`, with
  blocks and line commands
- [x] Split brace arguments on commas into fields named by position for
  `@xref`, `@pxref`, `@ref`, `@inforef`, `@link`, `@uref`, `@url`, `@email`,
  `@image`, `@abbr`, `@acronym`, `@namedanchor`
- [x] `@node` arguments in fields (`node_line`: node, next, previous, up)
- [x] Split the inline conditionals' arguments into `condition` and `text`
  (multi-line, like a conditional block) or, for `@inlineraw`, a raw `body`,
  injected in the language its format names
- [ ] `@inlinefmtifelse`'s three arguments (a third field costs several
  hundred parser states)
- [x] `@float` arguments: `type` and `label` fields; the label is a tag and
  highlighted like a node name. Commands with named arguments (`@xref`)
  aren't allowed in the type
- [x] A block at the start of a line closes an ordinary brace command, as in
  texi2any (conditionals don't)
- [x] Accent commands without braces: `@'e`, `@"u`, and letter accents with
  a space: `@u o`, `@v c`; the character is the command's `argument`
- [x] `@value{name}`: an inline command whose name is highlighted as a
  variable, like `@set`'s
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
- [ ] A definition word glued to a command (`foo@var{x}`) counts as two
  words
- [x] Index entries with `@subentry` (a nested `subentry` node, as in
  texi2any), `@seeentry`, `@seealso`, `@sortas`
- [ ] `@multitable` prototype argument and `@columnfractions`
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
- [x] No-brace commands: `@refill`, `@thischapter`, `@thisfile`, ... parse as
  inline commands without arguments, as in texi2any

## Structure

Deliberate divergences from texi2any, which tolerates these with warnings:
block commands only start a line (texi2any also accepts `text @html` and
`@tex in tex @end tex` on one line), and `@end` only ends a block at the start
of a line.

- [x] An unclosed brace in a line command's argument (`@chapter A @code{x`)
  closes at the end of that line, as in texi2any. Not yet in `@node` lines
  or in the commands with named arguments (`@xref{` in a heading)
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
- [x] Sections nest by level (`part`, `top`, `chapter`, `section`,
  `subsection`, `subsubsection`), each with its `node` line and `heading`.
  A section inside a conditional ends at its `@end`; the Top node in
  `@ifnottex` and alternative headings per output format therefore don't
  contain the content after the conditional
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
- [ ] A brace argument crossing a conditional's boundary (e.g. `@footnote{`
  inside `@ifnottex`, closed after its `@end`) can't be represented and stays
  a one-character error
- [x] TeX lines before `\input texinfo` (e.g. gawk's Italian manual): text,
  as in texi2any, which only recognises `\input` on the first line

## Differences from texi2any

From comparing with texi2any on its own test inputs (137 of 1,755 differ;
the manuals match fully).

Gaps, where we give a parse error and texi2any recovers:

- [ ] A block command on the last line without a newline (`@iftex` at the end
  of the file) is an `ERROR`; it should be a block ending in `missing_end`
- [ ] An unclosed brace in a `@multitable` prototype (`@multitable { a`)
  should close at the end of the line
- [ ] Badly braced definition lines (`@deffn {cat name`) and a definition line
  ending in `@` at the end of the file
- [ ] `@c` right after a `@ ` symbol command on some line commands
  (`08misc_commands/comment_space_command_on_line`)
- [ ] Files not in UTF-8 (`@documentencoding ISO-8859-1`, Shift-JIS): the
  editor has to convert them, as tree-sitter reads UTF-8

Deliberate, so the tree shows the source as it is written and edited:

- Macros, `@value`, `@alias`, `@linemacro` and conditionals aren't expanded or
  evaluated. Most of the remaining differences (the `50value`, `58alias`,
  `60macro` and `65linemacro` suites) come from the comparison script only
  approximating texi2any's expansion
- `@click` stays `@click`; texi2any outputs the command `@clickstyle` chose
- Block commands and `@end` only count at the start of a line; texi2any also
  accepts them in the middle of one, with a warning (`@group x @end group`,
  `@table` on an `@item` line, `@deffn` in a menu description)
- Content texi2any reports and drops is kept as written: `@tab` cells beyond
  the multitable's columns, `@tab` before the first `@item`, `@headitem` or
  `@itemx` in `@itemize`, `@item` outside a list, a recursive
  `@insertcopying`. Only an `@end` that ends no block is marked
  (`unmatched_end`)
- Commands inside a malformed `@end` line (`@end @code{quotation}`) are part
  of the `unmatched_end` node, not parsed

## Queries

- [x] `queries/highlights.scm`, with tests in `test/highlight/`
- [x] Highlight gaps from overlapping patterns: `@example` contents, markup
  in heading titles, `@defvar` entities, `@emph`/`@var` arguments
- [x] `queries/injections.scm`: TeX/LaTeX, HTML, XML, DocBook raw blocks
- [x] Inject LaTeX into `@math` and `@displaymath`
- [x] `queries/folds.scm`
- [x] `queries/tags.scm`: nodes, anchors and documented functions and variables
  as definitions; cross-references and menu entries as references
- [x] Register queries in `tree-sitter.json`

## Project

- [x] `script/compare-texi2any.py`: compare trees with texi2any's
- [x] Run the comparison on the inputs of texi2any's own parser tests
  (`texinfo-tests/`): 1,446 of 1,755 match exactly
- [x] Triage the test inputs that differ: 1,579 of 1,755 now match exactly.
  The remaining 176 are the macro and value suites (the script's simple
  macro expansion), invalid input that texi2any discards (unknown commands,
  misplaced `@tab` and `@headitem`, stray `@end`), `@click` (texi2any
  substitutes the `@clickstyle` symbol), and the divergences below
- [ ] Mirror `@clickstyle`/`@click` and `@linemacro` calls in the comparison
- [x] CI (`.github/workflows/ci.yml`): generated `src/` up to date, tests,
  fuzzing, parsing the manuals, and both texi2any comparisons with minimum
  match rates (`--fail-under`)
- [x] README: supported features, editor setup, development with Nix
- [x] `CLAUDE.md` with project conventions
- [ ] Expand `@linemacro` calls in the texi2any comparison
- [ ] In `--structure` mode, classify texi2any's commands from one shared list
  of line commands, headings and blocks, not from the commands our parser
  produced; a line command we don't know is currently hidden there
- [x] More example manuals: Emacs's user manual and Lisp reference
- [x] Reduce the parser's size: `src/parser.c` was 29 MB. The line and brace
  command keywords, now recognised by the scanner, made each parser state
  where a command can start carry about 200 entries; it is now 6 MB, with
  the same tree
- [ ] Release automation (npm, crates.io, PyPI)
