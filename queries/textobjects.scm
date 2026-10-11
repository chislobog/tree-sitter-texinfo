; Textobjects, for Helix: selecting with `ma`/`mi` and moving with `]`/`[`
; followed by the object's key. Helix merges the nodes a match captures
; under one name into a single range; `(_)+` takes all the siblings in one
; match (an anchor after it would match nothing).
; Sections are classes, as in Helix's LaTeX queries: the section, and its
; content after the heading.
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
  (definition_line)
  .
  (_)+ @function.inside
  .
  end: (_))

(macro_definition) @function.around

(macro_definition
  body: (_) @function.inside)

; Arguments: each argument of a command whose arguments split at commas, or of
; a node line, with the comma after it; a brace command's argument; a
; definition line's arguments; a macro's parameters.
(inline_command
  ((argument) @parameter.inside
    .
    ","? @parameter.around) @parameter.around)

(node_line
  ((argument) @parameter.inside
    .
    ","? @parameter.around) @parameter.around)

(inline_command
  arguments: (brace_arguments) @parameter.inside @parameter.around)

(definition_line
  arguments: (definition_arguments) @parameter.inside @parameter.around)

(macro_definition
  parameter: (text) @parameter.inside @parameter.around)

; Entries: list and table items, multitable rows and their cells, and menu
; entries; inside an item or a cell, its content after the command.
[
  (item)
  (tab)
  (menu_entry)
] @entry.around

(item
  name: (_)
  .
  (_)+ @entry.inside)

(tab
  name: (_)
  .
  (_)+ @entry.inside)

(comment) @comment.inside

(comment)+ @comment.around
