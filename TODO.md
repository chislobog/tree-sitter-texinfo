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
- [ ] Report a stray `@end` with no open block as an error (currently text)

## Commands and arguments

- [x] Blocks and line commands at the start of a line inside brace commands
  such as `@footnote` and `@caption` (found by the texi2any comparison)
- [ ] Paragraphs inside `@footnote`, `@caption` and `@shortcaption`; their text
  is flat for now
- [x] Split brace arguments on commas into fields named by position for
  `@xref`, `@pxref`, `@ref`, `@inforef`, `@link`, `@uref`, `@url`, `@email`,
  `@image`, `@abbr`, `@acronym`, `@namedanchor`
- [x] `@node` arguments in fields (`node_line`: node, next, previous, up)
- [ ] Split the inline conditionals' arguments (`@inlineraw`'s second argument
  is raw text)
- [ ] `@float` arguments (type, label)
- [x] A block at the start of a line closes an ordinary brace command, as in
  texi2any (conditionals don't)
- [ ] Accent commands without braces: `@'e`, `@"u`, and letter accents with
  a space: `@u o`, `@v c`
- [ ] `@value{name}` as its own node
- [ ] `@math{...}` body for TeX injection
- [ ] Plain-text line arguments (`@set`, `@clear`, `@include`, `@setfilename`,
  `@documentencoding`, ...) as a single text node. texi2any keeps `@set`
  values as raw text and parses them where `@value` uses them
- [ ] `@set NAME VALUE` split into name and value
- [x] Definition lines: `@deffn`, `@deftypefn`, ..., their `x` variants and
  `@defline`/`@deftypeline` split into `category`, `class`, `type`, `entity`
  and `arguments` fields (braces group words)
- [x] `@` at the end of a definition line's arguments is a `line_continuation`
  (still a `symbol_command` before the arguments start)
- [ ] A definition word glued to a command (`foo@var{x}`) counts as two
  words
- [ ] Index entries with `@subentry`, `@seeentry`, `@seealso`, `@sortas`
- [ ] `@multitable` prototype argument and `@columnfractions`
- [ ] `@tab` as a cell separator in multitable rows
- [ ] `@headings`, `@everyheading` and friends with `@|` separators
- [ ] `@alias NEW = EXISTING`, `@definfoenclose`
- [x] Missing line commands from texi2any's table: `defline`, `deftypeline`,
  `xrefname`, `clickstyle`, the two-letter index commands, ...
- [ ] User-defined index commands (`@defindex op` makes `@opindex`), which
  are only known at run time
- [x] Missing blocks: `@documentinfo`, `@publication`
- [x] Fix typo: `codequoteundirective` should be `codequoteundirected`
- [ ] No-brace commands: `@refill`, `@thischapter`, `@thisfile`, ...

## Structure

Deliberate divergences from texi2any, which tolerates these with warnings:
block commands only start a line (texi2any also accepts `text @html` and
`@tex in tex @end tex` on one line), and `@end` only ends a block at the start
of a line.

- [ ] An unclosed brace in a line command's argument (`@chapter A @code{x`)
  should close at the end of that line, as in texi2any; now it runs on to the
  next blank line
- [x] A block right after `@footnote{` (`@footnote{@quotation`)

- [x] `@itemx` after index entries or comments that follow an `@item`
  (make, texinfo; found with `compare-texi2any.py --structure`)
- [x] `@item` inside a conditional block inside a list, e.g. `@ifclear X`
  wrapping an `@item` in a `@table` (sed, gawk)
- [ ] Menu entries: `* Node::  Description`, `* Name: Node.  Description`,
  menu comments (also in `@direntry`)
- [ ] Decide on nested sections (chapter containing sections) vs flat headings
- [ ] Name the command field `name` in headings too (it is `command`), and
  give `@bye` a `name` field, so every command node is queried the same way
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
- [ ] TeX lines before `\input texinfo` (e.g. gawk's Italian manual)

## Queries

- [ ] `queries/highlights.scm`
- [ ] `queries/injections.scm`: TeX/LaTeX, HTML, XML, DocBook blocks and `@math`
- [ ] `queries/folds.scm`
- [ ] `queries/tags.scm`: nodes and anchors as definitions, cross-references as
  references
- [ ] Register queries in `tree-sitter.json`

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
- [ ] CI: generate, check `src/` is up to date, test, fuzz, parse examples,
  compare with texi2any
- [ ] README: supported features, editor setup, development with Nix
- [x] `CLAUDE.md` with project conventions
- [ ] Expand `@linemacro` calls in the texi2any comparison
- [ ] In `--structure` mode, classify texi2any's commands from one shared list
  of line commands, headings and blocks, not from the commands our parser
  produced; a line command we don't know is currently hidden there
- [ ] More example manuals (e.g. Emacs Lisp reference)
- [ ] Release automation (npm, crates.io, PyPI)
