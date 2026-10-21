# Design

How the grammar parses Texinfo, and why: what was done, found and decided,
by area, with what each choice cost where it was measured (parser states,
speed). The reference is the [Texinfo
manual](https://www.gnu.org/software/texinfo/manual/texinfo/texinfo.html)
(version 7.3) and `texi2any`, whose command classes come from
`tta/data/command_data.txt` in the Texinfo sources. Open work is in
`TODO.md`.

## Basics

- `\input texinfo` first line
- Comments: `@c`, `@comment`, including at end of line, and followed by
  any character that can't continue a name (`@c|`, `@comment@x`), which
  texi2any also reads as a comment (found by comparing arguments)
- Line commands with the rest of the line as argument
- Sectioning and heading commands, all levels
- Paragraphs separated by blank lines
- Brace commands, nested, with arguments spanning lines
- Symbol commands: `@@`, `@{`, `@}`, `@*`, `@.`, `@ `, accents like `@'`
- A form feed in the middle of a paragraph's line ends the paragraph,
  as the Texinfo manual says and texi2any does (found by review: the
  differences listed it as deliberate); elsewhere it is a blank, as at the
  start of a line, in braces, a line command or a preformatted block
- Only texi2any's symbol commands and accents are symbol commands
  (found by review): `@#`, `@%`, `@_` and the like were, which texi2any
  reports as "unexpected @"; the `@` is alone now, as before a character
  other than ASCII, and the character text
- Bare brace groups
- Blocks with optional line argument, nested
- Lists: `@itemize`, `@enumerate`, `@table`, `@ftable`, `@vtable`, `@multitable`
  with `@item`, `@itemx`, `@headitem`
- `@bye` and ignored trailing content

## External scanner

- Match `@end NAME` against the open block
- Raw blocks, body as plain text: `@verbatim`, `@ignore` (nested `@ignore`
  counted), `@macro`, `@rmacro`, `@linemacro`
- Format-raw blocks, body as text for injections: `@tex`, `@latex`, `@html`,
  `@xml`, `@docbook`. texi2any still reads commands inside them; the tree
  keeps the body raw, for the injected language
- Parse commands inside `@displaymath`, as texi2any does (it is a normal
  block now, not raw)
- `@verb{xTEXTx}` with an arbitrary delimiter character
- A stray `@end` that ends no open block is an `unmatched_end` node, to
  the end of its line, which editors should report
- An `@end` of no block at all (`@end exmaple`, and each `@end e`, `@end
  ex`, ... while `@end example` is typed) or without a name ends nothing
  (found by review): texi2any warns of it ("unknown @end", "@end missing
  argument") and ignores it, a line command that ends neither the blocks
  open, nor the paragraph, nor a brace command. It closed every open block
  but conditionals, as the `@end` of a block that isn't open does in
  texi2any, so a typo in one `@end` broke the rest of the document. In a
  paragraph, or preformatted text, it is a line of the paragraph, by a
  token of its own where the paragraph may go on, as a paragraph's line
  commands have; in braces, a line item, as a line command is. 44 parser
  states (5,490 to 5,534)
- An unclosed brace command closes before a node, a section or `@bye`
  on the next line at the end of the line before it, which the scan reads
  to the command: decided at the command by its column, an edit before it
  on its line, then undone, left the section inside an unclosed `@math{`,
  as tree-sitter reused its subtree (found by `tree-sitter fuzz`, which the
  checks ran without failing on what it reports)
- Nor does an incremental parse reuse a node that only holds what the
  tokens after it decide (found by `just fuzz-deep` in two of texi2any's
  test inputs). After an edit by a paragraph's index entry, an `@anchor`
  or `@image` on the next line left the paragraph: the scanner gives it
  the token that starts no paragraph only where no line command of the
  paragraph may follow, and after the reused node of those line commands
  none could. And a newline made the start of a definition's or an item's
  body, where an edit had left no x line after it, stayed one when the
  `@deffnx` came back. The rules are inlined (4 parser states); a corpus
  test of each fails the `fuzz` check without that
- A scan of the first stage that reads past blanks and fails no longer
  leaves the second to go on from where it stopped (found by review): an
  accent at the end of a line read past the newline to look for its
  argument, so a comment line after it ended the paragraph and a brace
  left open closed on the next line; `@end ifinfo @c` lost its comment,
  and `name@c@code{x}` on a definition line was one command. The fuzzer
  builds the scanner with checks of what no tree shows
  (`test/fuzz/checked_scanner.c`): a newline in the padding before a
  token, a zero-width token with a length, a token not valid where it is
  returned and a read past the end of the file
- A `@node`'s look ahead for its section skips a node description
  block only to the next node, section or `@bye`, where one left open
  closes, as in texi2any (found by review): it read to the end of the
  file, so a parse of many nodes with one open was quadratic (273 KB in
  6 s) and each keystroke after one cost a parse of the rest of the file.
  A section there is the node's; the parse check times such an input
  (`--stress`)
- Edge cases found by review: a `@verb` delimiter that isn't UTF-8,
  which tree-sitter reads as -1, was taken for none, as the scanner marked
  that with -1; nested `@ignore` blocks were counted to 64, now to any
  depth (macro definitions nested in a macro still to 64); a user-defined
  index command of 32 characters or more wasn't a line command; a NUL,
  which the grammar and texi2any take as a blank, wasn't one to the
  scanner, so `@end` after one ended no block
- A paragraph's line end reads the first line of a conditional on the
  next line, as the conditional's own scan does (found by review, by
  chains of random edits): after one edit and then another on that first
  line, which made the conditional start with text, the paragraph was
  reused, ended before the conditional that now goes on with it
- A menu entry starts with the scanner's zero-width token, which it
  gives only where the innermost block other than a conditional is a menu
  (found by review): the grammar's `*`, which takes precedence over text,
  was lexed wherever a paragraph's line end could be followed by an entry
  in some context, so a line starting with `* ` in a definition's body was
  an error, and an edit before it made an incremental parse differ
  (+211 parser states: the scanner's token keeps the states of a line end
  an entry may follow from merging with others)
- Keep the preformatted context through blocks nested in `@example`
  and the other preformatted blocks (`@group`, `@quotation`, even `@menu`),
  where a blank line doesn't close a brace command, as in texi2any
- Paragraphs in `@example` and the other preformatted blocks are
  `preformatted` nodes, texi2any's preformatted text, so an editor's
  paragraph motions no longer take code for prose (found by review). The
  scanner starts one with a zero-width token where a paragraph would start
  in a preformatted context, but not after a newline that may go on with
  an open one, nor at a `{` in the middle of a line, the braces of the
  `@caption` before it, say: a `{` after `@anchor{...}` starts a
  `paragraph`, as before. A cell's last one has a rule of its own (43
  parser states)
