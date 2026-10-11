; Generated from queries/textobjects.scm by script/editor-queries.py.
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
] @class.outer

(_
  heading: (_)
  .
  (_)+ @class.inner)

; Definitions (`@deffn` and friends) and macro definitions are functions: the
; block, and its body after the definition line.
(definition_block) @function.outer

(definition_block
  (definition_line)
  .
  (_)+ @function.inner
  .
  end: (_))

(macro_definition) @function.outer

(macro_definition
  body: (_) @function.inner)

; Arguments: each argument of a command whose arguments split at commas, or of
; a node line, with the comma after it; a brace command's argument; a
; definition line's arguments; a macro's parameters.
(inline_command
  ((argument) @parameter.inner
    .
    ","? @parameter.outer) @parameter.outer)

(node_line
  ((argument) @parameter.inner
    .
    ","? @parameter.outer) @parameter.outer)

(inline_command
  arguments: (brace_arguments) @parameter.inner @parameter.outer)

(definition_line
  arguments: (definition_arguments) @parameter.inner @parameter.outer)

(macro_definition
  parameter: (text) @parameter.inner @parameter.outer)

(comment) @comment.inner

(comment)+ @comment.outer
