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
  (argument) @parameter.inside)

; Anchors skip anonymous nodes, so a query can't tell which of the commas
; after an argument is its own where an empty argument follows
; (`@xref{a,,, d}`): the comma is only taken between arguments in
; consecutive fields, listed below, and an argument followed by an empty one
; or by none is alone.
(_
  (argument) @parameter.around
  .
  ","
  .
  ",")

(_
  (argument) @parameter.around .)

(_
  (argument) @parameter.around
  .
  (missing_brace))

(block
  label: (argument) @parameter.around)

(node_line
  node: (argument) @parameter.around
  .
  "," @parameter.around
  .
  next: (argument))

(node_line
  next: (argument) @parameter.around
  .
  "," @parameter.around
  .
  previous: (argument))

(node_line
  previous: (argument) @parameter.around
  .
  "," @parameter.around
  .
  up: (argument))

; Cross-references, `@uref`, `@email`, `@image`, ... (ARGUMENT_COMMANDS in
; grammar.js), and the inline conditionals.
(inline_command
  node: (argument) @parameter.around
  .
  "," @parameter.around
  .
  label: (argument))

(inline_command
  label: (argument) @parameter.around
  .
  "," @parameter.around
  .
  title: (argument))

(inline_command
  title: (argument) @parameter.around
  .
  "," @parameter.around
  .
  file: (argument))

(inline_command
  file: (argument) @parameter.around
  .
  "," @parameter.around
  .
  manual: (argument))

(inline_command
  url: (argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (argument))

(inline_command
  text: (argument) @parameter.around
  .
  "," @parameter.around
  .
  replacement: (argument))

(inline_command
  address: (argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (argument))

(inline_command
  file: (argument) @parameter.around
  .
  "," @parameter.around
  .
  width: (argument))

(inline_command
  width: (argument) @parameter.around
  .
  "," @parameter.around
  .
  height: (argument))

(inline_command
  height: (argument) @parameter.around
  .
  "," @parameter.around
  .
  alt: (argument))

(inline_command
  alt: (argument) @parameter.around
  .
  "," @parameter.around
  .
  extension: (argument))

(inline_command
  abbreviation: (argument) @parameter.around
  .
  "," @parameter.around
  .
  meaning: (argument))

(inline_command
  anchor: (argument) @parameter.around
  .
  "," @parameter.around
  .
  label: (argument))

(inline_command
  format: (argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (argument))

(inline_command
  variable: (argument) @parameter.around
  .
  "," @parameter.around
  .
  text: (argument))

(inline_command
  format: (argument) @parameter.around
  .
  "," @parameter.around
  .
  then: (argument))

(inline_command
  then: (argument) @parameter.around
  .
  "," @parameter.around
  .
  else: (argument))

; `@inforef` and `@link` have no `title` between `label` and `file`.
((inline_command
  name: (command_name) @_name
  label: (argument) @parameter.around
  .
  "," @parameter.around
  .
  file: (argument))
  (#any-of? @_name "@inforef" "@link"))

(inline_command
  format: (argument) @parameter.around
  .
  "," @parameter.around
  .
  body: (raw_text))

(block
  type: (argument) @parameter.around
  .
  "," @parameter.around
  .
  label: (argument))

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
  name: (_)
  .
  (_)+ @entry.inside)

(comment) @comment.inside

(comment)+ @comment.around