- Command names as texi2any reads them, `[[:alnum:]][[:alnum:]-]*` of any
  length, so `@0` or a name longer than 32 characters is one (unknown)
  command, and `_` ends one: `@code_x` is `@code`, then `_x` (it continued
  one until the comparison of arguments showed `@c_x` is a comment)
- No table of the file's macros in the scanner: it made every token
  after a definition carry the table in its state, so adding or renaming a
  macro reparsed the rest of the file (76 ms for gawk's manual, against
  0.3 ms for a word typed), and parsing gawk's manual with 128 macros took
  61 MB, not 43 MB. Macro calls are now commands like any other
  (Deliberate, below); the parser lost 787 states
- Speed while editing, measured in gdb's manual (1.9 MB, 98 ms for a
  fresh parse), a quarter of the way in (found by review): a keystroke in
  a paragraph takes 0.2 ms; typing `@ifset` or `@quotation`, 1 ms, as a
  block left open ends at the next node; a keystroke in a raw block left
  open, 9 ms, as its body runs to the end of the file, as in texi2any, and
  typing its `@end`, 75 ms, a parse of what was raw text. A token for each
  line of the body was no faster (8 ms): tree-sitter reuses each line's,
  but no node of the repeat that holds them, a repeat's being fragile, so
  it shifts every line after the edit

## Commands and arguments

- Blocks and line commands at the start of a line inside brace commands
  such as `@footnote` and `@caption` (found by the texi2any comparison), and
  headings such as `@heading` (found by comparing arguments; +12 parser
  states)
- Paragraphs inside `@footnote`, `@caption` and `@shortcaption`, with
  blocks and line commands
- In `@itemize` and `@enumerate`, the text after `@item` starts the item's
  first paragraph (in tables it is the item's line argument); `@noindent`
  and `@indent` start a paragraph too, as in texi2any
- An unclosed `{` group ends at a blank line or its block's end, like a
  brace command, instead of breaking the rest of the file
- Split brace arguments on commas into fields named by position for
  `@xref`, `@pxref`, `@ref`, `@inforef`, `@link`, `@uref`, `@url`, `@email`,
  `@image`, `@abbr`, `@acronym`, `@namedanchor`
- `@node` arguments in fields (`node_line`: node, next, previous, up)
- Split the inline conditionals' arguments into `format` (`@inlinefmt`)
  or `variable` (`@inlineifset`, `@inlineifclear`) and `text` (multi-line,
  like a conditional block) or, for `@inlineraw`, `format` and a raw
  `body`, injected in the language its format names. The first was
  `condition` for all of them until review (100 parser states)
- `@inlinefmtifelse`'s three arguments: `format`, `then` and `else`,
  both texts spanning blank lines and blocks (+1,074 parser states)
- `@float` arguments: `type` and `label` fields; the label is a tag and
  highlighted like a node name. Commands with named arguments (`@xref`)
  aren't allowed in the type
- A block at the start of a line closes an ordinary brace command, as in
  texi2any (conditionals don't)
- So does a list's next item at the start of a line (`@item`, `@itemx`,
  `@headitem`, `@tab` in a list or multitable), which also ends the
  paragraph, as in texi2any; an `@item` line swallowed by an unclosed
  `@code{` in a table moved the rest of the item out of it. texi2any also
  closes the brace at one in the middle of a line, which the manual
  doesn't allow
- Accent commands without braces: `@'e`, `@"u`, and letter accents with
  a space: `@u o`, `@v c`; the character is in the command's `arguments`,
  a `brace_arguments` as with braces (its `argument`, a `text`, until
  review), as texi2any has either as the accent's argument
- Each field holds one kind of node since review: `argument` is always
  a `line_argument`, which holds the raw text of `@vskip`, `@clickstyle`,
  `@unmacro` and of what follows a macro's name or parameters, and the
  ignored text after `@bye` on its line, as texi2any's line argument does
  (they were the `argument` themselves); an accent's argument is above
- `@value{name}`: an inline command whose name is highlighted as a
  variable, like `@set`'s
- `@alias` has `alias` and `target` fields; `@macro`, `@rmacro` and
  `@linemacro` are `macro_definition` nodes, whose bodies the injection
  query highlights as Texinfo
- Tags for macros, aliases and `@set` variables, and `@value` as a
  reference
- `@math{...}` body for TeX injection
- Plain-text line arguments (`@include`, `@setfilename`, ...): texi2any
  parses commands in them too, so they stay a `line_argument`; file names
  are highlighted as paths
- `@set NAME VALUE` and `@clear NAME` split into `variable` and `value`
  fields. As in texi2any, the value is raw text up to a comment; its
  commands are parsed where `@value` uses it
- Form feeds and vertical tabs as spaces, as in texi2any, but that a
  form feed in the middle of a paragraph's line ends it (below)
- Spaces between a built-in brace command and its brace (`@code {a}`),
  which texi2any accepts. Not for other commands, which may be macros or
  commands without braces (`@tab {b}`); not across a newline, which
  texi2any accepts with a warning
- Definition lines: `@deffn`, `@deftypefn`, ..., their `x` variants and
  `@defline`/`@deftypeline` split into `category`, `class`, `type`, `entity`
  and `arguments` fields (braces group words)
- `@` at the end of a definition line's arguments is a `line_continuation`
  (still a `symbol_command` before the arguments start)
- A line ending in `@` right after a definition word, with the next
  line starting with no blank, continues the word, as in texi2any
  (`foo@` then `bar` is `foobar`); it was a space between two words. Its
  `@` is a `line_continuation` inside the `word`, and one before a word is
  in a `continuation` field, outside the word's (both were an anonymous
  `@\n` until review; outside any field, it costs 3,572 parser states)
