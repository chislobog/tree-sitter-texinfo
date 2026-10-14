; Generated from queries/highlights.scm by script/editor-queries.py.
; Editors resolve overlapping patterns differently: the tree-sitter CLI
; keeps the match that finishes first, Neovim and Helix the last one in the
; file. So keep patterns exclusive, with predicates. Capture nodes used in
; predicates with a real highlight, not a private `@_name`: the CLI drops
; the rest of a match after a capture that isn't a highlight.
; Regexes must mean the same in Rust's syntax (the CLI, Helix) and Vim's
; "very magic" one (Neovim), where `@`, `<`, `>`, `=` and `{` are operators:
; write `[@]`, not `@`.
(comment) @comment

(input_line) @keyword.directive

; Headings
(part_heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.1)

(top_heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.1)

(chapter_heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.1)

(section_heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.2)

(subsection_heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.3)

(subsubsection_heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.4)

((heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.1)
  (#any-of? @keyword "@majorheading" "@chapheading"))

((heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.2)
  (#eq? @keyword "@heading"))

((heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.3)
  (#eq? @keyword "@subheading"))

((heading
  name: (command_name) @keyword
  title: (line_argument)? @markup.heading.4)
  (#eq? @keyword "@subsubheading"))

; Nodes, cross-references and menus
(node_line
  name: (command_name) @keyword
  node: (argument) @markup.link.label)

(node_line
  [
    next: (argument)
    previous: (argument)
    up: (argument)
  ] @markup.link)

(inline_command
  [
    node: (argument)
    anchor: (argument)
  ] @markup.link.label)

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments)? @markup.link.label)
  (#eq? @function "@anchor"))

(inline_command
  [
    url: (argument)
    address: (argument)
  ] @markup.link.url)

; The other arguments of a cross-reference, a URL or an address: the text a
; link shows, or a manual's file. An inline conditional's `text` is text.
(inline_command
  [
    label: (argument)
    title: (argument)
    manual: (argument)
  ] @markup.link)

((inline_command
  name: (command_name) @function
  text: (argument)? @markup.link)
  (#any-of? @function "@uref" "@url" "@email"))

(inline_command
  file: (argument) @string.special.path)

(inline_command
  alt: (argument) @string)

; `@alias NEW = EXISTING` names two commands.
(line_command
  [
    alias: (text)
    target: (text)
  ] @function)

(menu_entry
  node: (menu_node) @markup.link.label)

; A float's label is a cross-reference target, like an anchor.
(block
  label: (argument) @markup.link.label)

(menu_entry
  label: (menu_label) @markup.link)

; Definitions
; Three exclusive patterns, as each node can only be highlighted by one match.
((definition_line
  name: (command_name) @keyword
  entity: (_)? @variable)
  (#any-of? @keyword
    "@defvr" "@defvrx" "@defvar" "@defvarx" "@defopt" "@defoptx" "@defcv" "@defcvx" "@defivar"
    "@defivarx" "@deftypevr" "@deftypevrx" "@deftypevar" "@deftypevarx" "@deftypecv" "@deftypecvx"
    "@deftypeivar" "@deftypeivarx"))

((definition_line
  name: (command_name) @keyword
  entity: (_)? @type)
  (#any-of? @keyword "@deftp" "@deftpx"))

((definition_line
  name: (command_name) @keyword
  entity: (_)? @function)
  (#not-any-of? @keyword
    "@defvr" "@defvrx" "@defvar" "@defvarx" "@defopt" "@defoptx" "@defcv" "@defcvx" "@defivar"
    "@defivarx" "@deftypevr" "@deftypevrx" "@deftypevar" "@deftypevarx" "@deftypecv" "@deftypecvx"
    "@deftypeivar" "@deftypeivarx" "@deftp" "@deftpx"))

(definition_line
  [
    type: (_)
    class: (_)
  ] @type)

(definition_line
  category: (_) @attribute)

(definition_arguments) @variable.parameter

(line_continuation) @punctuation.special

; Blocks
((block
  name: (command_name) @keyword.control.conditional)
  (#match? @keyword.control.conditional "^[@]if"))

((block_end) @keyword.control.conditional
  (#match? @keyword.control.conditional "^[@]end[ \t]*if"))

; The whole block, so every paragraph and nested block in it is raw or
; quoted, around their own highlights.
((block
  name: (command_name) @keyword) @markup.raw.block
  (#any-of? @keyword
    "@example" "@smallexample" "@lisp" "@smalllisp" "@display" "@smalldisplay" "@format"
    "@smallformat"))

((block
  name: (command_name) @keyword) @markup.quote
  (#any-of? @keyword "@quotation" "@smallquotation"))

(raw_block
  body: (raw_text) @markup.raw.block)

; Not the blocks matched above.
((block
  name: (command_name) @keyword)
  (#not-match? @keyword "^[@]if")
  (#not-any-of? @keyword
    "@example" "@smallexample" "@lisp" "@smalllisp" "@display" "@smalldisplay" "@format"
    "@smallformat" "@quotation" "@smallquotation"))

(list
  name: (command_name) @keyword)

(menu
  name: (command_name) @keyword)

(raw_block
  name: (command_name) @keyword)

(macro_definition
  name: (command_name) @keyword
  macro: (text)? @function)

(macro_definition
  parameter: (text) @variable.parameter)

((block_end) @keyword
  (#not-match? @keyword "^[@]end[ \t]*if"))

(unmatched_end) @keyword

; Lists
(item
  name: (command_name) @markup.list)

(itemx
  name: (command_name) @markup.list)

(tab
  name: (command_name) @markup.list)

; Line commands. The generic pattern excludes the commands with their own
; pattern.
((line_command
  name: (command_name) @keyword)
  (#not-any-of? @keyword "@include" "@verbatiminclude" "@setfilename"))

((line_command
  name: (command_name) @keyword
  argument: (line_argument)? @string.special.path)
  (#any-of? @keyword "@include" "@verbatiminclude" "@setfilename"))

(subentry
  name: (command_name) @keyword)

(line_command
  variable: (text) @variable)

(line_command
  value: (raw_text) @string)

(bye
  name: (command_name) @keyword)

(ignored) @comment

; Inline markup
((inline_command
  name: (command_name) @function
  arguments: (brace_arguments)? @markup.raw)
  (#any-of? @function
    "@code" "@samp" "@kbd" "@key" "@file" "@command" "@option" "@env" "@t" "@indicateurl"))

(inline_command
  body: (raw_text) @markup.raw)

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments)? @markup.italic)
  (#any-of? @function "@emph" "@i" "@dfn" "@cite" "@slanted"))

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments)? @markup.bold)
  (#any-of? @function "@strong" "@b"))

((inline_command
  name: (command_name) @function
  arguments: (brace_arguments)? @variable.parameter)
  (#eq? @function "@var"))

; `@noindent` and `@indent`, which say how the next paragraph starts.
((inline_command
  name: (command_name) @keyword)
  (#any-of? @keyword "@noindent" "@indent"))

; `@value{NAME}` refers to a variable set by `@set NAME`.
((inline_command
  name: (command_name) @function
  arguments: (brace_arguments)? @variable)
  (#eq? @function "@value"))

; Accents, like the symbol commands they mostly are: `@'e`, `@u{o}`
((inline_command
  name: (command_name) @constant.character.escape)
  (#any-of? @constant.character.escape
    "@'" "@\"" "@^" "@`" "@~" "@=" "@," "@H" "@dotaccent" "@dotless" "@ogonek" "@ringaccent"
    "@tieaccent" "@u" "@ubaraccent" "@udotaccent" "@v"))

; Everything else, except the commands above.
((inline_command
  name: (command_name) @function)
  (#not-any-of? @function
    "@code" "@samp" "@kbd" "@key" "@file" "@command" "@option" "@env" "@t" "@indicateurl" "@emph"
    "@i" "@dfn" "@cite" "@slanted" "@strong" "@b" "@var" "@value" "@anchor" "@uref" "@url" "@email"
    "@noindent" "@indent" "@'" "@\"" "@^" "@`" "@~" "@=" "@," "@H" "@dotaccent" "@dotless" "@ogonek"
    "@ringaccent" "@tieaccent" "@u" "@ubaraccent" "@udotaccent" "@v"))

; Commands that error recovery left without their usual parent; not a
; generic (command_name) pattern, which would overlap the ones above.
(ERROR
  (command_name) @function)

(symbol_command) @constant.character.escape

[
  "{"
  "}"
] @punctuation.bracket

; A `}` that closes nothing, which editors should report too (README).
(unmatched_brace) @punctuation.bracket

[
  ","
  ":"
  "::"
  "."
] @punctuation.delimiter
