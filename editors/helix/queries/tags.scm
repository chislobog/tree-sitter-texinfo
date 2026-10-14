; Generated from queries/tags.scm by script/editor-queries.py.
; Nodes and anchors are what cross-references and menus point to. Only tag
; captures are allowed here, so predicates test the text of the whole
; command.
(node_line
  node: (argument) @name) @definition.section

((inline_command
  arguments: (brace_arguments) @name) @definition.section
  (#match? @definition.section "^[@]anchor[ \t]*[{]"))

(inline_command
  anchor: (argument) @name) @definition.section

(block
  label: (argument) @name) @definition.section

; Documented functions, variables and data types.
((definition_line
  entity: (_) @name) @definition.constant
  (#match? @definition.constant "^[@]def(type)?(vr|var|opt|cv|ivar)x?[ \t]"))

((definition_line
  entity: (_) @name) @definition.type
  (#match? @definition.type "^[@]deftpx?[ \t]"))

((definition_line
  entity: (_) @name) @definition.function
  (#not-match? @definition.function "^[@]def(type)?(vr|var|opt|cv|ivar|tp)x?[ \t]"))

; Macros, aliases and variables, and uses of a variable
(macro_definition
  macro: (text) @name) @definition.macro

(line_command
  alias: (text) @name) @definition.macro

((line_command
  variable: (text) @name) @definition.constant
  (#match? @definition.constant "^[@]set[ \t]"))
