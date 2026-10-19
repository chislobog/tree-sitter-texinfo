; Spell checking, for Neovim: in a buffer it highlights with tree-sitter, it
; checks only what is captured as @spell, and @nospell, which takes priority,
; leaves out what is in it. So the prose, but not the code in it.
; script/editor-queries.py appends these patterns to Neovim's highlights:
; they are Neovim's alone, and `(command_name) @nospell` would overlap the
; patterns of queries/highlights.scm, which the tree-sitter CLI doesn't
; allow.
(paragraph) @spell

(comment) @spell

(menu_description) @spell

[
  (part_heading)
  (top_heading)
  (chapter_heading)
  (section_heading)
  (subsection_heading)
  (subsubsection_heading)
  (heading)
] @spell

; Not command names, symbols, raw text or definition lines, which name code.
(command_name) @nospell

(symbol_command) @nospell

(raw_text) @nospell

(definition_line) @nospell

; Nor the arguments of the commands for code, keys, files and variables.
((inline_command
  name: (command_name) @_name
  arguments: (brace_arguments) @nospell)
  (#any-of? @_name
    "@code" "@samp" "@kbd" "@key" "@file" "@command" "@option" "@env" "@var" "@t" "@indicateurl"))

(inline_command
  [
    url: (_)
    address: (_)
    file: (_)
  ] @nospell)

; Nor preformatted code, with the blocks in it.
((block
  name: (command_name) @_name) @nospell
  (#any-of? @_name "@example" "@smallexample" "@lisp" "@smalllisp"))
