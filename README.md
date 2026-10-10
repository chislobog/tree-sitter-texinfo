# tree-sitter-texinfo

A [tree-sitter](https://tree-sitter.github.io/) grammar for
[GNU Texinfo](https://www.gnu.org/software/texinfo/), for syntax highlighting
and structural editing.

The reference for correct behaviour is `texi2any` from Texinfo 7.3. The
grammar is checked against it on over 400 files of real manuals (texinfo,
make, sed, gawk, glibc, gdb, binutils, bash, coreutils, autoconf, automake,
and Emacs's manuals, Gnus and Calc among them) and on the 1,755 inputs of
`texi2any`'s own parser tests. Every difference in the commands found is
listed, with its reason, in `test/texi2any-differences.txt`: in the manuals,
6 of 524,778 commands, in fragments meant for `@include`; in the test inputs,
mostly deliberately invalid input and macro expansion.

## What is parsed

- Document structure: `\input texinfo`, sections nested by level (`top`,
  `part`, `chapter`, `section`, `subsection`, `subsubsection`), each with its
  `@node` line (with `node`, `next`, `previous` and `up` fields) and heading;
  heading-only commands such as `@heading`; `@bye`.
- Paragraphs, comments, line commands, brace commands and symbol commands.
- Blocks, matched by name (`@example` … `@end example`), nested to any depth,
  including conditionals.
- Raw blocks (`@verbatim`, `@ignore`, `@tex`, `@html`, …) and
  `@verb{xTEXTx}`, whose bodies are plain text, and `macro_definition`s
  (`@macro`, `@rmacro`, `@linemacro`), whose bodies are kept raw too and
  highlighted as Texinfo by injection.
- `@set` with `variable` and `value`, `@alias` with `alias` and `target`.
- Lists and tables with `@item`, `@itemx`, `@headitem`.
- Menus: `menu_entry` with `label`, `node` and `description` fields.
- Definitions: `@deffn` and friends, with `category`, `class`, `type`,
  `entity` (the defined name) and `arguments` fields.
- Arguments of cross-references and other multi-argument commands, in fields
  named by position: `@xref{Top, , Title}` gives `node` and `title` fields.

```
@deftypefn {Library Function} int foo (int @var{n})
```

```
(definition_line
  name: (command_name)
  category: (brace_group (text))
  type: (text)
  entity: (text)
  arguments: (definition_arguments (text) (inline_command …) (text)))
```

### Differences from texi2any

The tree shows the source as written, as an editor needs it: conditionals,
macros and `@value` are not evaluated, and `@include` is not followed.

Errors are kept local, so a half-typed command doesn't break the rest of the
document. An unclosed brace command ends where `texi2any` ends it (a blank
line, the `@end` of the enclosing block, or the end of the file), marked by a
zero-width `missing_brace` node; a block still open at the end of the file
ends with a zero-width `missing_end` node. An `@end` of an enclosing block
closes the blocks inside it with `missing_end`, as in `texi2any`; one of a
block that isn't open closes the open blocks (but not conditionals), and is
then an `unmatched_end` node, which `texi2any` reports and ignores. Editors that
report tree-sitter errors should also report these three nodes.

A section inside a conditional ends at the conditional's `@end`, so when only
the heading is conditional (as for the Top node, often inside `@ifnottex`), the
content after the conditional belongs to the enclosing level.

Block commands only start, and `@end` only ends a block, at the beginning of a
line; `texi2any` tolerates them elsewhere with a warning. `TODO.md` lists what
is not done yet.

## Queries

- `queries/highlights.scm`: headings, nodes and links, definitions, blocks,
  lists, commands and inline markup, using the common `@markup.*`,
  `@keyword` and `@function` capture names.
- `queries/injections.scm`: LaTeX in `@tex`, `@latex`, `@math` and
  `@displaymath`, HTML in `@html`, XML in `@xml` and `@docbook`, the
  language its format names in `@inlineraw{html, ...}`, and Texinfo in the
  bodies of `@macro` definitions.
- `queries/folds.scm`: sections, blocks, lists, items, menus and macro
  definitions.
- `queries/tags.scm`: nodes, anchors, float labels, documented functions and
  variables, macros, aliases and `@set` variables as definitions;
  cross-references, menu entries and `@value` as references.

## Using it in an editor

Neovim, with the `main` branch of
[nvim-treesitter](https://github.com/nvim-treesitter/nvim-treesitter):

```lua
vim.api.nvim_create_autocmd("User", {
  pattern = "TSUpdate",
  callback = function()
    require("nvim-treesitter.parsers").texinfo = {
      install_info = {
        url = "https://github.com/chislobog/tree-sitter-texinfo",
        queries = "queries",
      },
    }
  end,
})
vim.treesitter.language.register("texinfo", "texinfo")
```

Helix, in `languages.toml`:

```toml
[[language]]
name = "texinfo"
scope = "source.texinfo"
file-types = ["texi", "txi", "texinfo"]
comment-token = "@c"

[[grammar]]
name = "texinfo"
source = { git = "https://github.com/chislobog/tree-sitter-texinfo", rev = "main" }
```

Then copy the files in `queries/` to Helix's `runtime/queries/texinfo/`.

## Development

The Nix flake provides everything: tree-sitter, a C compiler and clangd,
Node, Python with py-tree-sitter, and `texi2any`. With direnv the shell loads
automatically; otherwise run `nix develop`. It also links the example manuals
as `examples/` and `texi2any`'s test inputs as `texinfo-tests/`.

- `grammar.js` is the grammar; `src/scanner.c` is the external scanner
  (block matching, raw text, closing unclosed constructs); `src/` is otherwise
  generated by `tree-sitter generate` and committed.
- `test/corpus/` holds the parse tests, `test/highlight/` and `test/tags/` the
  query tests.
- `script/compare-texi2any.py` compares the commands in our trees with
  `texi2any`'s.

```sh
tree-sitter generate && tree-sitter test
tree-sitter fuzz --iterations 100
tree-sitter parse --quiet --stat 'examples/**/*.texi' 'examples/**/*.texinfo'
find -L examples -name '*.texi*' | sort | xargs python3 script/compare-texi2any.py
```

CI runs these on every push. `CLAUDE.md` describes the conventions.

## License

MIT
