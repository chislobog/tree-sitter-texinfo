; When patterns overlap, the one that finishes matching first takes
; precedence, so avoid overlapping a specific pattern with a generic one.

(comment) @comment

(input_line) @keyword.directive

; Headings

(part_heading
  command: (command_name) @keyword
  title: (line_argument) @markup.heading.1)

(top_heading
  command: (command_name) @keyword
  title: (line_argument) @markup.heading.1)

(chapter_heading
  command: (command_name) @keyword
  title: (line_argument) @markup.heading.1)

(section_heading
  command: (command_name) @keyword
  title: (line_argument) @markup.heading.2)

(subsection_heading
  command: (command_name) @keyword
  title: (line_argument) @markup.heading.3)

(subsubsection_heading
  command: (command_name) @keyword
  title: (line_argument) @markup.heading.4)

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
  node: (argument) @markup.link.label)

(inline_command
  [
    url: (argument)
    address: (argument)
  ] @markup.link.url)

(menu_entry
  node: (menu_node) @markup.link.label)

(menu_entry
  label: (menu_label) @markup.link)

; Definitions

((definition_line
  name: (command_name) @_name
  entity: (_) @variable)
  (#match? @_name "^@def(type)?(vr|var|opt|cv|ivar)x?$"))

(definition_line
  entity: (_) @function)

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
  name: (command_name) @keyword.conditional)
  (#match? @keyword.conditional "^@if"))

((block_end) @keyword.conditional
  (#match? @keyword.conditional "^@end[ \t]+if"))

((block
  name: (command_name) @_name
  (paragraph) @markup.raw.block)
  (#any-of? @_name
    "@example" "@smallexample" "@lisp" "@smalllisp" "@display" "@smalldisplay"
    "@format" "@smallformat"))

((block
  name: (command_name) @_name
  (paragraph) @markup.quote)
  (#any-of? @_name "@quotation" "@smallquotation"))

(raw_block
  body: (raw_text) @markup.raw.block)

((block
  name: (command_name) @keyword)
  (#not-match? @keyword "^@if"))

(list
  name: (command_name) @keyword)

(menu
  name: (command_name) @keyword)

(raw_block
  name: (command_name) @keyword)

(definition_line
  name: (command_name) @keyword)

((block_end) @keyword
  (#not-match? @keyword "^@end[ \t]+if"))

; Lists

(item
  name: (command_name) @markup.list)

(itemx
  name: (command_name) @markup.list)

; Line commands

(line_command
  name: (command_name) @keyword)

(bye
  (command_name) @keyword)

(bye
  (ignored) @comment)

; Inline markup

((inline_command
  name: (command_name) @_name
  arguments: (brace_arguments) @markup.raw)
  (#any-of? @_name
    "@code" "@samp" "@kbd" "@key" "@file" "@command" "@option" "@env" "@t"
    "@indicateurl"))

(inline_command
  body: (raw_text) @markup.raw)

((inline_command
  name: (command_name) @_name
  arguments: (brace_arguments) @markup.italic)
  (#any-of? @_name "@emph" "@i" "@dfn" "@cite" "@slanted"))

((inline_command
  name: (command_name) @_name
  arguments: (brace_arguments) @markup.strong)
  (#any-of? @_name "@strong" "@b"))

((inline_command
  name: (command_name) @_name
  arguments: (brace_arguments) @variable.parameter)
  (#eq? @_name "@var"))

; Everything else

(inline_command
  name: (command_name) @function)

; Commands that error recovery left without their usual parent. A generic
; (command_name) pattern would win over the specific ones above, since a
; pattern's captures apply as soon as it finishes matching.
(ERROR
  (command_name) @function)

(symbol_command) @string.escape

[
  "{"
  "}"
] @punctuation.bracket

[
  ","
  ":"
  "::"
  "."
] @punctuation.delimiter
