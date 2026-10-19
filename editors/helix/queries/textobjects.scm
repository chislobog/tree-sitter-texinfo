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
] @class.around

(_
  heading: (_)
  .
  (_)+ @class.inside)

; Definitions (`@deffn` and friends) and macro definitions are functions: the
; block, and its body after the definition line.
(definition_block) @function.around

(definition_block
  body: (_) @function.inside)

; In `@defblock`, the body after each `@defline`.
(block
  body: (definition_body) @function.inside)

(macro_definition) @function.around

(macro_definition
  body: (_) @function.inside)

; Arguments: each argument of a command whose arguments split at commas, of
; a node line or of a float, with the comma after it; a brace command's
; argument; a definition line's arguments; a macro's parameters.
(_
  (comma_argument) @parameter.inside)

; Anchors skip anonymous nodes, so a query can't tell which of the commas
; after an argument is its own where an empty argument follows
; (`@xref{a,,, d}`): the comma is only taken between arguments in
; consecutive fields, listed below, and an argument followed by an empty one
; or by none is alone.
(_
  (comma_argument) @parameter.around
  .
  ","
  .
  ",")

(_
  (comma_argument) @parameter.around .)

(_
  (comma_argument) @parameter.around
  .
  (missing_brace))

(block
  label: (comma_argument) @parameter.around)

(node_line
  node: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  next: (comma_argument))

(node_line
  next: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  previous: (comma_argument))

(node_line
  previous: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  up: (comma_argument))

; Cross-references, `@uref`, `@email`, `@image`, ... (ARGUMENT_COMMANDS in
; grammar.js), and the inline conditionals.
(inline_command
  node: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  label: (comma_argument))

(inline_command
  label: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  title: (comma_argument))

(inline_command
  title: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  file: (comma_argument))

(inline_command
  file: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  manual: (comma_argument))

(inline_command
  url: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (comma_argument))

(inline_command
  text: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  replacement: (comma_argument))

(inline_command
  address: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (comma_argument))

(inline_command
  file: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  width: (comma_argument))

(inline_command
  width: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  height: (comma_argument))

(inline_command
  height: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  alt: (comma_argument))

(inline_command
  alt: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  extension: (comma_argument))

(inline_command
  abbreviation: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  meaning: (comma_argument))

(inline_command
  anchor: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  label: (comma_argument))

(inline_command
  format: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (comma_argument))

(inline_command
  variable: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (comma_argument))

(inline_command
  format: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  then: (comma_argument))

(inline_command
  then: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  else: (comma_argument))

; `@inforef` and `@link` have no `title` between `label` and `file`.
((inline_command
  name: (command_name) @_name
  label: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  file: (comma_argument))
  (#any-of? @_name "@inforef" "@link"))

(inline_command
  format: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  body: (raw_text))

(block
  type: (comma_argument) @parameter.around
  .
  "," @parameter.around
  .
  label: (comma_argument))

(inline_command
  arguments: (brace_arguments) @parameter.inside @parameter.around)

(definition_line
  arguments: (definition_arguments) @parameter.inside @parameter.around)

(macro_definition
  parameter: (text) @parameter.inside @parameter.around)

; Entries: list and table items, multitable rows and their cells, and menu
; entries; inside an item, its body after the `@itemx` lines, and inside a
; row or a cell, its content after the command.
[
  (item)
  (tab)
  (menu_entry)
] @entry.around

(item
  body: (_) @entry.inside)

(item
  !body
  name: (_)
  .
  (_)+ @entry.inside)

(tab
  (cell) @entry.inside)

; A row's first cell, which no `@tab` starts.
(item
  name: (_)
  .
  (cell) @entry.around @entry.inside)

(comment) @comment.inside

(comment)+ @comment.around
