; Generated from queries/textobjects.scm by script/editor-queries.py.
; Textobjects, for selecting and moving by them. Editors merge the nodes a
; match captures under one name into a single range; `(_)+` takes all the
; siblings in one match (an anchor after it would match nothing).
; Sections are classes, as in LaTeX's queries: the section, and its content
; after the heading.
[
  (part)
  (top)
  (chapter)
  (section)
  (subsection)
  (subsubsection)
] @class.outer

(_
  heading: (_)
  .
  (_)+ @class.inner)

; Definitions (`@deffn` and friends) and macro definitions are functions: the
; block, and its body after the definition line.
(definition_block) @function.outer

(definition_block
  body: (_) @function.inner)

; In `@defblock`, the body after each `@defline`.
(block
  body: (definition_body) @function.inner)

(macro_definition) @function.outer

(macro_definition
  body: (_) @function.inner)

; Arguments: each argument of a command whose arguments split at commas, of
; a node line or of a float, with the comma after it; a brace command's
; argument; a definition line's arguments; a macro's parameters.
(_
  (argument) @parameter.inner)

; Anchors skip anonymous nodes, so a query can't tell which of the commas
; after an argument is its own where an empty argument follows
; (`@xref{a,,, d}`): the comma is only taken between arguments in
; consecutive fields, listed below, and an argument followed by an empty one
; or by none is alone.
(_
  (argument) @parameter.outer
  .
  ","
  .
  ",")

(_
  (argument) @parameter.outer .)

(_
  (argument) @parameter.outer
  .
  (missing_brace))

(block
  label: (argument) @parameter.outer)

(node_line
  node: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  next: (argument))

(node_line
  next: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  previous: (argument))

(node_line
  previous: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  up: (argument))

; Cross-references, `@uref`, `@email`, `@image`, ... (ARGUMENT_COMMANDS in
; grammar.js), and the inline conditionals.
(inline_command
  node: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  label: (argument))

(inline_command
  label: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  title: (argument))

(inline_command
  title: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  file: (argument))

(inline_command
  file: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  manual: (argument))

(inline_command
  url: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  text: (argument))

(inline_command
  text: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  replacement: (argument))

(inline_command
  address: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  text: (argument))

(inline_command
  file: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  width: (argument))

(inline_command
  width: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  height: (argument))

(inline_command
  height: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  alt: (argument))

(inline_command
  alt: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  extension: (argument))

(inline_command
  abbreviation: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  meaning: (argument))

(inline_command
  anchor: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  label: (argument))

(inline_command
  format: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  text: (argument))

(inline_command
  variable: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  text: (argument))

(inline_command
  format: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  then: (argument))

(inline_command
  then: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  else: (argument))

; `@inforef` and `@link` have no `title` between `label` and `file`.
((inline_command
  name: (command_name) @_name
  label: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  file: (argument))
  (#any-of? @_name "@inforef" "@link"))

(inline_command
  format: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  body: (raw_text))

(block
  type: (argument) @parameter.outer
  .
  "," @parameter.outer
  .
  label: (argument))

(inline_command
  arguments: (brace_arguments) @parameter.inner @parameter.outer)

(definition_line
  arguments: (definition_arguments) @parameter.inner @parameter.outer)

(macro_definition
  parameter: (text) @parameter.inner @parameter.outer)

; Entries: list and table items, multitable rows and their cells, and menu
; entries; inside an item, its body after the `@itemx` lines, and inside a
; row or a cell, its content after the command.
[
  (item)
  (tab)
  (menu_entry)
] @entry.outer

(item
  body: (_) @entry.inner)

(item
  !body
  name: (_)
  .
  (_)+ @entry.inner)

(tab
  name: (_)
  .
  (_)+ @entry.inner)

(comment) @comment.inner

(comment)+ @comment.outer