- A definition word glued to a command (`foo@var{x}`,
  `@var{$converter}->method` in texi2any's API reference) is one `word` node,
  as in texi2any; the scanner emits a zero-width glue between its parts.
  Only built-in brace commands with their braces glue to what follows:
  after another command (`@xref{a}b`) the next part starts a new word
- Index entries with `@subentry` (a nested `subentry` node, as in
  texi2any), `@seeentry`, `@seealso`, `@sortas`
- `@columnfractions`: an inline command in the multitable's argument,
  with its own `argument`, as texi2any has it as a line command there
- Multitable rows: each `@item`/`@headitem` holds its first cell and a
  `tab` node per further cell, including rows inside conditionals; each
  cell's content is a `cell` node
- `@headings`, `@everyheading` and friends with `@|` separators: a line
  argument with `@|` symbol commands, as texi2any has it
- `@alias NEW = EXISTING`, `@definfoenclose`: plain line arguments, as in
  texi2any
- Missing line commands from texi2any's table: `defline`, `deftypeline`,
  `xrefname`, `clickstyle`, the two-letter index commands, ...
- User-defined index commands (`@defindex op` makes `@opindex`): any
  `@...index` at the start of a line is an index line command
- Missing blocks: `@documentinfo`, `@publication`
- Fix typo: `codequoteundirective` should be `codequoteundirected`
- An accent without braces at the end of a line command's line
  (`@chapter Caf@'`) has no argument, as in texi2any: it took the first
  character of the next line, and the heading the rest of that line (found
  by comparing arguments)
- No-brace commands: `@refill`, `@thischapter`, `@thisfile`, ... parse as
  inline commands without arguments, as in texi2any

## Structure

Deliberate divergences from texi2any, which tolerates these with warnings:
block commands only start a line (texi2any also accepts `text @html` and
`@tex in tex @end tex` on one line), and `@end` only ends a block at the start
of a line.

- An unclosed brace in a line command's argument (`@chapter A @code{x`)
  closes at the end of that line, as in texi2any, also in the commands with
  named arguments (`@xref{` in a heading, which had gone on to the next
  line) and in `@node` lines, where it had taken the section's heading and
  paired every later node with the heading after it (+103 parser states)
- A block right after `@footnote{` (`@footnote{@quotation`)

- `@itemx` after index entries or comments that follow an `@item`
  (make, texinfo; found with `compare-texi2any.py --structure`)
- `@item` inside a conditional block inside a list, e.g. `@ifclear X`
  wrapping an `@item` in a `@table` (sed, gawk)
- Menu entries in `@menu`, `@detailmenu` and `@direntry`: `menu_entry` with
  `label`, `node` and `description` fields; other text is a menu comment.
  An entry still being typed (`* Name` with no separator) has only a label
- A menu entry's description may start on the line after the entry, as
  in texi2any; a bare `*` there is description text, not an entry
- A menu node ending in a run of dots before whitespace keeps all but
  the last, as in texi2any: `* Label: foo... desc` is node `foo..` and
  description `desc`, not `foo` and `.. desc`; before `,` or a command it
  keeps them all
- Comment lines after a menu entry without a description start its
  description, as in texi2any, which goes on with the lines after them up
  to a blank line (found by review): they were menu comments, and so was
  the text after them (+4 parser states)
- A tab ends a menu node, as a comma does, as in the Texinfo manual and
  texi2any (found by review): `* Label: Node<TAB>Description` was all node
- Sections nest by level (`part`, `top`, `chapter`, `section`,
  `subsection`, `subsubsection`), each with its `node` line and `heading`.
  A section inside a conditional ends at its `@end`; the Top node in
  `@ifnottex` and alternative headings per output format therefore don't
  contain the content after the conditional
- `@anchor {old}`, with blanks before the brace, between `@node` and its
  section, as texi2any takes it: the node line still starts the section
- No node is associated with a part, as the Texinfo manual says and
  texi2any warns: a `@node` before `@part` was the part's. And as in
  texi2any, an appendix after a part's chapters ends the part, with the
  appendices after it, but those before its first other chapter are in it
  (found by review, in 22 of texi2any's test inputs, whose sections now all
  have texi2any's parents; +26 parser states: an appendix's node has a
  token of its own, as the part can't hold the appendix it starts).
  texi2any only moves the first appendix of the document out of a part,
  which a grammar can't count
- Line commands between `@node` and its section, such as index entries
  (50 in SBCL's manuals) and `@need` (glibc's): the node line still starts
  the section, as texi2any associates them (found by review; +60 parser
  states). Text between them still leaves the node line on its own, unlike
  in texi2any: the node would look ahead through all of its text
- `@majorheading`, `@chapheading`, `@heading`, `@subheading` and
  `@subsubheading` are `standalone_heading` nodes, not sections
- Every command node names its command field `name` (headings used
  `command`, `@bye` had none)
- Line commands and headings at end of file without a newline
- Blocks still open at the end of the file end with a zero-width
  `missing_end` node instead of turning into an error
- Unclosed brace commands end where texi2any ends them (a blank line for
  built-in commands other than `@footnote`, `@caption`, `@shortcaption` and
  `@math`; the enclosing block's `@end`; the end of the file), marked by a
  zero-width `missing_brace` node, so they don't break the rest of the file
- A `@node`, a sectioning command or `@bye` closes the blocks left open
  before it, but not a conditional, and an unclosed brace command and its
  paragraph, as in texi2any ("@node seen before @end menu"). A `@menu` or a
  list still being typed had taken the rest of the file, its chapters read
  as text, and an `@example` had held the chapters after it (found by
  review: the comparison doesn't see where a block ends without its `@end`)
- An unclosed brace command also ends at the `@end` of an enclosing block
  (`@code{` in an `@example` inside a `@quotation`, then `@end quotation`),
  which closes the blocks inside it, as in texi2any; the rest of the file
  stayed inside the brace
- A brace argument crossing a conditional's boundary (e.g. `@footnote{`
  inside `@ifnottex`, closed after its `@end`, or TeX braces in Calc's and
  Gnus's `@iftex` blocks) can't be represented: the brace closes at the
  conditional's end, and the later `}` is an `unmatched_brace` node, as is
  any `}` that closes nothing, which texi2any reports and ignores. It was a
  one-character parse error, the last in the manuals. Five manuals still
  hold such recovery nodes, all where texi2any doesn't parse: TeX braces in
  Calc's, Gnus's and gawk's `@iftex` blocks (gawk's `@center @i{` takes a
  second line, as TeX allows), and a macro call in `texinfo-tex-test.texi`
  whose braces hold a blank line
- TeX lines before `\input texinfo` (e.g. gawk's Italian manual): text,
  as in texi2any, which only recognises `\input` on the first line
- The `@macro` line has `macro` and `parameter` fields, as texi2any
  reads it, rather than an argument parsed for commands; anything else on it
  but a comment is raw text. Tags and highlighting use the fields
- `@raisesections` and `@lowersections` move the sections after them,
  as in texi2any (found by review: the Texinfo manual itself uses them):
  the scanner counts them in its state, and names each sectioning command
  but `@part` and `@top` with a token for its level, which the headings
  take instead of keywords; a `@node`'s token looks at the level too. Each
  section in the nine manuals that use them has texi2any's level now,
  checked against its HTML output. Editing one of them reparses the rest of
  the file (32 ms in the Texinfo manual), as it moves every section after
  it. texi2any also raises a section that skips a level (a subsection
  right in a chapter), which the tree doesn't: a chapter holds it

## Differences from texi2any

From comparing with texi2any on its own test inputs, whose differences are
all listed with their reasons in `test/texi2any-differences.txt`; the
manuals match.

Gaps, where we give a parse error and texi2any recovers:

- A block command on the last line without a newline (`@iftex` at the end
  of the file) is a block ending in `missing_end`
- An unclosed brace group in a line argument, such as a `@multitable`
  prototype (`@multitable { a`), closes at the end of the line
- Badly braced definition lines (`@deffn {cat name`) close their braces
  at the end of the line; a lone `@` at the end of the file is a symbol
- Comments after `@bye` and `@definfoenclose`: the tree was right; the
  comparison script skipped them
- Gaps found by listing every difference: blank lines inside braces in
  `@math` and inside brace commands in preformatted blocks; raw blocks
  other than `@verbatim` inside brace commands and cross-reference
  arguments; lone carriage returns in text; an empty menu entry name;
  `@footnote`, `@caption`, `@inlineraw`, the inline conditionals and
  `@verb` without braces
- A lone carriage return at the start or end of a text (`\rHello`,
  `Hello\r@code{x}`, `Hello\r` at the end of the file) is a blank, as
  texi2any takes it, and so is the `\r` of a `\r\n`: no token starts with
  one, or it would take a lone one before it (`\r@code`)
- A blank at the end of a file that leaves a block open
  (`@quotation`, a line `x`, then a last line of only a space) gave a
  `MISSING block_end` instead of `missing_end`
- An unclosed `@verb` takes the rest of the file, as in texi2any, and
  ends with a `missing_brace`: it was an error from the start of its
  paragraph to the end of the file, outside the sections before it (9 of
  texi2any's test inputs). A `@verb{` at the end of a line has no
  delimiter: as in texi2any, its raw text ends at the first `}`
- Parse errors on input that is being typed or that texi2any accepts:
  `\input texinfo` without a newline (a new file) or with text after it (a
  TeX `%` comment), text after `@bye` on its line, which texi2any ignores
  with a warning, and an empty node name (`* ::`), which was the node `:`.
  `@` before a character other than ASCII is a lone `@`, as in texi2any:
  the character was part of the symbol command (found by review)
- Commands after `@bye` on its line: its argument stopped at the first
  `@`, so `@bye @code` read `@c` as a comment and `@bye @item` was a parse
  error. It is now raw text up to a comment, as `@set`'s value is, and as
  texi2any has it (found by review). Such raw text may also end in an `@`,
  which was a line continuation and a parse error in `@set x a @` too: a
  token of its own, of lower precedence, so that error recovery elsewhere,
  which lexes every token, doesn't take a command's `@` for it (no parser
  states)

Gaps where the tree differs from texi2any's and the Texinfo manual, found by
comparing arguments:

- A raw block (`@ignore`, `@html`, ...) after a menu entry's description
  line, or right after the entry, is part of the description, which goes on
  after it: the Texinfo manual only ends one at an empty line or the next
  entry, as texi2any does (in gdb's, gas's and Semantic's manuals; 31
  parser states)
- A menu entry inside a conditional in a menu (`@ifset` around some of
  the entries) was text in a paragraph: 235 entries in 18 manuals (ld,
  gas, Emacs, gdb, ...). The comparison couldn't see it, as it compares
  texi2any's expansion, where the conditional is gone. The scanner now
  allows an entry in a block whose innermost enclosing block other than a
  conditional is a menu (45 parser states)
- Other blocks (`@example`, lists, definitions) are part of a menu
  entry's description too; the scanner gives a conditional its usual token
  there, which ends the description, as a conditional holds the next
  entries, which texi2any only sees after evaluating it (6 of texi2any's
  test inputs; the block rule's content, now a rule of its own, saved 158
  parser states)
- A menu entry may start after spaces; the Texinfo manual and texi2any
  only start one with `* ` at the beginning of a line
  (`leading_space_before_menu_star.texi`). The star is an immediate token
  now, which blanks before it rule out
- A comment line ended a paragraph, which texi2any goes on with after it
  (760 paragraphs in 156 of the manuals and test inputs; seen while
  comparing, as paragraphs aren't compared). Comment lines followed by a
  line that isn't blank now stay in the paragraph, or a multitable cell's
  (89 parser states)
- texi2any also keeps index entries and the other line commands that
  don't close a paragraph (`@include`, `@author`, `@set`, ...) inside one;
  here they ended it. Now 4,100 of them in the manuals are inside their
  paragraph, 731 paragraphs fewer (1,501 parser states, 813 of them for
  `@set`, `@clear`, `@alias` and the commands with a raw argument, which
  have rules of their own, for 4 in the manuals; the budget went to 14,200).
  Since review, those rules are one hidden rule that every context's line
  commands share, and a `@subentry` may follow `@alias`'s argument, as in
  texi2any and other line commands: 942 states fewer, budget 13,500
- Comment lines followed by a line that isn't blank stay in a menu
  entry's description too (4 manuals; 413 parser states)
- Comment lines inside a paragraph in braces (`@footnote`) too: the
  token before them is zero-width now, so the scan of a brace that the
  newline may close can return it as well. Comment lines before a line
  holding only a command (`@item`) had been taken for ones before a blank
  line (53 files)
- Paragraphs start and end where texi2any's do, found by comparing
  them: a command that starts no paragraph in texi2any (`@anchor`,
  `@noindent`, `@titlefont`, ...: NO_PARAGRAPH_COMMANDS in the scanner) is
  outside one where one would start, also before the text on its line, and
  inside an open one (12,910 of the manuals' 16,623 differences in
  paragraphs, most of them an `@anchor` line before a definition or an
  item; 90 parser states); a raw block but `@verbatim` (`@tex`, `@html`,
  `@ignore`) doesn't end a paragraph (1,611 more, most in Maxima's
  manuals; 53 parser states), nor do comment lines after one (ld's and
  Maxima's manuals; 43 parser states)
- `@caption`, `@shortcaption` and `@titlefont` (the brace commands in
  texi2any's close_paragraph_commands) end a paragraph, also in the middle
  of a line, with a zero-width token before them, and start none; `@*`
  starts none; and in a footnote or caption, neither does a command that
  starts none (`@anchor`), at the start of a line (54 parser states for the
  captions, 18 for the footnotes, as alternatives of `_line_item`: in the
  footnote's own rule, they cost 176)
- `@image` and `@namedanchor` start no paragraph either (29 files, 5 of
  them manuals). As keywords, they get a zero-width token from the scanner
  before them where content starts, so that they are read one way only,
  and are followed by their line's end, the paragraph the rest of the line
  starts or another of them, the same everywhere: 181 parser states,
  against 1,100 for the keyword as an alternative where content starts,
  whose arguments then had states for each context's followers; the
  budget went to 14,300
- Nor does `@image` inside a footnote's or caption's braces, at the
  start of a line (2 of texi2any's test inputs), also where the `}` follows
  on its line (`@caption{@image{x}}`): an item of the footnote's text of
  its own, followed by whatever the text allows there, such as a paragraph
  or the `}` (43 parser states). It was first among the line items that
  brace commands' lines share, followed by its line's end or a paragraph,
  which put a paragraph inside `@code{` where an image started one of its
  lines; the scanner looked for a `}` on the image's line to avoid it,
  which only hid it where the `}` was on that line
- Macro definitions don't end a paragraph either, as in texi2any (6
  files; 88 parser states, the definition's own in a paragraph)
- Nor do conditionals, which texi2any evaluates, so that the text of
  the branch it takes goes on with the paragraph around it: a conditional
  whose first line is text is a block inside the paragraph, at the start of
  one of its lines; one that starts with items, a section or a block stays
  part of the structure around the paragraph (60 parser states). The
  comparison can't see it, as texi2any's expansion has no conditionals
- Text after a raw block's `@end` on that line, in a paragraph, is one
  text in the paragraph (texi2any reports it); it was an error that could
  reach far, found by the editing simulation; after a conditional's or a
  macro definition's too (102 parser states)
- Comment lines at the end of a paragraph, before a blank line or the
  end of the file, are in it, as in texi2any; they were after it (61
  files). In a paragraph, comment lines are no longer extras between
  counted newlines, which would have taken the blank line's newline too,
  but explicit, each with its line end (63 parser states)
- And preformatted text: in `@example` and the other preformatted
  blocks and menus, each stretch between the blocks, items and cells in it
  and the commands that end a paragraph (texi2any's
  close_preformatted_commands), as texi2any holds it, and its text, in
  about 22,000 stretches. It found nothing to fix in the grammar: its
  differences come from those already listed, and an example of a literal
  `\n`, which texi2any's dump writes as a newline
- The comparison compares paragraphs too: where they start and end
  among the commands, and their text outside commands. 165,618 of 165,620
  paragraphs match texi2any's; the other differences are listed, by the
  gaps above or as deliberate (a `#line` directive is text). Where
  texi2any's paragraphs
  differ for want of what another file defines (a line of commands it
  doesn't know is empty to it, an `@value` of a variable not set starts no
  paragraph), our side splits them as texi2any does or the difference is
  ignored

Deliberate, so the tree shows the source as it is written and edited:

- A block left open before a conditional whose first line is `@node Top`
  runs past the node into the conditional, where texi2any, which has
  evaluated the conditional, closes it at the node (`gawkinet.texi` with
  its `@end direntry` deleted). The scanner can't decide it at the
  conditional: a token's end can't move back over the line it read to
  decide, between a zero-width end of the block and the conditional's
  name; a zero-width token before every conditional in a block would,
  for about 200 parser states, as the menu entry's did, for a mistake
  being typed
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
  the multitable's columns, `@tab` before the first `@item`, a recursive
  `@insertcopying`. Marked are an `@end` that ends no block
  (`unmatched_end`) and, since review, an `@item`, `@itemx`, `@headitem`
  or `@tab` where it means nothing, as texi2any decides it (`@item` outside
  a list, `@itemx` outside a table, `@headitem` and `@tab` outside a
  multitable): a `misplaced_command` node, an extra, which editors should
  report. It was an `inline_command`, or an `item` in a `@quotation`, which
  no editor could tell from one that's right. texi2any ends the paragraph
  at it, and keeps an `@itemx` as a line command; the extra leaves the
  paragraph whole, so that typing one changes nothing around it, and costs
  no parser states. The binutils fragments for `@include` inside a table no
  longer differ, as texi2any drops their `@item` too. The scanner tells raw text
  (`@set`'s value, what follows `@bye`) from where a command may be by the
  external tokens valid there, none but the end of the file: a test of a
  brace command's name being valid chose differently after an incremental
  parse reused the paragraph before the command, where only an item's or
  the list's end is (found by fuzzing texi2any's test inputs)
- Before a list's first item, an `@itemx` in a table or a `@tab` in a
  multitable means nothing too, as texi2any reports ("@itemx should not
  begin @table", "@tab before @item"), but was an `inline_command`, which
  no editor reports (found by review). Only right after the list's head,
  past blank and comment lines, is it a `misplaced_command`: further on, a
  macro call may have made an item, as coreutils' `@optItem` makes its
  `@item`, and an `@itemx` after it is right. Telling the item before it by
  the `@itemx`'s token being valid, a token of the scanner's in place of
  its keyword, reported every one in coreutils' tables. The grammar takes
  the command in the head, after its line end (9 parser states; after the
  head, where every token that may follow it gave its states copies, 292),
  and the line ends of the blank and comment lines before it by a token
  the scanner gives them where such a command follows
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

- `queries/highlights.scm`, with tests in `test/highlight/`
- Highlight gaps from overlapping patterns: `@example` contents, markup
  in heading titles, `@defvar` entities, `@emph`/`@var` arguments
- The head of each block, list, menu, raw block, macro and command with
  arguments is a hidden rule of its own, ending in its line's end, and so
  are a line command's argument and a definition line's words (found by
  review): 7,775 parser states became 5,447, `parser.c` 15.6 MB 11.6 MB, with
  every tree the same
- Text and headings between a node and its section: the node line still
  starts the section, as texi2any associates them (+54 parser states). Its
  look ahead stops at a block, an `@end`, an item or a definition line, as
  skipping a block would mean matching its `@end`; the longest one in the
  manuals is 45 lines, through which a keystroke takes 0.16 ms
- Names for queries (found by review): the node of `@heading` and the
  like is `standalone_heading`, as sections have a `heading` field holding
  others; an argument separated by commas is a `comma_argument`, as the
  `argument` field holds a `line_argument`; a float's target is its
  `anchor`, as `@namedanchor`'s is, not its `label`, the text a link shows
  elsewhere; `_section` is a supertype (`(_section)`); a multitable cell's
  content, the first one's too, is a `cell` (2 parser states fewer)
- An `@` that starts no command, a symbol or an accent (`@#`, or before
  a character other than ASCII) is an `unexpected_at` node, with the
  character, an extra like `unmatched_brace`, for editors to report, as
  texi2any reports it; it was a lone `@` and text. No parser state more
- `@defblock`: the text after each `@defline` or `@deftypeline` is its
  `body`, a `definition_body`, as texi2any's `def_item` (the text before the
  first is the block's own), and a function's inside for the textobjects
  (+19 parser states). The two are no longer content anywhere else, as in
  texi2any, which allows them only in `@defblock`
- A stray `@deffnx` (outside a definition, or after its body's text)
  stays a `definition_line`: texi2any, which reports it, makes it a
  definition line too
- A command after a macro's name (`@macro aaa @node X`) is raw text, as
  in texi2any: it was an error that lost the whole definition, which also
  made one chain of random edits differ from a fresh parse
- Highlights for table item terms (`@markup.strong`), the text of index
  entries and subentries (`@string.special`), and the flags of `@ifset`,
  `@ifclear` and the inline conditionals (`@variable`, as for `@set` and
  `@value`); the text a link shows is `@markup.link.text`, Helix's name,
  which Neovim, without one, gets as `@markup.link`. Neovim gets the
  `entry` textobjects too, which its keymaps take though
  nvim-treesitter-textobjects doesn't name them
- Spell checking in Neovim, which checks only what highlights capture as
  `@spell` (found by review: it checked nothing): paragraphs, comments,
  headings and menu descriptions, but not command names, symbols, raw
  text, definition lines, code arguments or preformatted blocks
  (`@nospell`, which takes priority). Neovim's alone
  (`editors/neovim/spell.scm`): `(command_name) @nospell` would overlap
  the patterns of `queries/highlights.scm`; tested by
  `test/queries/neovim-highlights/`
- Highlights found by review: `@ignore`'s body is a comment, not raw
  text; `@display` and `@format` keep the font of the text, as the Texinfo
  manual says, not highlighted as code; a definition word that is a symbol
  command (`@deffn Operator @@`) is only an escape, as the patterns of a
  definition's words no longer overlap it. Tested by
  `script/test-queries.py` (`test/queries/highlights/`), which sees a
  raw block's body and every capture, as `tree-sitter test` checks only
  the innermost highlight
- `queries/injections.scm`: TeX/LaTeX, HTML, XML, DocBook raw blocks
- Inject LaTeX into `@math` and `@displaymath`
- Inject Emacs Lisp into `@lisp` and `@smalllisp`: 1,152 of the
  manuals' 1,185 `@lisp` blocks are in Emacs's manuals (SBCL's 31 are
  Common Lisp, which no query can tell apart)
- `queries/folds.scm`
- `queries/tags.scm`: nodes, anchors and documented functions and variables
  as definitions; cross-references and menu entries as references
- Tag `@anchor {target}` and `@value {var}`, with blanks before the
  brace (as in `info-stnd.texi`)
- No reference tag for a cross-reference to another manual (with a
  file or manual argument) or a menu entry's `(manual)node`, which named a
  local node of the same name (found by review). Their tests are in
  `test/queries/tags/`, as tree-sitter 0.26.11's tags tests read `!NAME`
  as `NAME`
- Register queries in `tree-sitter.json` (highlights, injections and
  tags: it has no key for folds and textobjects)
- Queries for several editors, favouring none: `queries/` uses the
  plainer capture name where editors differ (`.tsqueryrc.json`
  enforces them), and `editors/helix/queries/` and `editors/neovim/queries/`
  are generated from them with each editor's names, checked against its
  lists. Textobjects too. No indent queries, deliberately: Texinfo isn't
  indented by nesting, and editors keep the previous line's indentation
  without them
- Textobjects: a definition's `@function.inside` included its `@deffnx`
  lines, and a table item's `@entry.inside` its `@itemx` lines, siblings of
  the body that a query can't start after. Now the body is a node, as in
  texi2any (`def_item`, `table_definition`): a `definition_body` or an
  `item_body`, in the `body` field, after the last `@deffnx` or `@itemx`
  line and the index entries, blank lines and blocks (a conditional holding
  an index entry) before it (+164 parser states, no slower)
- Tests of the folds, textobjects and injections
  (`script/test-queries.py`, on `test/queries/`): `tree-sitter test` reads
  each test file of these queries as the query itself (0.26.11)
- Textobjects: `.` skips anonymous nodes, so in `@xref{a, b, , d}` the
  empty argument's comma joined `b`'s `@parameter.around`. A comma is only
  taken between arguments in consecutive fields now, which the query lists;
  an argument followed by an empty one is alone, like the last, as no query
  can tell which comma is its own
- The lists of command names that keep the highlights' and the tags'
  patterns apart (`#any-of?` and its complement, `#not-any-of?`) were kept
  in step by hand (found by review). `script/test-queries.py` now checks
  that no two patterns capture a node, and that every command name and
  symbol command is highlighted and every definition line tagged, in a
  document with every command it builds from the scanner's tables, and in
  the highlight and tags tests

## Project

- `script/compare-texi2any.py`: compare trees with texi2any's
- Run the comparison on the inputs of texi2any's own parser tests
  (`texinfo-tests/`): 1,446 of 1,755 matched exactly then
- Triage the test inputs that differ: 1,579 of 1,755 then matched exactly.
  The remaining 176 are the macro and value suites (the script's simple
  macro expansion), invalid input that texi2any discards (unknown commands,
  misplaced `@tab` and `@headitem`, stray `@end`), `@click` (texi2any
  substitutes the `@clickstyle` symbol), and the divergences below
- Mirror `@clickstyle`/`@click` and `@alias` in the comparison
- Fuzz the manuals too (`tree-sitter fuzz`, random edits anywhere), not
  only texi2any's test inputs
- CI time: no CI for Markdown-only changes; `nix flake check` runs the
  same checks locally before pushing, and CI keeps the Nix store between
  runs, so only the checks whose inputs changed run again. The bindings on
  Linux, macOS and Windows on every push
- Each check's inputs are only the files it reads, so that a change to
  a query or a snapshot doesn't rerun the comparisons and the fuzzing; the
  fuzzing checks start from a fixed seed, as Nix caches their result; each
  check's command and the extraction of the manuals (now
  `script/extract-examples.py`, from a manifest in `flake.nix`, the same
  687 files) were bash that shellcheck never saw (found by review)
- Standard tools instead of shell scripts: `nix flake check` runs every
  check (cached by Nix), `nix fmt` formats (treefmt-nix), pre-commit runs
  the git hooks (git-hooks.nix), all declared with flake-parts; the justfile only holds
  one-line aliases
- Longer fuzzing locally: `just fuzz-deep` (`tree-sitter fuzz`) and
  `just fuzz-scanner` (libFuzzer with sanitizers; LLVM is 23, as LLVM 21's AddressSanitizer deadlocks at startup on macOS 27)
- Test the checks themselves (`script/test-checks.py`): each must fail
  on inputs in `test/checks/` that it is meant to catch
- Compare our tree of texi2any's own expansion of each file
  (`--macro-expand`) rather than imitate the expansion: 300 fewer lines in
  the script, and the differences it caused in the macro suites are gone
- CI (`.github/workflows/ci.yml`): generated `src/` up to date, tests,
  fuzzing, parsing the manuals, and both texi2any comparisons, which must
  find exactly the differences listed in `test/texi2any-differences.txt`
- README: supported features, editor setup, development with Nix
- `CLAUDE.md` with project conventions
- Expand `@linemacro` calls in the texi2any comparison
- In `--structure` mode, texi2any's line commands come from its own
  command table, not from what our parser produced, so a line command we
  don't parse as one shows up
- More example manuals: Emacs's manuals, glibc, gdb, binutils, bash,
  coreutils, autoconf, automake
- Manuals by other authors, found with nix-locate: SBCL (with ASDF),
  Maxima (English and Japanese), GNU Parallel, Stow, LaTeX2e. They showed
  that a comma-separated argument (`@xref{`) didn't take a conditional
  block, so a brace left open before one broke the enclosing block; and
  `.txi` files are checked too
- Reduce the parser's size: `src/parser.c` was 29 MB. The line and brace
  command keywords, now recognised by the scanner, made each parser state
  where a command can start carry about 200 entries; it went to 6 MB, with
  the same tree. Features added since took it back to 24 MB (13,748
  states), each with its cost measured against the budget; until the
  comma-separated arguments, whose empty ones the nested optionals they
  were written as expanded into productions for each combination, became a
  chain of hidden rules, one per position, as did a definition line's
  words: 14 MB, 7,139 states, the same trees, and generation in a second
  instead of ten. Other measures of the kind: macro parameters as a hidden
  rule of their own, 979 states became 166; a `line_argument` after
  `@alias` that no `@subentry` could follow cost 384, as a rule has states
  for each set of tokens that may follow it; a change in nested optional
  sequences once made generation run for over 13 minutes
- Tree snapshots of every manual and test input
  (`script/tree-snapshots.py`), and a simulation of typing mistakes in the
  manuals that checks error locality and incremental parsing
  (`script/check-edits.py`), both in CI
- A long line parsed in time quadratic in its length (a line of 8,000
  `@code{x}` took 4.6 s, 35 ms now): the scanner asked for the column before
  every token, which tree-sitter counts from the start of the line. It asks
  only where the answer decides the token now, and the parse check times
  generated long lines too (`--stress`), as no manual has a line over a
  kilobyte (found by review)
- Parse errors and parse speed of the manuals checked
  (`script/parse-examples.py`, timing each parse's CPU time); texi2any's
  test inputs and the manuals fuzzed with `tree-sitter fuzz`, from a corpus
  generated by the check
- The generated Neovim queries run in Neovim, by a check: its regexes
  are Vim's, which no other check reads, and every pattern starting `^@`
  had made Neovim reject the highlights
- The comparison reads texi2any's command names whole, as texi2any does:
  `@foo-barindex` (after `@defindex foo-bar`) was `@foo`, a false difference
- Python source distributions include `src/scanner.c`: `setup.py`, as
  tree-sitter 0.26's template writes it, added it only while compiling, so a
  wheel built from the sdist failed to import
- A check that the scanner's token enum and its tables of layouts and
  section levels agree with the grammar, and that its tables of names don't
  overlap (`script/check-scanner.py`), so the order in which the scanner
  tries them doesn't matter; and since review, that its names are texi2any's,
  in texi2any's classes (line, paragraph-ending, brace commands, the kinds
  of blocks, the preformatted ones and the conditionals), and that every
  sectioning command in the grammar is in its table. It found two
  obsolete commands among the line commands
  (`@setcontentsaftertitlepage`), which texi2any no longer knows
- A budget for the parser's state count (`script/parser-size.py`, a
  flake check), raised only on purpose: features added 1-8% each, which
  the check against the previous push didn't flag, until the parser had
  twice the states it had once the scanner recognised the command names
- The comparison checks each command's arguments too, field by field
  where the grammar splits them as texi2any does (cross-references, `@node`,
  `@float`, definition lines, menu entries) and as one elsewhere, by their
  letters and digits; menu entries are compared as commands. It found three
  bugs: `@c|` wasn't a comment, a heading wasn't one inside `@footnote`,
  and an accent at the end of a heading took the next line's first
  character; and the gaps above
- The corpus tests' inputs are compared with texi2any too
  (`--corpus-inputs`, in the `compare-tests` check), as their trees are
  written by hand: six differ, each listed with its reason. It found that
  `@deffn Command foo@` and then `bar` names `foobar` in texi2any, which
  a test had as two words, and that texi2any takes the text after `@bye`
  on its line as its argument (now the `argument` field of `bye`)
- The comparison counts where a block left open ends, as where
  texi2any closes one at a `@node`: it couldn't see a block that went on
  past it (no new difference: the grammar closes them as texi2any does
  now, but in the middle of a line). Letters of any script count in
  arguments, not only ASCII ones (no new difference either, in Maxima's
  Japanese manual among others)
- texi2any and the manuals pinned apart from the tools (the `reference`
  input in `flake.nix`, a fixed nixpkgs revision): `nix flake update` for a
  newer tree-sitter or LLVM no longer brings a new texi2any and new manuals
  with it, which change many listed differences and snapshots at once
- Not published to npm, crates.io or PyPI, by choice: the names are left to
  a possibly more complete implementation. Editors build the grammar from
  the repository; releases, if any, are git tags
