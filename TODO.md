# TODO

Coverage of the [Texinfo manual](https://www.gnu.org/software/texinfo/manual/texinfo/texinfo.html)
(version 7.3). Command classes follow `tta/data/command_data.txt` in the Texinfo
sources, which is what `texi2any` itself uses.

## Done

- [x] `\input texinfo` first line
- [x] Comments: `@c`, `@comment`, including at end of line
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
  `@xml`, `@docbook`. `@` still introduces commands inside them; not parsed yet
- [x] Parse commands inside `@displaymath`, as texi2any does (it is a normal
  block now, not raw)
- [x] `@verb{xTEXTx}` with an arbitrary delimiter character
- [x] A stray `@end` that ends no open block is an `unmatched_end` node, to
  the end of its line, which editors should report
- [x] Keep the preformatted context through blocks nested in `@example`
  and the other preformatted blocks (`@group`, `@quotation`, even `@menu`),
  where a blank line doesn't close a brace command, as in texi2any
- [ ] Macro names starting with a digit: `@macro 1foo {x}` is remembered, but
  `read_name` in `src/scanner.c` and the grammar's command name reject the
  digit, so `@1foo{one}` is the symbol `@1`, text and a brace group, not a
  call. texi2any reads command names as `[[:alnum:]][[:alnum:]_-]*`
- [ ] Macro names of 32 characters or more: `read_name` rejects them
  (`MAX_NAME`), and `remember_macro` cuts the name to 32 characters before
  hashing it, so their calls aren't recognised and an argument spanning a
  blank line gives brace errors
- [ ] `@unmacro` doesn't remove the macro from the scanner's table: after
  `@linemacro code {x}` and `@unmacro code`, `@code{one}` is still a
  `line_command`, and the entry still counts towards `MAX_MACROS`
- [x] Remember a file's first 128 macros (`MAX_MACROS`), not 64: glibc's
  `manual/macros.texi` defines 59
- Known limitation: the scanner remembers macros by a 32-bit FNV-1a hash of
  their name, as their names don't fit in tree-sitter's 1 KB of serialized
  state, so two names with the same hash (`mjkmtxnhps` and `mjbmpqijuu`) are
  one macro to it. For 128 macros, the chance of that is about 2 in a million

## Commands and arguments

- [x] Blocks and line commands at the start of a line inside brace commands
  such as `@footnote` and `@caption` (found by the texi2any comparison)
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
- [x] Split the inline conditionals' arguments into `condition` and `text`
  (multi-line, like a conditional block) or, for `@inlineraw`, a raw `body`,
  injected in the language its format names
- [x] `@inlinefmtifelse`'s three arguments: `condition`, `then` and `else`,
  both texts spanning blank lines and blocks (+1,074 parser states)
- [x] `@float` arguments: `type` and `label` fields; the label is a tag and
  highlighted like a node name. Commands with named arguments (`@xref`)
  aren't allowed in the type
- [x] A block at the start of a line closes an ordinary brace command, as in
  texi2any (conditionals don't)
- [x] Accent commands without braces: `@'e`, `@"u`, and letter accents with
  a space: `@u o`, `@v c`; the character is the command's `argument`
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
- [x] A call of a macro with exactly one parameter, without braces, takes
  the rest of the line as its `argument`, as in texi2any (Calc's `@infoline`,
  autoconf's `@caindex`); in paragraph lines only, where nearly all such
  calls are: in every context it would cost 2,000 parser states. The scanner
  records each macro's kind, and gives a call followed by `{` the ordinary
  macro call
- [x] A `@linemacro` call is a `line_command` whose `argument` is the rest of
  its line, continued over lines ending in `@`; a brace group in it may hold
  line breaks, but not a brace command's braces (texi2any's tests only).
  How the arguments split depends on the number of parameters, which the
  grammar doesn't know
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
- [x] No-brace commands: `@refill`, `@thischapter`, `@thisfile`, ... parse as
  inline commands without arguments, as in texi2any

## Structure

Deliberate divergences from texi2any, which tolerates these with warnings:
block commands only start a line (texi2any also accepts `text @html` and
`@tex in tex @end tex` on one line), and `@end` only ends a block at the start
of a line.

- [x] An unclosed brace in a line command's argument (`@chapter A @code{x`)
  closes at the end of that line, as in texi2any, also in the commands with
  named arguments (`@xref{` in a heading). Not yet in `@node` lines
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
- [x] An unclosed brace command also ends at the `@end` of an enclosing block
  (`@code{` in an `@example` inside a `@quotation`, then `@end quotation`),
  which closes the blocks inside it, as in texi2any; the rest of the file
  stayed inside the brace
- [ ] A brace argument crossing a conditional's boundary (e.g. `@footnote{`
  inside `@ifnottex`, closed after its `@end`, or TeX braces in Calc's and
  Gnus's `@iftex` blocks) can't be represented: the brace closes at the
  conditional's end, and the later `}` is a one-character error
- [x] TeX lines before `\input texinfo` (e.g. gawk's Italian manual): text,
  as in texi2any, which only recognises `\input` on the first line
- [x] The `@macro` line has `macro` and `parameter` fields, as texi2any
  reads it, rather than an argument parsed for commands; anything else on it
  but a comment is raw text. Tags and highlighting use the fields
- [x] A call of a macro defined in the file may have spaces before its
  brace, as in texi2any, also when its argument spans a blank line

## Differences from texi2any

From comparing with texi2any on its own test inputs (96 of 1,755 differ, all
listed with their reasons in `test/texi2any-differences.txt`; the manuals
match but for three binutils fragments meant for `@include`, and the parse
errors in Calc's, Gnus's and gawk's `@iftex` blocks).

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
- [ ] Files not in UTF-8 (`@documentencoding ISO-8859-1`, Shift-JIS): the
  editor has to convert them, as tree-sitter reads UTF-8

Deliberate, so the tree shows the source as it is written and edited:

- Macros, `@value`, `@alias`, `@linemacro` and conditionals aren't expanded or
  evaluated, so input whose structure only works after expansion (a macro
  that opens a brace the text after its call closes) has parse errors
- `@click` stays `@click`; texi2any outputs the command `@clickstyle` chose
- Block commands and `@end` only count at the start of a line, as the Texinfo
  manual requires ("at the beginning of a line by itself"). texi2any's error
  recovery also accepts them in the middle of one, with a warning
  (`@group x @end group`, `@table` on an `@item` line); no manual among the
  examples does that
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
- [x] Register queries in `tree-sitter.json`
- [x] Queries for several editors, favouring none: `queries/` uses the
  plainer capture name where editors differ (`.tsqueryrc.json`
  enforces them), and `editors/helix/queries/` and `editors/neovim/queries/`
  are generated from them with each editor's names, checked against its
  lists. Textobjects too. No indent queries, deliberately: Texinfo isn't
  indented by nesting, and editors keep the previous line's indentation
  without them
- [ ] Textobjects: a definition's `@function.inside` includes its `@deffnx`
  lines, and a table item's `@entry.inside` its `@itemx` lines: they are
  siblings of the body, and a query can't start after the last of them.
  Needs the extra lines grouped in the tree. Also, `.` skips anonymous nodes,
  so in `@xref{a, b, , d}` the empty argument's comma joins `b`'s
  `@parameter.around`

## Project

- [x] `script/compare-texi2any.py`: compare trees with texi2any's
- [x] Run the comparison on the inputs of texi2any's own parser tests
  (`texinfo-tests/`): 1,446 of 1,755 match exactly
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
  the same tree
- [x] Tree snapshots of every manual and test input
  (`script/tree-snapshots.py`), and a simulation of typing mistakes in the
  manuals that checks error locality and incremental parsing
  (`script/check-edits.py`), both in CI
- [x] Parse errors and parse speed of the manuals checked
  (`script/parse-examples.py`, timing each parse's CPU time); texi2any's
  test inputs and the manuals fuzzed with `tree-sitter fuzz`, from a corpus
  generated by the check
- [x] The generated Neovim queries run in Neovim, by a check: its regexes
  are Vim's, which no other check reads, and every pattern starting `^@`
  had made Neovim reject the highlights
- [ ] Release automation (npm, crates.io, PyPI)
