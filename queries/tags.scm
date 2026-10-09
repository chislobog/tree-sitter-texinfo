; Nodes and anchors are what cross-references and menus point to. Only tag
; captures are allowed here, so predicates test the text of the whole
; command.
(node_line
  node: (argument) @name) @definition.node

((inline_command
  arguments: (brace_arguments) @name) @definition.anchor
  (#match? @definition.anchor "^@anchor[{]"))

(inline_command
  anchor: (argument) @name) @definition.anchor

(inline_command
  node: (argument) @name) @reference.node

(menu_entry
  node: (menu_node) @name) @reference.node

; Documented functions and variables
((definition_line
  entity: (_) @name) @definition.variable
  (#match? @definition.variable "^@def(type)?(vr|var|opt|cv|ivar)x?[ \t]"))

((definition_line
  entity: (_) @name) @definition.function
  (#not-match? @definition.function "^@def(type)?(vr|var|opt|cv|ivar)x?[ \t]"))
