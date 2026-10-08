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
- [x] `@displaymath` body (as raw text)
- [ ] Parse commands inside `@displaymath`, as texi2any does (e.g. `@r{...}`
  in the Texinfo manual)
- [x] `@verb{xTEXTx}` with an arbitrary delimiter character
- [ ] Report a stray `@end` with no open block as an error (currently text)

## Commands and arguments

- [x] Blocks and line commands at the start of a line inside brace commands
  such as `@footnote` and `@caption` (found by the texi2any comparison)
- [ ] Paragraphs inside `@footnote`, `@caption` and `@shortcaption`; their text
  is flat for now
- [ ] Split brace arguments on commas for multi-argument commands: `@xref`,
  `@ref`, `@pxref`, `@inforef`, `@link`, `@uref`, `@url`, `@email`, `@image`,
  `@abbr`, `@acronym`, `@anchor`, `@namedanchor`, `@inlinefmt`,
  `@inlinefmtifelse`, `@inlineraw`, `@inlineifset`, `@inlineifclear`, `@U`
- [ ] `@node` arguments split on commas (name, next, previous, up)
- [ ] Accent commands without braces: `@'e`, `@"u`, and letter accents with
  a space: `@u o`, `@v c`
- [ ] `@value{name}` as its own node
- [ ] `@math{...}` body for TeX injection
- [ ] Plain-text line arguments (`@set`, `@clear`, `@include`, `@setfilename`,
  `@documentencoding`, ...) as a single text node. texi2any keeps `@set`
  values as raw text and parses them where `@value` uses them
- [ ] `@set NAME VALUE` split into name and value
- [ ] Definition lines: `@deffn`, `@deftypefn`, ... and their `x` variants split
  into category, name and arguments (braces group words). `@` at the end of
  a definition line continues it and should not be a `symbol_command`
- [ ] Index entries with `@subentry`, `@seeentry`, `@seealso`, `@sortas`
- [ ] `@multitable` prototype argument and `@columnfractions`
- [ ] `@tab` as a cell separator in multitable rows
- [ ] `@headings`, `@everyheading` and friends with `@|` separators
- [ ] `@alias NEW = EXISTING`, `@definfoenclose`
- [ ] Missing line commands: `defline`, `deftypeline`, `xrefname`, `maketitle`,
  `bsixpaper`, `clickstyle`, `fonttextsize`, `microtype`,
  `{even,odd,every}{heading,footing}marks`, `verbatiminclude`, `subentry`,
  user-defined index commands
- [x] Missing blocks: `@documentinfo`, `@publication`
- [x] Fix typo: `codequoteundirective` should be `codequoteundirected`
- [ ] No-brace commands: `@refill`, `@thischapter`, `@thisfile`, ...

## Structure

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
