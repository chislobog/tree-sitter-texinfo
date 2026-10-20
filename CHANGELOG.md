# Changelog

The version follows [semantic versioning](https://semver.org/) as the
tree-sitter documentation describes it for grammars: while it is 0.y.z, a
change to node types or the tree's structure bumps the minor version.

## Unreleased

### Breaking changes

- An `@item`, `@itemx`, `@headitem` or `@tab` where it means nothing
  (outside a list, `@itemx` in `@itemize`, `@tab` in `@table`, ...) is a
  `misplaced_command` node, which editors should report, as texi2any does:
  it was an `inline_command`, or an `item` in `@quotation` and the like.

## 0.3.0

Fixes found by a review of the whole project, spell checking in Neovim,
and a parser with 5,490 states instead of 7,400 (`parser.c` is 11.4 MB, not
14.5 MB). The tree changes shape in places (a part's node, appendices
after a part's chapters, a section's node, preformatted text, raised and
lowered sections), which bumps the minor version.

### Breaking changes

- Names for queries: the node of `@heading`, `@subheading`, `@majorheading`
  and `@chapheading` is `standalone_heading` (`heading` was also the field
  of a section's heading); an argument separated by commas is a
  `comma_argument` (`argument` was also the field holding a
  `line_argument`); a float's target is in its `anchor` field, as
  `@namedanchor`'s, not `label`; each multitable cell's content is a
  `cell` node; `_section` is a supertype.
- In `@defblock`, the text after each `@defline` is its `body`, a
  `definition_body`, and `@defline` is no longer content elsewhere.
- An `@` that starts no command (`@#`) is an `unexpected_at` node.
- Paragraphs in `@example` and the other preformatted blocks are
  `preformatted` nodes, texi2any's preformatted text, so that an editor's
  paragraph motions and spell checking can tell code from prose.

### The tree

- No node is associated with a part, as the Texinfo manual says, and an
  appendix after a part's chapters ends the part, with the appendices
  after it, as in texi2any.
- `@raisesections` and `@lowersections` move the sections after them, as
  in texi2any, in the Texinfo manual itself among nine manuals that use
  them.
- A node starts the section after it past index entries and other line
  commands, headings and text, as in texi2any (52 sections in the manuals
  were left without their node).
- A form feed in the middle of a paragraph's line ends the paragraph, as
  the Texinfo manual says and texi2any does.
- Only texi2any's symbol commands are symbol commands: an `@` before any
  other character (`@#`) is an `unexpected_at` node, which editors should
  report, as texi2any does.
- In menus: a tab ends a node name; comment lines after an entry without
  a description start its description; and a line starting with `* ` in
  a definition's body is text, no longer an error.
- An accent at the end of a line no longer makes a comment line after it
  end its paragraph; a bare comment after `@end` in a paragraph and a
  comment right after a definition word are comments again.
- Edge cases: a `@verb` delimiter that isn't UTF-8, more than 64 nested
  `@ignore` blocks, a user-defined index command of 32 characters or
  more, and a NUL before `@end`.

### Editing

- A node with a node description block left open after it no longer
  makes each keystroke parse to the end of the file, nor a file of many
  such nodes parse in quadratic time.
- An incremental parse no longer differs from a fresh one after edits on
  a conditional's first line after a paragraph, or before an accent at
  the end of a line.

### Queries

- Emacs Lisp is injected into `@lisp` and `@smalllisp`, XML into
  `@inlineraw{docbook, ...}`.
- `@ignore`'s body is highlighted as a comment; `@display` and `@format`
  no longer as code.
- Tags no longer reference a local node for a cross-reference or menu
  entry to another manual.
- Neovim checks the spelling of the prose, but not of the code in it
  (`editors/neovim/spell.scm`): it checked nothing once it highlighted a
  buffer with tree-sitter.
- The README's Neovim setup starts highlighting and folding, and Helix's
  pins the grammar to a commit, with the queries of the same checkout.

### Bindings

- An editable install of the Python package has its queries; Node.js no
  longer warns of a deprecated entry point. CI builds the C library and
  tests the Python package from its sdist.

## 0.2.0

Paragraphs where texi2any has them, each field with one kind of node, and
a parser with 7,400 states instead of 12,400 (`parser.c` is 14.5 MB, not
21 MB), which generates in a second.

### Breaking changes

- `queries/` no longer follows Helix's capture names but, where editors
  differ, uses the plainer name: `@markup.strong`, `@string.escape` and
  `@keyword.conditional` instead of `@markup.bold`,
  `@constant.character.escape` and `@keyword.control.conditional`. Helix
  users should take `editors/helix/queries/`, which has Helix's names.
- A macro call is a command like any other: the scanner no longer
  remembers the macros the file defines, so adding or renaming one no
  longer reparses the rest of the file. A call without braces no longer
  takes the rest of its line as its `argument`, a `@linemacro` call is no
  longer a `line_command`, and a call's braces end at a blank line.
- The content of a table item after its `@itemx` lines, of an itemize or
  enumerate item, and of a definition after its `@deffnx` lines is in a
  `body` field, an `item_body` or a `definition_body`, as in texi2any, so
  that a query can take the body without the x lines.
- Each field holds one kind of node. `argument` is always a
  `line_argument`, which holds a `raw_text` or an `ignored` where the
  field was one; an accent's argument without braces (`@'e`) is in its
  `arguments`, a `brace_arguments`, as with braces; the inline
  conditionals' first field is `format` or `variable`, not `condition`;
  a definition line's `@` at the end of a line is a `line_continuation`,
  in the word it joins or in a `continuation` field.

### The tree

- Paragraphs start and end where texi2any's do, which the comparison with
  texi2any now checks, with the text of each: index entries, `@set` and
  the other line commands that don't end a paragraph stay in it, as do
  comment lines in and at the end of it, macro definitions, conditionals
  of text, and raw blocks but `@verbatim`. `@caption`, `@shortcaption`
  and `@titlefont` end one in the middle of a line, and `@anchor`,
  `@image`, `@namedanchor`, `@*` and the like start none.
- A `}` that closes nothing is an `unmatched_brace` node, not an error, so
  every manual checked parses without one.
- A node, a section or `@bye` closes the blocks left open before it, and
  an unclosed brace closes at the end of a node line, before a section on
  the next line and at a list's next item, as in texi2any.
- Command names are read as texi2any reads them (from a digit, of any
  length, and ending at `_`), and `@c` is a comment whatever character
  follows it.
- Menu entries start only with a `*` at the start of a line, may be inside
  a conditional in a menu, and keep a node's trailing dots but the one
  ending it; a heading may be inside a footnote or caption; a definition
  word is joined across a line ending in `@`; an unclosed `@verb` and
  small errors being typed no longer reach past their paragraph.

### Queries

- Highlighting of link texts, file names and alias names; Helix's tags
  use the kinds its symbol pickers know; the highlights work in Neovim
  and while typing; the textobjects pair an empty argument's comma
  correctly. Folds, textobjects and injections have tests now.

### Bindings

- The Go and Python bindings require runtimes that load ABI 15
  (go-tree-sitter and py-tree-sitter 0.25); the Python source
  distribution includes the scanner; each binding's test parses a
  document.

## 0.1.0

The first release: a grammar for GNU Texinfo, for structural editing and
highlighting. The tree shows the source as written (macros, `@value` and
conditionals stay unexpanded) and copes with input being typed. texi2any
(Texinfo 7.3) is the reference, and the Texinfo manual where texi2any's
error recovery accepts what the manual forbids.

- Every command in texi2any's command table: sections and nodes, blocks
  (with `@end` matching the open block), lists, tables and multitables,
  definitions and their glued words, menus, raw blocks, macros (with their
  name and parameters, and calls without braces), `@verb`, inline
  conditionals, cross-references and the other commands whose arguments
  split at commas into named fields.
- Error recovery that keeps a mistake local, as in texi2any: an unclosed
  brace ends at a blank line or the end of its block, an unclosed block at
  the end of the file, and an `@end` that ends no block is marked.
- Queries for highlighting, injections (TeX, LaTeX, HTML, XML, Texinfo in
  macro bodies), folds, tags and textobjects, with Helix's capture names in
  `queries/` and Neovim's in `editors/neovim/queries/`.
- Checked against texi2any on about 690 files of real manuals and its 1,755
  test inputs, with every remaining difference listed with its reason, and
  by snapshots, fuzzing and a simulation of typing mistakes.
