# Changelog

The version follows [semantic versioning](https://semver.org/) as the
tree-sitter documentation describes it for grammars: while it is 0.y.z, a
change to node types or the tree's structure bumps the minor version.

## Unreleased

- `queries/` no longer follows Helix's capture names but, where editors
  differ, uses the plainer name: `@markup.strong`, `@string.escape` and
  `@keyword.conditional` instead of `@markup.bold`,
  `@constant.character.escape` and `@keyword.control.conditional`. Helix's
  queries, with its names, are now in `editors/helix/queries/`.

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
